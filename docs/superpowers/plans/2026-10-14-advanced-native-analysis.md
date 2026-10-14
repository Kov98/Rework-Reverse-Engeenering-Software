# Advanced Native Analysis Implementation Plan

> Execution uses the already authorized isolated worktree and parallel independent modules. The user's source-only restriction overrides test/build steps and approval loops.

**Goal:** implement deeper independent native analysis and accessible investigation tools, documenting semantic coverage and remaining work accurately.

**Architecture:** retain existing immutable snapshots and Qt/SQLite desktop. Add independent decompiler, enrichment and investigation modules against explicit headers; integrate through a native CLI and desktop actions.

**Tech Stack:** C++20, Qt6 Core/Widgets/Concurrent/Sql, optional installed Capstone5; native Windows APIs for Windows debugging.

**Spec:** docs/superpowers/specs/2026-10-14-advanced-native-analysis-design.md.

## Global constraints

- No tests, builds, CMake configuration, execution/import of product code, installs or downloads.
- Keep Ghidra/JVM/Python out of the native product path.
- Existing caps: 200000 instructions, 4096/function, 10000 functions, 300s, strings10000, previews4096.
- Do not erase legacy projects or source; do not merge or publish.
- Unsupported semantics and capabilities stay explicit; source review is not runtime acceptance.

## Review focus

- Unknown instructions/calls must invalidate dataflow instead of carrying false constants/types.
- Phi/control-flow joins, aliasing and partial registers must not invent equivalent pseudocode.
- Enrichment and indirect targets must not turn unbacked/writable data or import placeholders into functions.
- Patch/export/cache paths must preserve originals, reject traversal/aliases and survive cancellation.
- Debugger/extension lifetimes and stale background results must not leak processes or populate the wrong view.

### Task 1: Decompiler pipeline
- [x] Add public result/options, typed IR, SSA/dataflow and bounded readable pseudocode.
- [x] Preserve opaque fallbacks, addresses and coverage; document exact modeled subset.

### Task 2: Binary enrichment
- [x] Add bounded PE/ELF metadata/symbol/relocation enrichment with provenance and capability limits.
- [x] Add offline symbol import; expose details and use valid seeds/references in analysis.

### Task 3: Investigation tools
- [x] Implement bounded comparisons, verified patch exports and conservative emulation.
- [x] Implement explicit Windows debugger and opt-in native plugin APIs.

### Task 4: Product integration and scale
- [x] Integrate enriched analysis, indirect immutable target candidates, desktop decompiler and tool actions.
- [x] Add native headless automation, content-keyed decompilation cache and compatible-analysis reuse workflows.
- [x] Connect every delivered subsystem to a documented callable workflow; list remaining semantic work.

### Task 5: Source review and delivery
- [ ] Independent whole-change source review (attempted; subagent service usage limit prevented a completed review).
- [x] Review integration and bounds by source reading; correct findings without executing code.
- [x] Update documentation/validation and commit source with hooks disabled.

## Execution ledger

Ruling: reuse feat/reworkbench and .worktrees/implementation — existing isolated workspace is clean at 9e51f3d; no second worktree needed.

Ruling: implement against source-only acceptance — the user has not revoked the prohibition on tests/builds/execution, so no runtime claim is possible.

Task 1: source written with implementer review; typed scalar x86/x64 subset and remaining semantic work documented in native/workbench/docs/decompiler.md.

Task 2: source written; SHA supplied before symbol-map identity validation; merged seed cap10000 and serializer-compatible metadata bounds retained. Full PDB types/DWARF5 are absent, not claimed.

Task 3: source written; source-path replacement, original/protected aliases and cancellation checked in patch/comparison code; debugger thread ownership preserved by desktop controller. No target, interpreter or plugin was executed.

Task 4: source integrated with CMake/desktop/CLI, content cache, project reuse and annotation inheritance. Ruling: local-path indirect values are candidate edges only — do not promote them to direct operands without predecessor-independent proof; cost is less inferred high-level control flow and explicit partial status.

Task 5: implementer/root source review only. Independent whole-change reviewer failed on service usage limit without returning findings. Build/test/dynamic acceptance remain pending, as required. No superiority claim or benchmark result was produced.
