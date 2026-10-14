"""Build tiny self-authored fixtures; never execute the resulting binaries."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path


def elf_text(path: Path) -> bytes:
    """Extract relocation-free .text from our assembly-only ELF fixtures."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError("Expected an ELF object")
    endian = "<" if data[5] == 1 else ">"
    is64 = data[4] == 2
    offset = struct.unpack_from(endian + ("Q" if is64 else "I"), data, 40 if is64 else 32)[0]
    stride, count, names_index = struct.unpack_from(endian + "HHH", data, 58 if is64 else 46)

    def section(index):
        at = offset + stride * index
        name = struct.unpack_from(endian + "I", data, at)[0]
        start, size = struct.unpack_from(endian + ("QQ" if is64 else "II"), data, at + (24 if is64 else 16))
        return name, data[start:start + size]

    names = section(names_index)[1]
    found = None
    for index in range(count):
        name_at, content = section(index)
        name = names[name_at:names.index(b"\0", name_at)].decode("ascii")
        if name in (".rel.text", ".rela.text") and content:
            raise ValueError("Raw fixture contains unresolved text relocations")
        if name == ".text":
            found = content
    if not found:
        raise ValueError("ELF fixture has no .text")
    return found


def build_fixtures(output: Path, llvm_bin: Path) -> dict:
    output = output.resolve()
    source = output / "src"
    binaries = output / "bin"
    work = output / "build"
    binaries.mkdir(exist_ok=True)
    work.mkdir(exist_ok=True)
    zig = llvm_bin if llvm_bin.is_file() else None
    if zig is None:
        raise ValueError("Supply the portable zig.exe (bundled Clang/LLD) as --toolchain")
    environment = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(work / "zig-cache"))
    history = []

    def run(*args):
        command = [str(zig), *map(str, args)]
        result = subprocess.run(command, cwd=work, env=environment, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        history.append(command)
        (work / "commands.log").open("a", encoding="utf-8").write(json.dumps(command) + "\n" + result.stdout + result.stderr + "\n")
        if result.returncode:
            raise RuntimeError(f"Command failed: {command}\n{result.stderr}")
        return result.stdout.strip()

    version = run("version")
    fixtures = []

    def record(identifier, file, format_name, **metadata):
        content = file.read_bytes()
        fixtures.append({"id": identifier, "path": file.relative_to(output).as_posix(), "format": format_name, "sha256": hashlib.sha256(content).hexdigest(), "size": len(content), "commands": list(history), "license": "Self-authored test fixture; source included", **metadata})

    common = ["-O0", "-ffreestanding", "-nostdinc", "-nostdlib", "-fno-builtin", "-fno-stack-protector", "-fno-sanitize=undefined", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables"]
    for bits, target, machine in ((64, "x86_64-windows-msvc", "x64"), (32, "x86-windows-msvc", "x86")):
        obj = work / f"pe{bits}.obj"
        api = work / f"api{bits}.obj"
        library = work / f"fixture{bits}.lib"
        dll = binaries / f"fixture{bits}.dll"
        exe = binaries / f"pe{bits}.exe"
        run("cc", "-target", target, *common, "-c", source / "audit_api.c", "-o", api)
        run("lld-link", "/dll", "/noentry", "/nodefaultlib", f"/machine:{machine}", f"/out:{dll}", f"/implib:{library}", api)
        run("cc", "-target", target, *common, "-c", source / "fixture.c", "-o", obj)
        run("lld-link", f"/machine:{machine}", "/entry:entry", "/subsystem:console", "/nodefaultlib", f"/out:{exe}", obj, library)
        record(f"pe{bits}", exe, "PE", expected_function="compute")
    elf = binaries / "aarch64.elf"
    run("cc", "-target", "aarch64-freestanding", *common, "-nostdlib", "-Wl,-e,entry", source / "fixture.c", "-o", elf)
    record("elf_aarch64", elf, "ELF", expected_function="compute")
    slices = []
    for arch in ("x86_64", "aarch64"):
        macho = binaries / f"macho_{arch}"
        run("cc", "-target", f"{arch}-macos", "-mmacosx-version-min=11.0", *common, "-nostdlib", "-Wl,-e,_entry", source / "fixture.c", "-o", macho)
        slices.append(macho.read_bytes())
        if arch == "x86_64":
            record("macho_x64", macho, "Mach-O", expected_function="compute")
    first_offset = 4096
    second_offset = (first_offset + len(slices[0]) + 4095) & ~4095
    header = struct.pack(">II", 0xCAFEBABE, 2)
    header += struct.pack(">IIIII", 0x01000007, 3, first_offset, len(slices[0]), 12)
    header += struct.pack(">IIIII", 0x0100000C, 0, second_offset, len(slices[1]), 12)
    fat = binaries / "universal.macho"
    fat.write_bytes(header.ljust(first_offset, b"\0") + slices[0] + b"\0" * (second_offset - first_offset - len(slices[0])) + slices[1])
    record("macho_fat", fat, "Mach-O", expected_function="compute")
    for name, target, flags, language, context, expected in (
        ("arm", "arm-freestanding-eabi", ["-marm"], "ARM:LE:32:v7", {"TMode": "0"}, "0310a0e3010080e01eff2fe1"),
        ("thumb", "thumb-freestanding-eabi", ["-mthumb"], "ARM:LE:32:v7", {"TMode": "1"}, "032140187047"),
        ("mips", "mipsel-freestanding-none", ["-fno-pic", "-mno-abicalls"], "MIPS:LE:32:default", {}, "030082240800e00300000000"),
    ):
        obj = work / f"{name}.o"
        raw = binaries / f"{name}.bin"
        run("cc", "-target", target, *flags, "-c", source / f"{name}.s", "-o", obj)
        raw.write_bytes(elf_text(obj))
        if not raw.read_bytes().hex().startswith(expected):
            raise ValueError(f"Unexpected instruction encoding for {name}: {raw.read_bytes().hex()}")
        record(name, raw, "Raw", loader_id="BinaryLoader", language_id=language, compiler_id="default", image_base="0x1000", entries=[{"address": {"space": "ram", "value": "0x1000"}, "context": context}], expected_prefix=expected)
    manifest = {"schema_version": 1, "toolchain": f"Zig {version} (Clang/LLD)", "fixtures": fixtures}
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("tests/fixtures"))
    parser.add_argument("--toolchain", type=Path, required=True)
    args = parser.parse_args()
    result = build_fixtures(args.output, args.toolchain)
    print(f"Built {len(result['fixtures'])} verified fixtures using {result['toolchain']}")
