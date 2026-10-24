#!/usr/bin/env python3
"""Read-only source integration inventory for Rework native workbench.

Prepared in stage 020. NOT EXECUTED as part of that stage. This scanner
checks file wiring and source references only, not syntax, linking, build,
runtime correctness or Ghidra equivalence.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

INCREMENT_SOURCES = (
    "src/ir_semantic_audit.cpp",
    "src/type_evidence_analysis.cpp",
    "src/hir_analysis.cpp",
    "src/control_region_analysis.cpp",
    "src/structuring_plan.cpp",
    "src/phi_transfers.cpp",
    "src/parallel_copy_plan.cpp",
    "src/edge_effect_analysis.cpp",
    "src/control_safety_analysis.cpp",
)
COMMANDS = (
    "abi-observe",
    "type-observe",
    "hir-observe",
    "ir-audit",
    "phi-transfers",
    "phi-copy-plan",
    "edge-effects",
    "control-safety",
    "cfg-structure",
    "structuring-plan",
)
LOCAL_INCLUDE = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)
CMAKE_SOURCE = re.compile(r"(?<![\w/])(?:src|ui)/[\w./-]+\.(?:cpp|qrc)")
# Ignore CMake expansions; handle the explicit workbench target only.


def inventory(root: Path) -> dict:
    native = root / "native" / "workbench"
    cmake_file = native / "CMakeLists.txt"
    cli_file = native / "src" / "cli_main.cpp"
    errors: list[str] = []
    if not cmake_file.is_file() or not cli_file.is_file():
        return {
            "scope": "source_wiring_only",
            "pass": False,
            "errors": ["CMakeLists.txt or cli_main.cpp is missing"],
        }

    cmake = cmake_file.read_text(encoding="utf-8")
    cli = cli_file.read_text(encoding="utf-8")
    listed = sorted(set(CMAKE_SOURCE.findall(cmake)))
    missing_listed = [part for part in listed if not (native / part).is_file()]
    for source in missing_listed:
        errors.append("missing CMake referenced file: " + source)

    missing_increment = []
    missing_includes = []
    for rel in INCREMENT_SOURCES:
        if rel not in listed:
            missing_increment.append(rel)
            errors.append("increment source not listed in CMake: " + rel)
        source = native / rel
        if not source.is_file():
            if rel not in missing_listed:
                errors.append("increment source missing: " + rel)
            continue
        text = source.read_text(encoding="utf-8")
        for inc in LOCAL_INCLUDE.findall(text):
            if not ((source.parent / inc).is_file()
                    or (native / "include" / inc).is_file()):
                missing_includes.append(f"{rel}: {inc}")
                errors.append(f"unresolved quoted include in {rel}: {inc}")

    missing_commands = []
    for cmd in COMMANDS:
        # Presence in the two command registries and the handler branch
        # is a simple wiring clue, not proof that the command works.
        if cli.count('"' + cmd + '"') < 2:
            missing_commands.append(cmd)
            errors.append("insufficient CLI mentions: " + cmd)

    return {
        "scope": "source_wiring_only",
        "executed_build_or_tests": False,
        "source_root": str(root),
        "cmake_paths_inspected": len(listed),
        "cmake_missing_files": missing_listed,
        "required_increment_sources_missing_from_cmake": missing_increment,
        "unresolved_local_includes": missing_includes,
        "commands_with_missing_wiring_evidence": missing_commands,
        "static_inventory_pass": not errors,
        "errors": errors,
        "warning": (
            "This is a textual inventory. It cannot prove that C++ compiles, "
            "that generated includes exist, or that the decompiler is correct."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    result = inventory(args.root.resolve())
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("static_inventory_pass", False) else 1


if __name__ == "__main__":
    raise SystemExit(main())
