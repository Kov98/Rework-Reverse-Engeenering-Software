#include "decoder.hpp"
#include "reworkbench/analysis.hpp"

#include <limits>
#include <utility>

#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
#include <capstone/capstone.h>
#if CS_API_MAJOR != 5
#error "Reworkbench's optional instruction decoder requires Capstone API major 5."
#endif
#endif

namespace reworkbench {
namespace detail {

struct InstructionDecoder::Impl {
    Architecture architecture = Architecture::Unknown;
    int bits = 0;
    QString reason;
#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
    csh handle = 0;
    cs_insn* scratch = nullptr;
    ~Impl() {
        if (scratch) cs_free(scratch, 1);
        if (handle) cs_close(&handle);
    }
#endif
};

#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
namespace {

QString register_name(csh handle, unsigned int reg) {
    const char* name = cs_reg_name(handle, reg);
    return name ? QString::fromLatin1(name) : QString{};
}

bool add_unsigned(quint64 a, quint64 b, quint64& result) {
    if (b > std::numeric_limits<quint64>::max() - a) return false;
    result = a + b;
    return true;
}

std::optional<quint64> displaced(quint64 base, qint64 displacement) {
    if (displacement >= 0) {
        quint64 value = 0;
        if (add_unsigned(base, static_cast<quint64>(displacement), value)) return value;
    } else {
        const quint64 magnitude = quint64(-(displacement + 1)) + 1;
        if (magnitude <= base) return base - magnitude;
    }
    return std::nullopt;
}

int scalar_register_width(const QString& name, Architecture architecture, int bits) {
    if (architecture == Architecture::Arm64) {
        if (name == QStringLiteral("sp") || name == QStringLiteral("fp") || name == QStringLiteral("lr") || name == QStringLiteral("xzr") || name.startsWith(QLatin1Char('x'))) return 64;
        if (name == QStringLiteral("wsp") || name == QStringLiteral("wzr") || name.startsWith(QLatin1Char('w'))) return 32;
        if (name.startsWith(QLatin1Char('b'))) return 8;
        if (name.startsWith(QLatin1Char('h'))) return 16;
        if (name.startsWith(QLatin1Char('s'))) return 32;
        if (name.startsWith(QLatin1Char('d'))) return 64;
        if (name.startsWith(QLatin1Char('q')) || name.startsWith(QLatin1Char('v'))) return 128;
    } else if (architecture == Architecture::Arm || architecture == Architecture::Thumb) {
        if (name.startsWith(QLatin1Char('r')) || name == QStringLiteral("sp") || name == QStringLiteral("lr") || name == QStringLiteral("pc")) return 32;
        if (name.startsWith(QLatin1Char('s'))) return 32;
        if (name.startsWith(QLatin1Char('d'))) return 64;
        if (name.startsWith(QLatin1Char('q'))) return 128;
    } else if (architecture == Architecture::Mips) {
        // Floating-point/vector register size depends on mode, not the GPR mode.
        if (!name.startsWith(QLatin1Char('f')) && !name.startsWith(QLatin1Char('w')) && name != QStringLiteral("fcc0")) return bits;
    }
    return 0;
}

int memory_width(const QString& mnemonic, Architecture architecture, int fallback) {
    if (architecture == Architecture::Arm || architecture == Architecture::Thumb || architecture == Architecture::Arm64) {
        if (mnemonic.startsWith(QStringLiteral("ldrb")) || mnemonic.startsWith(QStringLiteral("strb")) || mnemonic.startsWith(QStringLiteral("ldrsb"))) return 8;
        if (mnemonic.startsWith(QStringLiteral("ldrh")) || mnemonic.startsWith(QStringLiteral("strh")) || mnemonic.startsWith(QStringLiteral("ldrsh"))) return 16;
        if (mnemonic.startsWith(QStringLiteral("ldrsw"))) return 32;
        if (mnemonic.startsWith(QStringLiteral("ldrd")) || mnemonic.startsWith(QStringLiteral("strd"))) return 64;
        if (mnemonic.startsWith(QStringLiteral("ldr")) || mnemonic.startsWith(QStringLiteral("str"))) return fallback;
    } else if (architecture == Architecture::Mips) {
        if (mnemonic == QStringLiteral("lb") || mnemonic == QStringLiteral("lbu") || mnemonic == QStringLiteral("sb")) return 8;
        if (mnemonic == QStringLiteral("lh") || mnemonic == QStringLiteral("lhu") || mnemonic == QStringLiteral("sh")) return 16;
        if (mnemonic == QStringLiteral("lw") || mnemonic == QStringLiteral("lwu") || mnemonic == QStringLiteral("sw") || mnemonic == QStringLiteral("lwc1") || mnemonic == QStringLiteral("swc1")) return 32;
        if (mnemonic == QStringLiteral("ld") || mnemonic == QStringLiteral("sd") || mnemonic == QStringLiteral("ldc1") || mnemonic == QStringLiteral("sdc1")) return 64;
    }
    return 0;
}

bool is_trap(const QString& mnemonic) {
    static const QStringList traps = { QStringLiteral("ud2"), QStringLiteral("hlt"), QStringLiteral("int"), QStringLiteral("int1"), QStringLiteral("int3"), QStringLiteral("syscall"), QStringLiteral("sysenter"), QStringLiteral("svc"), QStringLiteral("hvc"), QStringLiteral("smc"), QStringLiteral("udf"), QStringLiteral("brk"), QStringLiteral("break") };
    return traps.contains(mnemonic);
}

void decode_x86(csh handle, const cs_insn& native, Instruction& out, Architecture architecture) {
    const auto& detail = native.detail->x86;
    for (unsigned int n = 0; n < detail.op_count; ++n) {
        const auto& source = detail.operands[n];
        Operand operand;
        operand.width_bits = int(source.size) * 8;
        if (source.type == X86_OP_REG) {
            operand.kind = OperandKind::Register;
            operand.reg = register_name(handle, source.reg);
        } else if (source.type == X86_OP_IMM) {
            operand.kind = OperandKind::Immediate;
            operand.immediate = static_cast<quint64>(source.imm);
            // Capstone x86_op.size describes the logical operand width,
            // not necessarily the bytes encoded by an imm8/imm32 form.
            // Never infer sign extension from that logical width.
            const int encoded_offset = int(detail.encoding.imm_offset);
            const int encoded_size = int(detail.encoding.imm_size);
            const int available = out.bytes.size();
            if (encoded_size >= 1 && encoded_size <= 8
                && encoded_offset > 0 && encoded_offset <= available
                && encoded_size <= available - encoded_offset) {
                operand.encoded_immediate_bits = encoded_size * 8;
            }
        } else if (source.type == X86_OP_MEM) {
            operand.kind = OperandKind::Memory;
            operand.memory_base = register_name(handle, source.mem.base);
            operand.memory_index = register_name(handle, source.mem.index);
            operand.memory_scale = source.mem.scale;
            operand.displacement = source.mem.disp;
            if (source.mem.segment == X86_REG_INVALID && source.mem.index == X86_REG_INVALID) {
                std::optional<quint64> target;
                if (source.mem.base == X86_REG_RIP) {
                    quint64 next = 0;
                    if (add_unsigned(out.address.value, static_cast<quint64>(out.bytes.size()), next)) target = displaced(next, source.mem.disp);
                } else if (source.mem.base == X86_REG_EIP) {
                    // EIP addressing wraps within the 32-bit address domain.
                    target = (out.address.value + static_cast<quint64>(out.bytes.size()) + static_cast<quint64>(source.mem.disp)) & 0xffffffffULL;
                } else if (source.mem.base == X86_REG_INVALID) {
                    quint64 absolute = static_cast<quint64>(source.mem.disp);
                    if (detail.addr_size == 2) absolute &= 0xffffULL;
                    else if (detail.addr_size == 4 || architecture == Architecture::X86) absolute &= 0xffffffffULL;
                    target = absolute;
                }
                if (target && !out.memory_target) out.memory_target = Address{ out.address.space, *target };
            }
        }
        out.operands.push_back(std::move(operand));
    }
}

void decode_arm(csh handle, const cs_insn& native, Instruction& out, Architecture architecture) {
    const auto& detail = native.detail->arm;
    for (unsigned int n = 0; n < detail.op_count; ++n) {
        const auto& source = detail.operands[n];
        Operand operand;
        if (source.type == ARM_OP_REG) {
            operand.kind = OperandKind::Register;
            operand.reg = register_name(handle, source.reg);
            operand.width_bits = scalar_register_width(operand.reg, architecture, 32);
        } else if (source.type == ARM_OP_IMM) {
            operand.kind = OperandKind::Immediate;
            operand.immediate = static_cast<quint32>(source.imm);
            operand.width_bits = 32;
        } else if (source.type == ARM_OP_MEM) {
            operand.kind = OperandKind::Memory;
            operand.memory_base = register_name(handle, source.mem.base);
            operand.memory_index = register_name(handle, source.mem.index);
            operand.memory_scale = source.mem.scale;
            if (source.mem.lshift >= 0 && source.mem.lshift < 30) operand.memory_scale *= (1 << source.mem.lshift);
            operand.displacement = source.mem.disp;
            operand.width_bits = memory_width(out.mnemonic, architecture, 32);
            if (source.mem.base == ARM_REG_PC && source.mem.index == ARM_REG_INVALID && !detail.writeback) {
                quint64 pc = 0;
                if (add_unsigned(out.address.value, architecture == Architecture::Thumb ? 4 : 8, pc)) {
                    if (architecture == Architecture::Thumb) pc &= ~quint64(3);
                    if (auto target = displaced(pc, source.mem.disp)) out.memory_target = Address{ out.address.space, *target };
                }
            }
        }
        out.operands.push_back(std::move(operand));
    }
}

void decode_arm64(csh handle, const cs_insn& native, Instruction& out) {
    const auto& detail = native.detail->arm64;
    int data_width = 0;
    if (detail.op_count && detail.operands[0].type == ARM64_OP_REG) data_width = scalar_register_width(register_name(handle, detail.operands[0].reg), Architecture::Arm64, 64);
    for (unsigned int n = 0; n < detail.op_count; ++n) {
        const auto& source = detail.operands[n];
        Operand operand;
        if (source.type == ARM64_OP_REG) {
            operand.kind = OperandKind::Register;
            operand.reg = register_name(handle, source.reg);
            operand.width_bits = source.vas == ARM64_VAS_INVALID ? scalar_register_width(operand.reg, Architecture::Arm64, 64) : 0;
        } else if (source.type == ARM64_OP_IMM) {
            operand.kind = OperandKind::Immediate;
            operand.immediate = static_cast<quint64>(source.imm);
            operand.width_bits = 64;
            if (n > 0 && (native.id == ARM64_INS_LDR || native.id == ARM64_INS_LDRSW || native.id == ARM64_INS_PRFM)) out.memory_target = Address{ out.address.space, operand.immediate };
        } else if (source.type == ARM64_OP_MEM) {
            operand.kind = OperandKind::Memory;
            operand.memory_base = register_name(handle, source.mem.base);
            operand.memory_index = register_name(handle, source.mem.index);
            operand.displacement = source.mem.disp;
            operand.width_bits = memory_width(out.mnemonic, Architecture::Arm64, data_width);
            if (source.shift.type == ARM64_SFT_LSL && source.shift.value < 30) operand.memory_scale = (1 << source.shift.value);
        }
        out.operands.push_back(std::move(operand));
    }
}

void decode_mips(csh handle, const cs_insn& native, Instruction& out, int bits) {
    const auto& detail = native.detail->mips;
    for (unsigned int n = 0; n < detail.op_count; ++n) {
        const auto& source = detail.operands[n];
        Operand operand;
        if (source.type == MIPS_OP_REG) {
            operand.kind = OperandKind::Register;
            operand.reg = register_name(handle, source.reg);
            operand.width_bits = scalar_register_width(operand.reg, Architecture::Mips, bits);
        } else if (source.type == MIPS_OP_IMM) {
            operand.kind = OperandKind::Immediate;
            operand.immediate = static_cast<quint64>(source.imm);
            operand.width_bits = bits;
        } else if (source.type == MIPS_OP_MEM) {
            operand.kind = OperandKind::Memory;
            operand.memory_base = register_name(handle, source.mem.base);
            operand.displacement = source.mem.disp;
            operand.width_bits = memory_width(out.mnemonic, Architecture::Mips, 0);
        }
        out.operands.push_back(std::move(operand));
    }
}

} // namespace
#endif

InstructionDecoder::InstructionDecoder(Architecture architecture, ByteOrder order, int bits)
    : impl_(std::make_unique<Impl>()) {
    impl_->architecture = architecture;
    impl_->bits = bits;
#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
    int major = 0, minor = 0;
    cs_version(&major, &minor);
    if (major != 5) {
        impl_->reason = QStringLiteral("A biblioteca Capstone carregada não oferece a API 5 esperada.");
        return;
    }
    if (cs_support(CS_SUPPORT_DIET)) {
        impl_->reason = QStringLiteral("Capstone em modo diet não fornece os detalhes exigidos pelo analisador.");
        return;
    }
    cs_arch family;
    unsigned int mode = order == ByteOrder::Big ? CS_MODE_BIG_ENDIAN : CS_MODE_LITTLE_ENDIAN;
    switch (architecture) {
    case Architecture::X86: family = CS_ARCH_X86; mode = CS_MODE_32; break;
    case Architecture::X64: family = CS_ARCH_X86; mode = CS_MODE_64; break;
    case Architecture::Arm: family = CS_ARCH_ARM; mode |= CS_MODE_ARM; break;
    case Architecture::Thumb: family = CS_ARCH_ARM; mode |= CS_MODE_THUMB; break;
    case Architecture::Arm64: family = CS_ARCH_ARM64; break;
    case Architecture::Mips:
        family = CS_ARCH_MIPS;
        if (bits != 32 && bits != 64) {
            impl_->reason = QStringLiteral("A largura MIPS deve ser explicitamente 32 ou 64 bits.");
            return;
        }
        mode |= bits == 64 ? CS_MODE_MIPS64 : CS_MODE_MIPS32;
        break;
    default:
        impl_->reason = QStringLiteral("Arquitetura sem decoder suportado.");
        return;
    }
    if ((architecture == Architecture::X86 || architecture == Architecture::X64) && order == ByteOrder::Big) {
        impl_->reason = QStringLiteral("x86 requer ordem de bytes little-endian.");
        return;
    }
    const cs_err opened = cs_open(family, static_cast<cs_mode>(mode), &impl_->handle);
    if (opened != CS_ERR_OK) {
        impl_->reason = QStringLiteral("Capstone não abriu o modo solicitado: %1.").arg(QString::fromLatin1(cs_strerror(opened)));
        return;
    }
    if (cs_option(impl_->handle, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK) {
        impl_->reason = QStringLiteral("Capstone não habilitou detalhes de operandos.");
        return;
    }
    impl_->scratch = cs_malloc(impl_->handle);
    if (!impl_->scratch) impl_->reason = QStringLiteral("Capstone não alocou o registro de decodificação.");
#else
    (void)order;
    impl_->reason = QStringLiteral("Decoder indisponível: esta compilação não inclui Capstone 5. Metadados, bytes e strings continuam disponíveis.");
#endif
}

InstructionDecoder::~InstructionDecoder() = default;

bool InstructionDecoder::available() const {
#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
    return impl_->handle && impl_->scratch && impl_->reason.isEmpty();
#else
    return false;
#endif
}

QString InstructionDecoder::explanation() const { return impl_->reason; }

std::optional<DecodedInstruction> InstructionDecoder::decode(const QByteArray& bytes, Address address) {
#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
    if (!available() || bytes.isEmpty() || bytes.size() > 16) return std::nullopt;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(bytes.constData());
    size_t remaining = static_cast<size_t>(bytes.size());
    uint64_t pc = address.value;
    if (!cs_disasm_iter(impl_->handle, &data, &remaining, &pc, impl_->scratch)) return std::nullopt;
    const auto& native = *impl_->scratch;
    if (!native.detail || !native.size || native.size > bytes.size()) return std::nullopt;
    DecodedInstruction result;
    auto& out = result.instruction;
    out.address = std::move(address);
    out.bytes = bytes.left(native.size);
    out.mnemonic = QString::fromLatin1(native.mnemonic);
    out.operands_text = QString::fromLatin1(native.op_str);
    out.native_id = native.id;
    auto grouped = [&](unsigned int group) { return cs_insn_group(impl_->handle, &native, group); };
    const bool encoded_control_target = grouped(CS_GRP_CALL) || grouped(CS_GRP_JUMP);
    if (grouped(CS_GRP_RET) || grouped(CS_GRP_IRET)) out.flow = FlowKind::Return;
    else if (grouped(CS_GRP_CALL)) out.flow = FlowKind::Call;
    else if (grouped(CS_GRP_JUMP)) out.flow = FlowKind::Jump;
    else if (grouped(CS_GRP_INT) || is_trap(out.mnemonic)) out.flow = FlowKind::Trap;
    if (out.mnemonic == QStringLiteral("sysret") || out.mnemonic == QStringLiteral("sysretq") || out.mnemonic == QStringLiteral("sysexit") || out.mnemonic == QStringLiteral("rsm") || out.mnemonic == QStringLiteral("eret") || out.mnemonic == QStringLiteral("drps")) out.flow = FlowKind::Return;

    switch (impl_->architecture) {
    case Architecture::X86:
    case Architecture::X64:
        decode_x86(impl_->handle, native, out, impl_->architecture);
        if (out.flow == FlowKind::Jump && native.id != X86_INS_JMP && native.id != X86_INS_LJMP) out.flow = FlowKind::ConditionalJump;
        break;
    case Architecture::Arm:
    case Architecture::Thumb: {
        decode_arm(impl_->handle, native, out, impl_->architecture);
        const auto cc = native.detail->arm.cc;
        const bool conditional = cc != ARM_CC_AL && cc != ARM_CC_INVALID;
        if (out.flow == FlowKind::Jump && (conditional || native.id == ARM_INS_CBZ || native.id == ARM_INS_CBNZ)) out.flow = FlowKind::ConditionalJump;
        if (native.id == ARM_INS_BX && !out.operands.isEmpty() && out.operands[0].reg == QStringLiteral("lr")) out.flow = FlowKind::Return;
        // Some PC-writing instructions are not in Capstone's branch groups.
        cs_regs reads{}, writes{};
        uint8_t read_count = 0, write_count = 0;
        if (out.flow == FlowKind::Normal && cs_regs_access(impl_->handle, &native, reads, &read_count, writes, &write_count) == CS_ERR_OK) {
            for (uint8_t n = 0; n < write_count; ++n) if (writes[n] == ARM_REG_PC) out.flow = conditional ? FlowKind::ConditionalJump : FlowKind::Jump;
        }
        if (native.id == ARM_INS_IT) {
            result.stop_path = true;
            result.warnings << QStringLiteral("Estado Thumb IT não propagado; exploração interrompida antes das instruções condicionadas.");
        }
        if (native.id == ARM_INS_BLX || (native.id == ARM_INS_BX && out.flow != FlowKind::Return)) {
            result.stop_path = true;
            result.target_changes_mode = true;
            result.warnings << QStringLiteral("Interworking ARM/Thumb não propagado por endereço; destino requer análise no modo apropriado.");
        }
        if ((out.flow == FlowKind::Return || out.flow == FlowKind::Trap) && conditional) out.falls_through = true;
        break;
    }
    case Architecture::Arm64: {
        decode_arm64(impl_->handle, native, out);
        const auto cc = native.detail->arm64.cc;
        if (out.flow == FlowKind::Jump && ((native.id == ARM64_INS_B && cc != ARM64_CC_AL && cc != ARM64_CC_INVALID) || native.id == ARM64_INS_CBZ || native.id == ARM64_INS_CBNZ || native.id == ARM64_INS_TBZ || native.id == ARM64_INS_TBNZ)) out.flow = FlowKind::ConditionalJump;
        break;
    }
    case Architecture::Mips:
        decode_mips(impl_->handle, native, out, impl_->bits);
        if (native.id == MIPS_INS_JR && !out.operands.isEmpty() && out.operands[0].reg == QStringLiteral("ra")) out.flow = FlowKind::Return;
        if (out.flow == FlowKind::Jump && native.id != MIPS_INS_J && native.id != MIPS_INS_JR && native.id != MIPS_INS_B) out.flow = FlowKind::ConditionalJump;
        if (out.flow != FlowKind::Normal) {
            result.stop_path = true;
            result.warnings << QStringLiteral("Delay slots/branch-likely MIPS não modelados; caminho interrompido e CFG desta função indisponível.");
        }
        break;
    default: return std::nullopt;
    }

    // Branch immediates are already absolute addresses in Capstone's detail API.
    if (out.flow == FlowKind::Call || out.flow == FlowKind::Jump || out.flow == FlowKind::ConditionalJump) {
        const bool far_x86 = (impl_->architecture == Architecture::X86 || impl_->architecture == Architecture::X64) && (native.id == X86_INS_LJMP || native.id == X86_INS_LCALL);
        // An immediate in ADD/MOV/LDR-to-PC is a data operand, not an
        // already-resolved branch destination. Never turn it into a code xref.
        if (!far_x86 && encoded_control_target) {
            for (qsizetype n = out.operands.size(); n > 0; --n) {
                if (out.operands[n - 1].kind == OperandKind::Immediate) {
                    quint64 target = out.operands[n - 1].immediate;
                    if (impl_->architecture == Architecture::X86 || impl_->architecture == Architecture::Arm || impl_->architecture == Architecture::Thumb || (impl_->architecture == Architecture::Mips && impl_->bits == 32)) target &= 0xffffffffULL;
                    out.direct_target = Address{ out.address.space, target };
                    break;
                }
            }
        } else if (far_x86) {
            result.warnings << QStringLiteral("Transferência segmentada x86 não resolvida como endereço linear.");
        }
        if (!out.direct_target) result.warnings << QStringLiteral("Transferência indireta não resolvida; ponteiros e tabelas de salto não são inferidos.");
    }

    if (out.flow == FlowKind::Trap) result.warnings << QStringLiteral("Sistema/trap interrompe o caminho; retorno de supervisor, exceções e continuação não são modelados.");

    bool conditional_return = false;
    if (impl_->architecture == Architecture::Arm || impl_->architecture == Architecture::Thumb) {
        const auto cc = native.detail->arm.cc;
        conditional_return = cc != ARM_CC_AL && cc != ARM_CC_INVALID;
    }
    if (out.flow == FlowKind::Jump || out.flow == FlowKind::Return || out.flow == FlowKind::Trap) out.falls_through = conditional_return && out.flow != FlowKind::Jump;
    quint64 next = 0;
    if (!add_unsigned(out.address.value, static_cast<quint64>(out.bytes.size()), next)) {
        out.falls_through = false;
        result.stop_path = true;
        result.warnings << QStringLiteral("Próximo endereço excede o domínio de 64 bits.");
    }
    return result;
#else
    (void)bytes;
    (void)address;
    return std::nullopt;
#endif
}

} // namespace detail

QString decoder_description() {
#if defined(REWORKBENCH_HAS_CAPSTONE) && REWORKBENCH_HAS_CAPSTONE
    int major = 0, minor = 0;
    cs_version(&major, &minor);
    return QStringLiteral("Capstone %1.%2 (somente instruções); descoberta, CFG, referências e IR próprios.").arg(major).arg(minor);
#else
    return QStringLiteral("Sem decoder de instruções (Capstone 5 opcional); metadados, bytes, entropia e strings disponíveis.");
#endif
}

} // namespace reworkbench
