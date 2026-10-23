# Independent typed decompiler

The C++ decompiler is implemented in `decompiler.cpp`, `decompiler_lift.cpp` and
`decompiler_internal.hpp`, with public API in `reworkbench/decompiler.hpp`.
It does not invoke Ghidra, Python, a remote service or an external decompiler.
It consumes an existing immutable native `AnalysisSnapshot`; decoding and sample
execution are not part of this API. Current build and runtime coverage is tracked
in [validation.md](../../../docs/validation.md); it does not establish general semantic equivalence.

```cpp
auto result = reworkbench::decompile_function(snapshot, function_index);
// result.pseudocode, result.ssa_text, result.warnings
```

`pseudocode_addresses` and `ssa_addresses` map zero-based output lines to exact
original instruction addresses. `RenderedFunction::source_addresses` provides the
same metadata for the low-level IR. Instruction comments, attributable definitions
and explicit instruction control keep their origin; synthetic declarations,
inputs, phi transfers and ambiguous merged control do not acquire a guessed
address. Output ceilings discard mappings for cut lines and truncation markers.
This metadata supports line navigation, not per-token variable recovery. The
default decompilation cache uses format/key v2 to preserve and validate it; older
entries are recomputed without a project migration or semantics-version change.

An invalid function index raises `std::out_of_range`. Cancellation raises the
existing `Cancelled` exception. Unsupported architectures, invalid/overlapping
instruction layouts and resource ceilings produce an explicitly opaque bounded
listing with `complete == false`, never fabricated typed source.

## Implemented pipeline

1. Validate nonempty instruction spans, address-space identity, overflow,
   overlapping bytes, uniqueness and the actual entry instruction. Rebuild a CFG
   from decoded control flow and exact addresses. Exclude unreachable recovered
   instructions with an explicit warning. Branch targets outside the recovered
   body are visible transfers. Indirect jump-table candidates remain unresolved;
   no switch expression or condition is inferred from the first target.
2. Lift supported scalar x86/x64 instructions into typed fixed-width bitvectors,
   an explicit memory token and architectural state. Track full canonical GPRs
   rather than unrelated aliases. An AL/AH/AX write inserts bits into its parent;
   an EAX write in x64 clears the upper half. The same rule applies to other
   general registers and R8-R15. Every instruction retains its original address.
3. Place SSA phi functions at every reachable block input and link all predecessor
   outputs, including backedges. The function entry also has an external input.
   Remove only trivial phis; differing definitions at a join remain distinct.
4. Run a bounded monotone constant lattice with bottom/constant/varying values.
   Joins do not choose an arbitrary predecessor. Cycles use iterative propagation;
   if it does not converge within the budget, all provisional propagated constants
   are discarded. Memory loads are never folded across unknown aliases or stores.
5. Render typed SSA and machine-state pseudocode. Destruction of phi functions
   evaluates every incoming RHS into a temporary before changing any destination,
   preserving parallel-copy semantics on loop edges. All locals are declared
   before labels. A proven one-block conditional loop becomes `for (;;)`, and
   an isolated two-arm diamond becomes `if/else`; general control flow retains
   labels and transfers. Source comments retain the assembly and address.
   A branch following CMP can use a direct signed/unsigned comparison only when
   the current CF/ZF/SF/OF definitions still match that exact CMP. Intervening
   flag writers and opaque effects prevent that simplification; signed views
   describe two's-complement machine values, not recovered source declarations.

`machine_state function(machine_state in)` is the **synthetic analysis model**,
not an inferred function signature. Integer widths describe machine values, not
recovered user typedefs. `load_le_uN`, `store_le_uN`, wrapping arithmetic,
`machine_return`, `transfer_to`, `undefined_u1` and `opaque_instruction` are
explicit analysis intrinsics; the listing is not a standalone C translation unit.

## Instruction coverage

The supported model is scalar x86 in 32-bit mode and scalar x64 in 64-bit mode,
under flat little-endian memory and normal non-faulting execution. Register,
immediate and ordinary memory operands are checked for usable scalar widths.

| Instruction family | Modeled effects |
| --- | --- |
| `mov`, `movabs`, `movzx`, `movsx`, `movsxd` | Exact scalar data width and register extension/insertion; ordinary memory loads/stores. |
| `lea` | Effective-address arithmetic, scale, displacement, RIP/EIP-relative addressing, address-size truncation and destination extension/insertion. |
| `add`, `sub`, `cmp` | Modular result and CF/PF/AF/ZF/SF/OF bitvector formulas. |
| `and`, `or`, `xor`, `test` | Modular result, ZF/SF/PF, CF/OF cleared, AF explicitly undefined. |
| `inc`, `dec`, `neg`, `not` | Scalar result; INC/DEC preserve CF; other applicable arithmetic flags modeled. |
| Common `jcc`, `jcxz`, `jecxz`, `jrcxz` | Conditions use the current flag/count SSA values; unknown incoming flags stay explicit. |
| `setcc`, `cmovcc` | Known condition, scalar value selection and destination register rules; memory reads retain their dependency. |
| `jmp` with direct target | Resolved transfer, with out-of-body targets explicit. |
| `push`, register `pop`, `leave` | Native stack width, explicit memory token and stack arithmetic; POP RSP/ESP ordering is preserved. |
| Near `ret`, `ret imm16` | Explicit return-target load and stack adjustment. No source return value or ABI is guessed. |
| `nop` | No data-state effect. |

LOCK/REP prefixes, segment overrides/hints, non-scalar operands, unsupported stack
width overrides, SIMD/FPU, shifts/rotates, multiply/divide instructions, atomics,
system instructions and other missing operations remain opaque. Calls are opaque
because calling conventions and callee effects are unknown: **all** GPRs, tracked
flags, memory and other architectural state are replaced by projections of one
explicit opaque state transition. A previously known constant therefore cannot
incorrectly survive a call or an unsupported instruction. An undefined branch
outcome is materialized once and reused, rather than resampled during rendering.

Default segment bases, privilege changes, traps, exceptions, floating-point state,
memory-mapped I/O, volatile accesses and fault order are outside this model. The
remaining architectural token preserves unmodeled state on supported instructions;
instruction-pointer/control changes are represented by CFG edges and transfers.

## Limits and result meanings

| Resource | Default | Hard maximum |
| --- | ---: | ---: |
| Instructions per function | 4096 | 4096 |
| Basic blocks | 4096 | 4096 |
| Typed IR nodes | 196608 | 262144 |
| Dataflow iterations | 96 | 256 |
| Characters per listing (pseudocode / SSA) | 2097152 | 4194304 |

Zero is clamped to a bounded minimum; it never requests unlimited work. All main
loops check cancellation, including lifting, reachability, aliases, propagation,
dependency marking and rendering. Output truncation is explicit and invalidates
`complete`. Assembly/name/comment previews are bounded. Phi maps and work lists
are bounded by the block/node ceilings rather than uncontrolled recursion.

`modeled_instructions / total_instructions` counts the documented semantic subset
against the recovered function. `reachable_blocks` and `ir_nodes` report work
actually represented. `dataflow_converged` concerns constant propagation only;
it is false when that pass is disabled. `complete` means all recovered instructions
are represented within this narrow model and the recovered function/output are
not partial. It is not a proof of equivalence, original-source recovery, complete
architecture support or industrial decompiler parity.

## Remaining semantic work

Source-level variable/stack-object recovery, ABI and parameter inference,
structures/unions, PDB/DWARF type application, interprocedural effects, exceptions,
full x86 instruction semantics, other architecture lifters, indirect switch
recovery and general loop/region structuring remain future work. This source
implements the typed SSA/dataflow foundation rather than labeling those absent
features as supported.

Dynamic acceptance is deferred by the user's no-test/no-build instruction.
Future controlled validation must check partial/high registers, arithmetic flags,
zero-extension, address-size wrapping, POP RSP, RET adjustments, joins, loop phi
copies, opaque invalidation, aliasing stores, cancellation, resource ceilings and
malformed snapshots against independently specified machine behavior.
