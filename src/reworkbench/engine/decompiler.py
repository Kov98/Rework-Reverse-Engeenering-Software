from reworkbench.domain import DecompileResult
from reworkbench.engine.records import address_json


class Decompiler:
    def __init__(self, program):
        from ghidra.app.decompiler import DecompInterface
        self.program = program
        self.interface = DecompInterface()
        if not self.interface.openProgram(program):
            message = str(self.interface.getLastMessage())
            self.close()
            raise RuntimeError(message)

    def decompile(self, address, java_address, timeout_s, monitor, revision):
        manager = self.program.getFunctionManager()
        function = manager.getFunctionAt(java_address) or manager.getFunctionContaining(java_address)
        if function is None:
            return DecompileResult(address, None, "Nenhuma função reconhecida nesse endereço.", revision)
        try:
            result = self.interface.decompileFunction(function, timeout_s, monitor)
            if not result.decompileCompleted():
                return DecompileResult(address, None, str(result.getErrorMessage()) or "Descompilação interrompida.", revision)
            decompiled = result.getDecompiledFunction()
            code = str(decompiled.getC()) if decompiled else None
            return DecompileResult(address, code, None if code else "Motor não produziu pseudocódigo.", revision)
        except Exception as exc:
            return DecompileResult(address, None, str(exc), revision)

    def invalidate(self):
        self.interface.flushCache()

    def close(self):
        if self.interface is not None:
            self.interface.closeProgram()
            self.interface.dispose()
            self.interface = None
