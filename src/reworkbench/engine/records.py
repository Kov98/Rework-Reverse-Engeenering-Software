from reworkbench.domain import Address
from itertools import islice


def address_json(address):
    if address is None:
        return None
    space = address.getAddressSpace()
    value = int(address.getOffset()) & ((1 << int(space.getSize())) - 1)
    return Address(str(space.getName()), hex(value)).to_json()


def file_offset(program, address, slice_offset=0):
    info = program.getMemory().getAddressSourceInfo(address)
    value = int(info.getFileOffset()) if info is not None else -1
    return value + slice_offset if value >= 0 else None


def function_record(function):
    address = address_json(function.getEntryPoint())
    return {"key": f"{address['space']}:{address['value']}", "address": address, "name": str(function.getName()), "size": int(function.getBody().getNumAddresses()), "signature": str(function.getSignature()), "thunk": bool(function.isThunk())}


def iter_records(program, kind, slice_offset=0):
    functions = program.getFunctionManager()
    references = program.getReferenceManager()
    if kind == "functions":
        for function in functions.getFunctions(True):
            yield function_record(function)
    elif kind == "regions":
        for block in program.getMemory().getBlocks():
            start = address_json(block.getStart())
            yield {"key": str(block.getName()) + str(block.getStart()), "name": str(block.getName()), "start": start, "end": address_json(block.getEnd()), "size": int(block.getSize()), "permissions": ("r" if block.isRead() else "-") + ("w" if block.isWrite() else "-") + ("x" if block.isExecute() else "-"), "file_offset": file_offset(program, block.getStart(), slice_offset)}
    elif kind == "symbols":
        for symbol in program.getSymbolTable().getAllSymbols(True):
            yield {"key": str(symbol.getID()), "address": address_json(symbol.getAddress()), "name": str(symbol.getName(True)), "external": bool(symbol.isExternal()), "type": str(symbol.getSymbolType())}
    elif kind == "strings":
        from ghidra.program.model.data import StringDataInstance
        for data in program.getListing().getDefinedData(True):
            if StringDataInstance.isString(data):
                value = StringDataInstance.getStringDataInstance(data).getStringValue()
                if value is not None:
                    text = str(value)
                    yield {"key": str(data.getAddress()), "address": address_json(data.getAddress()), "value": text[:4096], "value_truncated": len(text) > 4096, "length": int(data.getLength()), "file_offset": file_offset(program, data.getAddress(), slice_offset)}
    elif kind == "imports":
        manager = program.getExternalManager()
        for symbol in program.getSymbolTable().getExternalSymbols():
            location = manager.getExternalLocation(symbol)
            if location is None:
                continue
            address = location.getExternalSpaceAddress()
            stubs = [address_json(reference.getFromAddress()) for reference in islice(references.getReferencesTo(address), 129)]
            yield {"key": str(symbol.getID()), "name": str(location.getLabel()), "library": str(location.getLibraryName()), "address": address_json(address), "stub_addresses": stubs[:128], "stub_addresses_truncated": len(stubs) > 128}
    elif kind == "assembly":
        for instruction in program.getListing().getInstructions(True):
            address = instruction.getAddress()
            owner = functions.getFunctionContaining(address)
            yield {"key": str(address), "address": address_json(address), "bytes": " ".join(f"{int(b) & 255:02x}" for b in instruction.getBytes()), "mnemonic": str(instruction.getMnemonicString()), "operands": ", ".join(str(instruction.getDefaultOperandRepresentation(i)) for i in range(instruction.getNumOperands())), "function_address": address_json(owner.getEntryPoint()) if owner else None, "file_offset": file_offset(program, address, slice_offset)}
    elif kind == "xrefs":
        for source in references.getReferenceSourceIterator(program.getMemory(), True):
            owner = functions.getFunctionContaining(source)
            for reference in references.getReferencesFrom(source):
                target = reference.getToAddress()
                target_owner = functions.getFunctionContaining(target)
                yield {"key": f"{source}:{target}:{reference.getOperandIndex()}:{reference.getReferenceType()}", "source": address_json(source), "target": address_json(target), "type": str(reference.getReferenceType()), "source_function": address_json(owner.getEntryPoint()) if owner else None, "target_function": address_json(target_owner.getEntryPoint()) if target_owner else None}
    else:
        raise ValueError(f"Tipo de registro não suportado: {kind}")
