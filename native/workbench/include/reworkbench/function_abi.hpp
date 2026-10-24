#pragma once

#include "reworkbench/abi.hpp"
#include "reworkbench/call_effects.hpp"
#include "reworkbench/stack_frame.hpp"
#include "reworkbench/stack_value_flow.hpp"
#include "reworkbench/memory_dependence.hpp"
#include "reworkbench/memory_ssa.hpp"
#include "reworkbench/memory_regions.hpp"
#include "reworkbench/type_evidence.hpp"

namespace reworkbench {

// Advisory facts derived from the currently modeled machine-state IR.
// These are *not* source declarations, ABI proof, or recovered argument counts.
struct AbiRegisterProvenance {
    QString register_name;
    QVector<Address> possible_producers;
};

// A possible read/write of a modeled stack slot, not a source argument.
// incoming: offset relative to callee entry RSP; outgoing: caller pre-CALL RSP.
struct AbiStackAccess {
    Address instruction;
    qint64 stack_offset_bytes = 0;
    int width_bits = 0;
};

struct AbiCallObservation {
    Address instruction;
    std::optional<Address> target;
    QString recovered_target_name;
    bool target_in_snapshot = false;
    QVector<AbiRegisterProvenance> possible_argument_producers;
    QVector<AbiStackAccess> possible_outgoing_stack_stores;
    bool stack_observation_limited = false;
    bool stack_memory_path_unknown = false;
    CallEffectContract effects;
    bool effects_unknown = true;
};

struct FunctionAbiObservation {
    QString status = QStringLiteral("unknown");
    QString profile_id;
    QStringList observed_integer_input_dependencies;
    QStringList possible_integer_return_writes;
    QVector<AbiStackAccess> possible_incoming_stack_reads;
    bool incoming_stack_observation_limited = false;
    StackFrameObservation frame;
    StackValueFlowObservation value_flow;
    MemoryDependenceObservation memory_dependence;
    MemorySSAObservation memory_ssa;
    MemoryRegionObservation memory_regions;
    TypeEvidenceObservation type_evidence;
    QVector<AbiCallObservation> calls;
    quint32 modeled_instructions = 0;
    quint32 total_instructions = 0;
    bool partial = true;
    QStringList warnings;
};

// Separate from the pseudocode renderer and caching. It never applies ABI
// assumptions to the decompiler's state transitions or removes opaque effects.
// Implemented for x86-64 with a supported file-format ABI *hint* only.
FunctionAbiObservation observe_function_abi(const AnalysisSnapshot& snapshot,
    qsizetype function_index,
    const std::optional<AbiProfile>& explicit_profile = std::nullopt,
    std::stop_token stop = {});

} // namespace reworkbench
