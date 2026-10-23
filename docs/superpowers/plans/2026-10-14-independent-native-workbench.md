# Independent Native Workbench Implementation Plan

> **For agentic workers:** contracts are shared below; implementation proceeds within the user's existing coding authorization. Test/build execution is explicitly suspended by the user.

**Goal:** deliver the source of an independent C++20/Qt workbench whose default launch path has no Ghidra/JVM/Python engine dependency.

**Architecture:** native immutable binary snapshots, own format loaders and analysis/IR, optional Capstone instruction decoder, Qt desktop, project SQLite persistence. Independent files are delegated against fixed public headers.

**Tech Stack:** C++20, Qt6 Core/Widgets/Concurrent/Sql, optional Capstone5, existing own bytecore library, CMake.

**Spec:** docs/superpowers/specs/2026-10-14-independent-native-workbench-design.md.

## Global constraints

- No tests, compilation, CMake configuration, product execution, dependency installation or downloads in this phase.
- No Ghidra/JVM/PyGhidra calls in native sources or default launchers.
- Preserve original samples and legacy projects; native manifest is `project.rwb.json`.
- File reads are bounded and checked; default hex range64KiB, strings10000, preview4096, instructions200000 total/4096 per function, functions10000, analysis300s.
- Native IR is explicitly partial; unknown semantics remain opaque. No fake pseudocode.

## Review focus

- Truncated/malicious tables and arithmetic overflow must not escape file bounds.
- Unbacked virtual memory and image slices must not map to fabricated file offsets.
- Late selection/project results must not populate the current view incorrectly.
- Cancellation and failed persistence must not discard user notes or replace intact exports.
- Decoder absence and unsupported semantics must remain visible rather than appear successful.

### Task 1: Native contracts, own loaders and source access

- [x] Define types and bounded BinaryFile access in native/workbench/include/reworkbench.
- [x] Implement own PE/ELF/Mach-O/raw parsing, inspect choices and address mapping.
- [x] Static review arithmetic and source-only status; dynamic acceptance deferred.

### Task 2: Native analysis, decoder and IR

- [x] Implement optional Capstone5 decoder, own function/control-flow/reference exploration and direct bytecore integration.
- [x] Generate own IR with supported/opaque coverage and explicit capability limits.
- [x] Static review branch targets, bounds, cancellation and delay-slot limitations; dynamic acceptance deferred.

### Task 3: Independent native projects

- [x] Implement project manifest/lock, atomic sample copying+hash, SQLite snapshots, note/name persistence and protected atomic export.
- [x] Preserve unsupported/future projects; reject legacy manifests explicitly.
- [x] Static review transactions and connection lifetime; dynamic acceptance deferred.

### Task 4: C++ Qt workspace and default migration

- [x] Implement own paginated explorers, assembly/IR/CFG/hex views, project/import workflows, background tasks, history, rename, notes and exports.
- [x] Switch default scripts/package launcher to native executable; retain old entry only as explicit legacy source.
- [x] Add root CMake and explain native prerequisites and unimplemented semantic work.

### Task 5: Source-only integration review

- [x] Resolve interface mismatches and static-review material findings.
- [x] Document exact source delivery, old prototype boundary and pending runtime acceptance; commit without running hooks/builds/tests.

Source delivery only. An independent source reviewer reported three important findings (sample hash verification, ARM/Thumb seed modes, SQLite annotation allocation budgets); those were corrected, with a follow-up source review reporting no new material finding. Additional source fixes retain the true CFG entry, immediately clear old rows/hex, and prepare bounded audit results in the background. Compilation, test and runtime acceptance remain deferred under the user's explicit instruction.
