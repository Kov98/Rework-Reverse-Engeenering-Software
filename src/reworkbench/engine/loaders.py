"""Ghidra loader discovery. Java imports are delayed until the worker starts."""
import struct
from pathlib import Path


def fat_slices(path: Path) -> list[dict]:
    with path.open("rb") as stream:
        header = stream.read(8)
        if len(header) != 8 or header[:4] != b"\xca\xfe\xba\xbe":
            return []
        count = struct.unpack(">I", header[4:])[0]
        if not 1 <= count < 32:
            return []
        rows = []
        for index in range(count):
            chunk = stream.read(20)
            if len(chunk) != 20:
                raise ValueError("Cabeçalho Mach-O universal truncado.")
            cpu, subtype, offset, size, alignment = struct.unpack(">IIIII", chunk)
            if offset + size > path.stat().st_size:
                raise ValueError("Imagem Mach-O fora dos limites do arquivo.")
            rows.append({"id": f"slice:{index}", "label": f"Imagem {index + 1}: CPU 0x{cpu:x}", "slice_index": index, "file_offset": offset, "size": size, "loader_id": None, "language_id": None, "compiler_id": None, "preferred": False})
        return rows


def inspect_file(path: Path) -> dict:
    slices = fat_slices(path)
    if slices:
        return {"choices": slices, "raw_required": False, "container": True}
    from java.io import File
    from ghidra.app.util.bin import RandomAccessByteProvider
    from ghidra.app.util.opinion import LoaderService
    choices = []
    with RandomAccessByteProvider(File(str(path))) as provider:
        for item in LoaderService.getAllSupportedLoadSpecs(provider).entrySet():
            loader = item.getKey()
            loader_id = str(loader.getClass().getSimpleName())
            if loader_id == "BinaryLoader":
                continue
            for spec in item.getValue():
                pair = spec.getLanguageCompilerSpec()
                language = str(pair.languageID) if pair else None
                compiler = str(pair.compilerSpecID) if pair else None
                identifier = f"{loader_id}|{language}|{compiler}"
                if any(c["id"] == identifier for c in choices):
                    continue
                choices.append({"id": identifier, "loader_id": loader_id, "language_id": language, "compiler_id": compiler, "label": f"{loader.getName()} · {language or 'automático'} · {compiler or 'automático'}", "preferred": bool(spec.isPreferred())})
    return {"choices": choices, "raw_required": not choices, "container": False}


def language_catalog() -> dict:
    from ghidra.program.util import DefaultLanguageService
    service = DefaultLanguageService.getLanguageService()
    languages = []
    for description in service.getLanguageDescriptions(False):
        languages.append({"id": str(description.getLanguageID()), "name": str(description.getDescription()), "processor": str(description.getProcessor()), "bits": int(description.getSize()), "endian": str(description.getEndian()), "compilers": [{"id": str(c.getCompilerSpecID()), "name": str(c.getCompilerSpecName())} for c in description.getCompatibleCompilerSpecDescriptions()]})
    return {"languages": languages}


def seed_entries(program, entries, monitor, pyghidra):
    from java.math import BigInteger
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    with pyghidra.transaction(program, "Configurar entradas do firmware"):
        for entry in entries:
            address = java_address(program, entry.address)
            if not program.getMemory().contains(address):
                raise ValueError(f"Entrada fora da memória carregada: {entry.address}")
            for name, value in entry.context.items():
                register = program.getProgramContext().getRegister(name)
                if register is None:
                    raise ValueError(f"Registrador de contexto desconhecido: {name}")
                program.getProgramContext().setValue(register, address, address, BigInteger(str(int(value, 0))))
            program.getSymbolTable().addExternalEntryPoint(address)
            disassembly = DisassembleCommand(address, None, True)
            if not disassembly.applyTo(program, monitor):
                raise ValueError(f"Disassembly da entrada falhou: {disassembly.getStatusMsg()}")
            if program.getFunctionManager().getFunctionAt(address) is None:
                creation = CreateFunctionCmd(address)
                if not creation.applyTo(program, monitor):
                    raise ValueError(f"Criação de função falhou: {creation.getStatusMsg()}")


def java_address(program, address):
    result = program.getAddressFactory().getAddress(f"{address.space}:{address.value[2:]}")
    if result is None:
        raise ValueError(f"Endereço não pertence à linguagem carregada: {address}")
    return result
