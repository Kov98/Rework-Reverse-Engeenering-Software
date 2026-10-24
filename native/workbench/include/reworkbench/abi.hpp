#pragma once

#include "reworkbench/types.hpp"

namespace reworkbench {

// A calling-convention *description*, not a recovered function signature.
// These profiles apply to ordinary userspace C/C++ calls. In particular they
// must not be applied blindly to syscalls, interrupts, naked functions, or
// custom per-function calling conventions.
struct AbiProfile {
    QString id;
    QString name;
    QStringList integer_argument_registers;
    QStringList vector_argument_registers;
    QStringList caller_saved_gprs;
    QStringList callee_saved_gprs;
    QStringList callee_saved_vector_registers;
    QStringList integer_return_registers;
    QStringList vector_return_registers;
    quint32 stack_alignment_bytes = 16;
    quint32 stack_argument_start_from_entry_rsp = 0;
    quint32 shadow_space_bytes = 0;
    quint32 red_zone_bytes = 0;
    bool positional_vector_arguments = false;
};

// Explicit profiles are normative *only if* this ABI actually applies.
// A format-based hint is not proof that any particular function follows it.
AbiProfile windows_x64_abi();
AbiProfile system_v_amd64_abi();
std::optional<AbiProfile> suggest_abi(const BinaryMetadata& metadata);
std::optional<AbiProfile> abi_profile_by_id(const QString& id);

} // namespace reworkbench
