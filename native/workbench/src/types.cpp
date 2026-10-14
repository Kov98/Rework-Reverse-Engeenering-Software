#include "reworkbench/types.hpp"

namespace reworkbench {
QString architecture_name(Architecture architecture) {
    switch (architecture) {
    case Architecture::X86: return QStringLiteral("x86");
    case Architecture::X64: return QStringLiteral("x86-64");
    case Architecture::Arm: return QStringLiteral("ARM");
    case Architecture::Thumb: return QStringLiteral("Thumb");
    case Architecture::Arm64: return QStringLiteral("AArch64");
    case Architecture::Mips: return QStringLiteral("MIPS");
    default: return QStringLiteral("Desconhecida");
    }
}
QString format_name(BinaryFormat format) {
    switch (format) {
    case BinaryFormat::Raw: return QStringLiteral("Firmware bruto");
    case BinaryFormat::Pe: return QStringLiteral("PE");
    case BinaryFormat::Elf: return QStringLiteral("ELF");
    case BinaryFormat::MachO: return QStringLiteral("Mach-O");
    case BinaryFormat::FatMachO: return QStringLiteral("Mach-O universal");
    default: return QStringLiteral("Desconhecido");
    }
}
Architecture architecture_from_name(const QString& name) {
    for (auto value : {Architecture::X86, Architecture::X64, Architecture::Arm, Architecture::Thumb, Architecture::Arm64, Architecture::Mips})
        if (architecture_name(value).compare(name, Qt::CaseInsensitive) == 0) return value;
    return Architecture::Unknown;
}
} // namespace reworkbench
