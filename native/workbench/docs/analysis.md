# Native analysis and partial IR

The workbench owns function discovery, block construction, references and its
low-level IR. Capstone is an optional instruction decoder. No Ghidra, JVM,
PyGhidra, SLEIGH engine, external disassembler process or bytecore executable is
started by this code. Existing libraries must be provided by the build owner;
these sources do not download dependencies.

## Decoder boundary

`decoder.cpp` uses the Capstone 5 C API with detail mode and a reusable instruction
record. Both the header API major and the loaded library major are checked. Diet
builds and unavailable architecture modes are reported as unavailable. Without
Capstone, loading, bytes, entropy and strings remain available and the analysis
snapshot is explicitly partial; no assembly or functions are fabricated.

Supported fixed image modes are x86-32, x86-64, ARM, Thumb, AArch64 and MIPS32/64,
subject to the architectures enabled in the installed Capstone library. Operands
include scalar register widths where the decoder supplies or identifies them,
immediates and memory components. Width zero means unknown, rather than a guessed
access width. This initial operand contract does not represent every vector
arrangement, extension, shift, addressing side effect or implicit operand. Assembly
text remains attached to every instruction.

Direct control destinations come from actual branch/call immediates. The engine
does not mistake an ADD/MOV-to-PC data immediate for a resolved branch address.
RIP/EIP-relative and unsegmented absolute x86 memory operands produce data/slot
references when resolvable; a call through a slot is not resolved by dereferencing
that slot. ARM PC-relative literals and AArch64 literal loads can also produce data
references. Unresolved indirect branches/calls and segmented far transfers remain
visible limitations.

ARM/Thumb currently has one mode per image. Thumb entry tag bits are removed for
byte addressing. BLX/BX interworking and Thumb IT state terminate the affected
path with a warning. MIPS control transfers terminate exploration before the delay
slot; delay slots and branch-likely are not modeled. A MIPS function containing
such a transfer receives no CFG, so an incomplete delay-slot graph cannot be
mistaken for an architectural CFG. System/trap instructions stop the path with an
explicit warning; supervisor return and exception continuations are not inferred.

API references consulted for integration, without incorporating upstream engine
implementation:

- [Capstone 5.0.6 C API](https://github.com/capstone-engine/capstone/blob/5.0.6/include/capstone/capstone.h)
- [x86 operand definitions](https://github.com/capstone-engine/capstone/blob/5.0.6/include/capstone/x86.h)
- [ARM operand definitions](https://github.com/capstone-engine/capstone/blob/5.0.6/include/capstone/arm.h)
- [AArch64 operand definitions](https://github.com/capstone-engine/capstone/blob/5.0.6/include/capstone/arm64.h)
- [MIPS operand definitions](https://github.com/capstone-engine/capstone/blob/5.0.6/include/capstone/mips.h)

## Discovery, bounds and partial results

Seeds are explicit image/user entries, function symbols and reachable direct call
destinations. There is no linear search for invented function starts. Within a
candidate function, exploration follows backed executable fallthrough and direct
branch paths. Known function seeds bound traversal into another candidate.
Overlapping decoded instructions, branch destinations inside instructions,
unbacked virtual bytes and ambiguous executable mappings stop the affected path.
Shared tails and exact function boundaries are not recovered. Function names from
symbols are hints, not evidence that all reachable bytes belong to that function.

The ceilings are 200,000 decoded instructions globally, 4,096 per function,
10,000 candidate functions, 10,000 strings and 300 seconds. Callers may lower these
limits. A decode read is at most 16 bytes and stays inside one backed executable
mapping and the file. Zero limits do not disable a safety ceiling. Address
arithmetic checks overflow; virtual addresses are never used as file offsets.
Blocks are built from recovered instruction boundaries and include only internal
successors. Cross-function transfers and data references remain in the reference
list. Indirect targets, exception edges, jump tables, stack analysis, unwinding,
calling conventions and dynamic code are not recovered.

The existing `bytecore_cpp` library is called directly for streaming entropy and
maximal printable ASCII strings; its previews are converted with Latin-1 rather
than a locale-dependent encoding. One operation stop source combines caller
cancellation and a steady-clock deadline, including the loader and bytecore scan.
Cancellation/timeout preserves completed metadata, completed scan aggregates and
already decoded functions. An interrupted entropy scan does not publish its
incomplete aggregates. Progress callbacks run on the analysis worker and must be
marshaled before touching widgets. The UI should analyze the project's immutable
sample copy; the analysis notices size changes, but does not claim to detect
in-place same-size source mutations during concurrent external writes.

## IR contract

The output is the workbench's textual, low-level IR, not recovered source code.
Each statement retains its instruction address and assembly. Values are modular
unsigned bit-vectors; register names denote architectural register slices. The
header states the 32-bit GPR zero-extension rules for x86-64/AArch64. The model
covers normal nonfaulting scalar execution and assumes flat x86 segments.
`load_leN` and `store_leN` are used only for modeled x86 memory operations.

The initially modeled subset is conservative: scalar x86 MOV/MOVABS and LEA,
NOP, direct near JMP, supported x86 flag-based conditional branches, and simple
unshifted AArch64 GPR MOV/ADD/SUB/AND/ORR/EOR. Segment overrides, vectors and
modified AArch64 operands do not enter these paths. AArch64 zero-register reads
and discarded writes are explicit. x86 arithmetic/CMP/TEST can display recognized
data computations, followed by opaque flag effects; those instructions do not
count as fully modeled. Calls, returns, other instructions and unsupported
architectural effects are explicitly opaque. No stack or calling-convention
semantics are invented.

`modeled_instructions` counts only instructions completely represented within
this limited abstraction. `complete` also requires a nonempty recovered function,
every instruction modeled and no partial discovery. It does not assert hardware
equivalence, complete exception/concurrency semantics, recovered types, SSA,
structured high-level control flow or a complete decompiler.

## Delivery status

This change is source-only. No tests, compiler, CMake configuration, product
execution, package installation or dependency download was performed for this
implementation, as explicitly requested. Static source inspection is not runtime
validation; decoding, CFGs, ABI/library compatibility and IR still require a later
authorized validation phase.
