#include "reworkbench/abi.hpp"

namespace reworkbench {

AbiProfile windows_x64_abi() {
    AbiProfile profile;
    profile.id = QStringLiteral("windows-x64");
    profile.name = QStringLiteral("Microsoft x64 (ordinary user-mode calls)");
    profile.integer_argument_registers = { "rcx", "rdx", "r8", "r9" };
    profile.vector_argument_registers = { "xmm0", "xmm1", "xmm2", "xmm3" };
    profile.caller_saved_gprs = { "rax", "rcx", "rdx", "r8", "r9", "r10", "r11" };
    profile.callee_saved_gprs = { "rbx", "rbp", "rsi", "rdi", "rsp", "r12", "r13", "r14", "r15" };
    profile.callee_saved_vector_registers = {
        "xmm6", "xmm7", "xmm8", "xmm9", "xmm10",
        "xmm11", "xmm12", "xmm13", "xmm14", "xmm15"
    };
    profile.integer_return_registers = { "rax" };
    profile.vector_return_registers = { "xmm0" };
    profile.stack_argument_start_from_entry_rsp = 40; // ret(8) + home area(32)
    profile.shadow_space_bytes = 32;
    profile.positional_vector_arguments = true; // XMMn follows argument *position*
    return profile;
}

AbiProfile system_v_amd64_abi() {
    AbiProfile profile;
    profile.id = QStringLiteral("sysv-amd64");
    profile.name = QStringLiteral("System V AMD64 (ordinary user-mode calls)");
    profile.integer_argument_registers = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    profile.vector_argument_registers = {
        "xmm0", "xmm1", "xmm2", "xmm3",
        "xmm4", "xmm5", "xmm6", "xmm7"
    };
    profile.caller_saved_gprs = { "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11" };
    profile.callee_saved_gprs = { "rbx", "rbp", "rsp", "r12", "r13", "r14", "r15" };
    profile.integer_return_registers = { "rax", "rdx" }; // possible multi-eightbyte return
    profile.vector_return_registers = { "xmm0", "xmm1" };
    profile.stack_argument_start_from_entry_rsp = 8;
    profile.red_zone_bytes = 128; // userspace; not the Linux kernel ABI
    profile.positional_vector_arguments = false; // separate INTEGER/SSE classes
    return profile;
}

std::optional<AbiProfile> abi_profile_by_id(const QString& id) {
    if (id == QStringLiteral("windows-x64")) return windows_x64_abi();
    if (id == QStringLiteral("sysv-amd64")) return system_v_amd64_abi();
    return std::nullopt;
}

std::optional<AbiProfile> suggest_abi(const BinaryMetadata& metadata) {
    if (metadata.architecture != Architecture::X64 || metadata.bits != 64)
        return std::nullopt;
    if (metadata.format == BinaryFormat::Pe) return windows_x64_abi();
    if (metadata.format == BinaryFormat::Elf) return system_v_amd64_abi();
    // Mach-O, raw images, mixed ABI targets and other formats are deliberately
    // left unknown until OS/platform and per-function evidence are available.
    return std::nullopt;
}

} // namespace reworkbench
