# ABI profiles (first decompiler increment, 2026-10-23)

This increment adds **non-authoritative ABI candidates** for x86-64 PE (Microsoft x64)
and x86-64 ELF (System V AMD64), exposed by `reworkbench/abi.hpp` and via the CLI
`inspect` and `decompile` responses as `abi_hint`.

The profile records register argument order (integer and vector), caller/callee
preservation of the tracked GPRs, return-register candidates, vector-register
preservation, stack alignment, shadow space, first stack-argument offset from
the callee's *entry* RSP, and the userspace SysV red zone.

This is **not** recovered source: a PE or ELF header does not prove a function
conforms to any specific convention. Custom `__vectorcall`, syscalls,
interrupt handlers, mixed-ABI calls, thunks, compiler intrinsics and functions
with incomplete byte ranges need more evidence. The output sets
`verified_for_function: false` by design. The unknown state remains for
raw images, Mach-O and unsupported architectures. The current decompiler
continues to treat unknown calls as opaque and does **not** yet infer
arguments or return types from these candidates.

Integration:
- `native/workbench/src/abi.cpp`: descriptions and conservative format-based selection.
- `native/workbench/src/cli_main.cpp`: serializes the candidate for inspect/decompile.
- `tests/native/test_cli.py`: PE64, generated ELF64 metadata, unknown/mismatched profiles.
- `native/workbench/CMakeLists.txt`: includes the new source in the native engine.

Next: create a verified x86-64 call-effects pass on a controlled corpus,
distinguish ABI hypothesis from observed effects per function, then add
stack-frame and candidate parameter/return recovery to HIR. Only optimize
callee-saved values across calls with validated ABI and appropriately
conservative memory/exception state.

References:
- https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention
- https://gitlab.com/x86-psABIs/x86-64-ABI
