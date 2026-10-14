#include "reworkbench/analysis.hpp"
#include "reworkbench/bytecore.hpp"
#include "decoder.hpp"

#include <QSet>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace reworkbench {
namespace {

constexpr quint64 instruction_ceiling = 200000;
constexpr quint32 function_instruction_ceiling = 4096;
constexpr quint32 function_ceiling = 10000;
constexpr quint32 string_ceiling = 10000;

bool checked_add(quint64 left, quint64 right, quint64& value) {
    if (right > std::numeric_limits<quint64>::max() - left) return false;
    value = left + right;
    return true;
}

QString key(const Address& address) {
    // Length prefix avoids collisions even when an imported space contains ':'.
    return QString::number(address.space.size()) + QLatin1Char(':') + address.space + QLatin1Char(':') + QString::number(address.value, 16);
}

void warn(QStringList& warnings, const QString& message) {
    if (!warnings.contains(message)) warnings.push_back(message);
}

Address canonical(Address address, Architecture architecture) {
    if (architecture == Architecture::Thumb) address.value &= ~quint64(1);
    return address;
}

bool instruction_aligned(Address address, Architecture architecture) {
    switch (architecture) {
    case Architecture::Thumb: return (address.value & 1) == 0;
    case Architecture::Arm:
    case Architecture::Arm64:
    case Architecture::Mips: return (address.value & 3) == 0;
    default: return true;
    }
}

// Read only a backed executable interval. An ambiguous overlapping map is not
// decoded by arbitrarily choosing one region. No request can exceed 16 bytes.
QByteArray decode_window(const BinaryFile& file, const BinaryMetadata& metadata,
                         Address address, QString& reason) {
    std::optional<quint64> offset;
    quint64 available = 0;
    for (const auto& region : metadata.regions) {
        if (!region.executable || !region.file_offset || region.start.space != address.space || address.value < region.start.value) continue;
        const quint64 relative = address.value - region.start.value;
        if (relative >= region.virtual_size || relative >= region.file_size) continue;
        quint64 mapped = 0;
        if (!checked_add(*region.file_offset, relative, mapped) || mapped >= file.size()) continue;
        const quint64 count = std::min({ quint64(16), region.virtual_size - relative, region.file_size - relative, file.size() - mapped });
        if (offset && *offset != mapped) {
            reason = QStringLiteral("Mapeamento executável sobreposto e ambíguo em %1.").arg(address.text());
            return {};
        }
        offset = mapped;
        available = std::max(available, count);
    }
    if (!offset || available == 0) {
        reason = QStringLiteral("Sem bytes executáveis respaldados em arquivo em %1.").arg(address.text());
        return {};
    }
    return file.read(*offset, available);
}

void build_blocks(Function& function, std::stop_token stop) {
    if (function.instructions.isEmpty()) return;
    std::map<quint64, quint32> index;
    QSet<quint64> leaders;
    for (qsizetype n = 0; n < function.instructions.size(); ++n) {
        if (stop.stop_requested()) {
            function.partial = true;
            warn(function.warnings, QStringLiteral("Construção de CFG interrompida; blocos indisponíveis."));
            return;
        }
        index.emplace(function.instructions[n].address.value, static_cast<quint32>(n));
    }
    leaders.insert(function.instructions.front().address.value);
    // A backward path may sort before the entry. The entry still starts its
    // own block even when that path falls through into it.
    if (index.contains(function.address.value)) leaders.insert(function.address.value);
    for (qsizetype n = 0; n < function.instructions.size(); ++n) {
        const auto& instruction = function.instructions[n];
        if (instruction.direct_target && instruction.flow != FlowKind::Call && instruction.direct_target->space == function.address.space && index.contains(instruction.direct_target->value)) leaders.insert(instruction.direct_target->value);
        quint64 next = 0;
        const bool next_valid = checked_add(instruction.address.value, static_cast<quint64>(instruction.bytes.size()), next);
        if (n + 1 < function.instructions.size() && (!next_valid || function.instructions[n + 1].address.value != next || instruction.flow != FlowKind::Normal)) leaders.insert(function.instructions[n + 1].address.value);
    }
    QVector<BasicBlock> blocks;
    for (qsizetype n = 0; n < function.instructions.size(); ++n) {
        if (stop.stop_requested()) {
            function.partial = true;
            warn(function.warnings, QStringLiteral("Construção de CFG interrompida; blocos indisponíveis."));
            return;
        }
        const auto& instruction = function.instructions[n];
        if (blocks.isEmpty() || leaders.contains(instruction.address.value)) {
            BasicBlock block;
            block.address = instruction.address;
            blocks.push_back(std::move(block));
        }
        blocks.back().instruction_indices.push_back(static_cast<quint32>(n));
    }
    for (auto& block : blocks) {
        const auto& last = function.instructions[block.instruction_indices.back()];
        auto successor = [&](Address address) {
            if (address.space != function.address.space || !index.contains(address.value) || !leaders.contains(address.value)) return;
            if (!block.successors.contains(address)) block.successors.push_back(std::move(address));
        };
        if ((last.flow == FlowKind::Jump || last.flow == FlowKind::ConditionalJump) && last.direct_target) successor(*last.direct_target);
        quint64 next = 0;
        if (last.falls_through && checked_add(last.address.value, static_cast<quint64>(last.bytes.size()), next)) successor(Address{ last.address.space, next });
    }
    function.blocks = std::move(blocks);
}

std::filesystem::path native_path(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    const auto utf8 = path.toUtf8();
    return std::filesystem::u8path(utf8.constData(), utf8.constData() + utf8.size());
#endif
}

using OwnedLocation = std::pair<QString, quint64>;
struct InstructionOwner {
    quint64 size = 0;
    Address function;
};

bool overlaps_owned(const std::map<OwnedLocation, InstructionOwner>& owners,
                    Address address, quint64 size, std::optional<Address>& owner) {
    const OwnedLocation location{ address.space, address.value };
    auto next = owners.lower_bound(location);
    if (next != owners.end() && next->first.first == address.space && next->first.second >= address.value && next->first.second - address.value < size) {
        owner = next->second.function;
        return true;
    }
    if (next != owners.begin()) {
        const auto previous = std::prev(next);
        if (previous->first.first == address.space && address.value >= previous->first.second && address.value - previous->first.second < previous->second.size) {
            owner = previous->second.function;
            return true;
        }
    }
    return false;
}

} // namespace

AnalysisSnapshot analyze_binary(const QString& path, const OpenOptions& options,
                                const AnalysisLimits& requested, std::stop_token stop,
                                ProgressCallback progress) {
    AnalysisSnapshot result;
    result.options = options;
    result.metadata.source_path = path;
    result.capabilities = { QStringLiteral("own-format-loaders"), QStringLiteral("bounded-file-bytes"), QStringLiteral("bytecore-entropy"), QStringLiteral("bytecore-ascii-strings") };
    AnalysisLimits limits = requested;
    limits.max_instructions = std::min(limits.max_instructions, instruction_ceiling);
    limits.max_function_instructions = std::min(limits.max_function_instructions, function_instruction_ceiling);
    limits.max_functions = std::min(limits.max_functions, function_ceiling);
    limits.max_strings = std::min(limits.max_strings, string_ceiling);
    limits.timeout_seconds = std::clamp(limits.timeout_seconds, 0, 300);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limits.timeout_seconds);
    std::stop_source operation;
    std::stop_callback external_cancel(stop, [&operation] { operation.request_stop(); });
    // A separate stop source lets the existing bytecore library observe the same
    // deadline without changing its API or launching its executable.
    std::jthread deadline_watch([&operation, deadline](std::stop_token timer_stop) {
        std::mutex mutex;
        std::condition_variable_any condition;
        std::unique_lock lock(mutex);
        condition.wait_until(lock, timer_stop, deadline, [] { return false; });
        if (!timer_stop.stop_requested()) operation.request_stop();
    });
    const auto token = operation.get_token();
    auto interrupted = [&] {
        if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) operation.request_stop();
        return token.stop_requested();
    };
    auto record_interruption = [&] {
        result.state = stop.stop_requested() ? QStringLiteral("cancelled") : QStringLiteral("partial");
        warn(result.warnings, stop.stop_requested() ? QStringLiteral("Análise cancelada; resultados já produzidos preservados.") : QStringLiteral("Prazo de análise atingido; resultados já produzidos preservados."));
    };
    auto report = [&](const QString& stage, quint64 completed, quint64 total) {
        if (progress) progress(Progress{ stage, completed, total });
    };

    try {
        if (interrupted()) { record_interruption(); return result; }
        report(QStringLiteral("metadados"), 0, 1);
        auto file = std::make_shared<BinaryFile>(path);
        result.metadata = load_binary(file, options, token);
        report(QStringLiteral("metadados"), 1, 1);
        if (interrupted()) { record_interruption(); return result; }
        report(QStringLiteral("entropia e strings"), 0, result.metadata.size);
        const auto scan = bytecore::scan_file(native_path(path), bytecore::ScanOptions{ 4, limits.max_strings }, token);
        result.entropy = scan.entropy;
        result.strings_truncated = scan.strings_truncated;
        result.strings.reserve(static_cast<qsizetype>(scan.strings.size()));
        for (const auto& string : scan.strings) {
            if (interrupted()) { record_interruption(); return result; }
            result.strings.push_back(ByteString{ string.offset, QString::fromLatin1(string.value.data(), static_cast<qsizetype>(string.value.size())) });
        }
        if (scan.size != result.metadata.size) {
            result.state = QStringLiteral("partial");
            warn(result.warnings, QStringLiteral("O tamanho da origem mudou durante a leitura; análise de instruções não iniciada."));
            return result;
        }
        report(QStringLiteral("entropia e strings"), scan.size, scan.size);
        if (interrupted()) { record_interruption(); return result; }

        detail::InstructionDecoder decoder(result.metadata.architecture, result.metadata.byte_order, result.metadata.bits);
        if (!decoder.available()) {
            result.state = QStringLiteral("partial");
            warn(result.warnings, decoder.explanation());
            return result;
        }
        result.capabilities << QStringLiteral("capstone5-instruction-decoder") << QStringLiteral("own-seeded-function-discovery") << QStringLiteral("own-direct-xrefs") << QStringLiteral("own-partial-cfg") << QStringLiteral("own-partial-low-level-ir");
        warn(result.warnings, QStringLiteral("Funções são candidatas exploradas por entradas, símbolos de função e chamadas diretas. Fluxos indiretos, exceções, convenções de chamada e limites exatos de função não são recuperados."));
        if (result.metadata.architecture == Architecture::Arm || result.metadata.architecture == Architecture::Thumb) warn(result.warnings, QStringLiteral("ARM/Thumb usa um modo fixo da imagem; interworking, IT e mudanças de estado interrompem a exploração afetada."));
        if (result.metadata.architecture == Architecture::Mips) warn(result.warnings, QStringLiteral("MIPS: delay slots e branch-likely não modelados. Funções com transferência de controle não recebem CFG."));

        std::deque<Address> seeds;
        QSet<QString> known_seeds;
        std::map<OwnedLocation, QString> symbol_names;
        QSet<QString> incompatible_modes;
        for (const auto& symbol : result.metadata.symbols) {
            if (interrupted()) { record_interruption(); return result; }
            if (symbol.kind == QStringLiteral("function-thumb-unselected") || symbol.kind == QStringLiteral("function-arm-unselected"))
                incompatible_modes.insert(key(canonical(symbol.address, result.metadata.architecture)));
        }
        bool seed_limit_hit = false;
        auto seed = [&](Address address) {
            address = canonical(std::move(address), result.metadata.architecture);
            const auto identity = key(address);
            if (incompatible_modes.contains(identity)) return;
            if (known_seeds.contains(identity)) return;
            if (!instruction_aligned(address, result.metadata.architecture) || !executable_address(result.metadata, address) || !file_offset(result.metadata, address)) return;
            if (known_seeds.size() >= limits.max_functions) { seed_limit_hit = true; return; }
            known_seeds.insert(identity);
            seeds.push_back(std::move(address));
        };
        for (const auto& entry : result.metadata.entries) {
            if (interrupted()) { record_interruption(); return result; }
            seed(entry);
        }
        for (const auto& entry : options.entries) {
            if (interrupted()) { record_interruption(); return result; }
            seed(entry);
        }
        for (const auto& symbol : result.metadata.symbols) {
            if (interrupted()) { record_interruption(); return result; }
            if (symbol.kind == QStringLiteral("function") || symbol.kind == QStringLiteral("func") || symbol.kind == QStringLiteral("STT_FUNC")) {
                seed(symbol.address);
                const auto address = canonical(symbol.address, result.metadata.architecture);
                if (!symbol.name.isEmpty()) symbol_names.try_emplace(OwnedLocation{ address.space, address.value }, symbol.name);
            }
        }
        if (seeds.empty()) {
            result.state = QStringLiteral("partial");
            warn(result.warnings, QStringLiteral("Sem entrada ou símbolo de função em bytes executáveis respaldados. Nenhuma função foi inventada por varredura linear."));
            return result;
        }

        std::map<OwnedLocation, InstructionOwner> owners;
        quint64 instruction_count = 0;
        auto reference = [&](Address source, Address target, const QString& type, Address function) {
            result.references.push_back(Reference{ std::move(source), std::move(target), type, std::move(function) });
        };
        while (!seeds.empty() && !interrupted() && result.functions.size() < limits.max_functions && instruction_count < limits.max_instructions) {
            const Address entry = seeds.front();
            seeds.pop_front();
            Function function;
            function.address = entry;
            const auto symbol_name = symbol_names.find(OwnedLocation{ entry.space, entry.value });
            function.name = symbol_name == symbol_names.end() ? QStringLiteral("sub_") + QString::number(entry.value, 16) : symbol_name->second;
            std::map<quint64, Instruction> instructions;
            std::deque<Address> paths{ entry };
            QSet<quint64> scheduled{ entry.value };
            bool suppress_cfg = false;
            auto function_warning = [&](const QString& message) {
                function.partial = true;
                warn(function.warnings, message);
            };
            auto path = [&](Address target) {
                target = canonical(std::move(target), result.metadata.architecture);
                if (target.space != entry.space || scheduled.contains(target.value)) return;
                if (incompatible_modes.contains(key(target))) {
                    function_warning(QStringLiteral("Destino %1 pertence a símbolo ARM/Thumb de outro modo; caminho não decodificado.").arg(target.text()));
                    return;
                }
                if (!instruction_aligned(target, result.metadata.architecture) || !executable_address(result.metadata, target) || !file_offset(result.metadata, target)) {
                    function_warning(QStringLiteral("Destino %1 sem instrução executável respaldada ou com alinhamento inválido.").arg(target.text()));
                    return;
                }
                if (target != entry && known_seeds.contains(key(target))) return;
                if (scheduled.size() >= limits.max_function_instructions) {
                    function_warning(QStringLiteral("Limite de caminhos/instruções desta função atingido."));
                    return;
                }
                scheduled.insert(target.value);
                paths.push_back(std::move(target));
            };

            while (!paths.empty() && !interrupted()) {
                Address pc = paths.front();
                paths.pop_front();
                while (!interrupted()) {
                    if (incompatible_modes.contains(key(pc))) {
                        function_warning(QStringLiteral("Caminho alcançou símbolo ARM/Thumb de outro modo em %1; exploração interrompida.").arg(pc.text()));
                        break;
                    }
                    if (instructions.contains(pc.value)) break;
                    if (instructions.size() >= limits.max_function_instructions || instruction_count >= limits.max_instructions) {
                        function_warning(QStringLiteral("Limite de instruções atingido; função incompleta."));
                        paths.clear();
                        break;
                    }
                    if (pc != entry && known_seeds.contains(key(pc))) break;
                    std::optional<Address> owner;
                    if (overlaps_owned(owners, pc, 1, owner)) {
                        function_warning(QStringLiteral("Caminho em %1 alcança bytes já atribuídos a %2; cauda compartilhada/sobreposição não recuperada.").arg(pc.text(), owner->text()));
                        break;
                    }
                    if (!instruction_aligned(pc, result.metadata.architecture)) {
                        function_warning(QStringLiteral("Alinhamento de instrução inválido em %1.").arg(pc.text()));
                        break;
                    }
                    QString read_reason;
                    const auto bytes = decode_window(*file, result.metadata, pc, read_reason);
                    if (bytes.isEmpty()) {
                        function_warning(read_reason);
                        break;
                    }
                    auto decoded = decoder.decode(bytes, pc);
                    if (!decoded) {
                        function_warning(QStringLiteral("Bytes inválidos, incompletos ou não suportados pelo decoder em %1; caminho interrompido.").arg(pc.text()));
                        break;
                    }
                    auto instruction = std::move(decoded->instruction);
                    if (overlaps_owned(owners, pc, static_cast<quint64>(instruction.bytes.size()), owner)) {
                        function_warning(QStringLiteral("Instrução em %1 se sobrepõe a instrução já recuperada; caminho interrompido.").arg(pc.text()));
                        break;
                    }
                    for (const auto& warning : decoded->warnings) function_warning(warning);
                    if (result.metadata.architecture == Architecture::Mips && instruction.flow != FlowKind::Normal) suppress_cfg = true;
                    if (instruction.direct_target) {
                        *instruction.direct_target = canonical(*instruction.direct_target, result.metadata.architecture);
                        const QString type = instruction.flow == FlowKind::Call ? QStringLiteral("call") : instruction.flow == FlowKind::ConditionalJump ? QStringLiteral("conditional-jump") : QStringLiteral("jump");
                        reference(pc, *instruction.direct_target, type, entry);
                        if (instruction.flow == FlowKind::Call && !decoded->target_changes_mode) {
                            seed(*instruction.direct_target);
                            if (!executable_address(result.metadata, *instruction.direct_target) || !file_offset(result.metadata, *instruction.direct_target)) function_warning(QStringLiteral("Chamada direta para %1 sem bytes executáveis disponíveis.").arg(instruction.direct_target->text()));
                        }
                    }
                    if (instruction.memory_target) reference(pc, *instruction.memory_target, instruction.flow == FlowKind::Call ? QStringLiteral("call-slot") : QStringLiteral("data"), entry);
                    quint64 next = 0;
                    const bool has_next = checked_add(pc.value, static_cast<quint64>(instruction.bytes.size()), next);
                    if (!has_next) instruction.falls_through = false;
                    const auto flow = instruction.flow;
                    const auto target = instruction.direct_target;
                    const bool falls_through = instruction.falls_through;
                    owners.emplace(OwnedLocation{ pc.space, pc.value }, InstructionOwner{ static_cast<quint64>(instruction.bytes.size()), entry });
                    instructions.emplace(pc.value, std::move(instruction));
                    ++instruction_count;
                    if ((instruction_count & 0xff) == 0) report(QStringLiteral("instruções"), instruction_count, limits.max_instructions);
                    if (decoded->stop_path) break;
                    if ((flow == FlowKind::Jump || flow == FlowKind::ConditionalJump) && target) path(*target);
                    if (!falls_through || !has_next) break;
                    const Address continuation{ pc.space, next };
                    if (continuation != entry && known_seeds.contains(key(continuation))) {
                        reference(pc, continuation, QStringLiteral("flow-to-function"), entry);
                        break;
                    }
                    pc = continuation;
                }
            }
            if (interrupted()) function_warning(QStringLiteral("Exploração interrompida; função incompleta."));
            function.instructions.reserve(static_cast<qsizetype>(instructions.size()));
            for (auto& [address, instruction] : instructions) {
                (void)address;
                function.instructions.push_back(std::move(instruction));
            }
            if (!suppress_cfg) build_blocks(function, token);
            if (function.partial) result.state = QStringLiteral("partial");
            result.functions.push_back(std::move(function));
            report(QStringLiteral("funções"), static_cast<quint64>(result.functions.size()), static_cast<quint64>(known_seeds.size()));
        }
        if (interrupted()) record_interruption();
        else if (!seeds.empty() || seed_limit_hit || instruction_count >= limits.max_instructions) {
            result.state = QStringLiteral("partial");
            warn(result.warnings, QStringLiteral("Limite global de funções/instruções atingido; candidatos restantes não foram explorados."));
        }
        return result;
    } catch (const Cancelled&) {
        record_interruption();
        return result;
    } catch (const bytecore::ScanCancelled&) {
        record_interruption();
        warn(result.warnings, QStringLiteral("Varredura de entropia/strings interrompida; agregados incompletos não foram apresentados."));
        return result;
    }
}

} // namespace reworkbench
