from __future__ import annotations

import contextlib
import hashlib
import itertools
import json
import os
import threading
from pathlib import Path

from reworkbench.domain import Address, Cancellation, EngineConfig, ImportOptions, Page, RECORD_KINDS
from reworkbench.engine.config import validate_engine
from reworkbench.engine.decompiler import Decompiler
from reworkbench.engine.loaders import fat_slices, inspect_file, java_address, language_catalog, seed_entries
from reworkbench.engine.records import address_json, function_record, iter_records
from reworkbench.engine.project_path import ghidra_project_path


@contextlib.contextmanager
def task_monitor(cancel: Cancellation, timeout_s=None):
    from ghidra.util.task import TaskMonitorAdapter
    monitor = TaskMonitorAdapter(True)
    cancel.bind(monitor.cancel)
    expired = threading.Event()

    def expire():
        expired.set()
        cancel.cancel()

    timer = threading.Timer(timeout_s, expire) if timeout_s else None
    if timer:
        timer.daemon = True
        timer.start()
    try:
        yield monitor, expired
    finally:
        if timer:
            timer.cancel()
        cancel.bind(lambda: None)


class GhidraBackend:
    def __init__(self, config: EngineConfig):
        diagnostic = validate_engine(config)
        if not diagnostic["ready"]:
            raise RuntimeError("; ".join(issue["message"] for issue in diagnostic["issues"]))
        os.environ["JAVA_HOME"] = str(config.java_home)
        os.environ["GHIDRA_INSTALL_DIR"] = str(config.ghidra_home)
        os.environ["PATH"] = str(config.java_home / "bin") + os.pathsep + os.environ.get("PATH", "")
        import pyghidra
        pyghidra.start(install_dir=config.ghidra_home)
        self.pyghidra = pyghidra
        self.program = None
        self._stack = None
        self._decompiler = None
        self._cursors = {}
        self.revision = 0
        self.slice_offset = 0

    def catalog(self):
        return language_catalog()

    def inspect(self, sample_path: Path):
        return inspect_file(sample_path)

    def open(self, analysis_dir: Path, sample_path: Path, options: ImportOptions, cancel: Cancellation):
        self.close()
        analysis_dir.mkdir(parents=True, exist_ok=True)
        self.analysis_dir, self.sample_path = analysis_dir, sample_path
        sidecar = analysis_dir / "engine.json"
        saved = json.loads(sidecar.read_text(encoding="utf-8")) if sidecar.is_file() else None
        self.options = ImportOptions.from_json(saved["options"]) if saved else options
        self.slice_offset = saved.get("slice_offset", 0) if saved else 0
        self.program_path = saved.get("program_path", "/sample") if saved else "/sample"
        self._stack = contextlib.ExitStack()
        try:
            project_exists = (analysis_dir / "program.gpr").exists()
            project_location = ghidra_project_path(analysis_dir, self._stack.callback)
            project = self._stack.enter_context(self.pyghidra.open_project(str(project_location), "program", create=not project_exists))
            if not saved:
                inspection = inspect_file(sample_path)
                if inspection["raw_required"] and options.loader_id != "BinaryLoader":
                    raise ValueError("Arquivo sem formato reconhecido; requer configuração de arquitetura e base para importar firmware bruto.")
                if options.loader_id == "BinaryLoader" and not options.language_id:
                    raise ValueError("Firmware bruto requer configuração de linguagem/processador.")
                with task_monitor(cancel) as (monitor, _):
                    builder = self.pyghidra.program_loader().project(project).name("sample").monitor(monitor)
                    if options.loader_id:
                        builder = builder.loaders(options.loader_id)
                    if options.language_id:
                        builder = builder.language(options.language_id)
                    if options.compiler_id:
                        builder = builder.compiler(options.compiler_id)
                    if options.loader_id == "BinaryLoader":
                        builder = builder.addLoaderArg("-loader-baseAddr", options.image_base or "0x0")
                    if inspection["container"]:
                        selected = next((choice for choice in inspection["choices"] if choice["id"] == options.load_spec_id), None)
                        if selected is None:
                            raise ValueError("Escolha uma imagem do contêiner antes de analisar.")
                        self.slice_offset = selected["file_offset"]
                        with self.pyghidra.open_filesystem(str(sample_path)) as filesystem:
                            slices = list(filesystem.getListing(None))
                            chosen = slices[selected["slice_index"]]
                            with builder.source(chosen.getFSRL()).load() as results:
                                results.save(monitor)
                    else:
                        with builder.source(str(sample_path)).load() as results:
                            results.save(monitor)
            self.program = self._stack.enter_context(self.pyghidra.program_context(project, self.program_path))
            if not saved and options.entries:
                with task_monitor(cancel) as (monitor, _):
                    seed_entries(self.program, options.entries, monitor, self.pyghidra)
                self.save()
            self.revision = int(self.pyghidra.program_info(self.program).getInt("REWorkbench Revision", 0))
            self._decompiler = Decompiler(self.program)
            self._cursors.clear()
            self.metadata = self._metadata()
            temporary = sidecar.with_suffix(".tmp")
            temporary.write_bytes(json.dumps({"program_path": self.program_path, "options": self.options.to_json(), "slice_offset": self.slice_offset}, ensure_ascii=False, indent=2).encode("utf-8"))
            temporary.replace(sidecar)
            return self.metadata
        except Exception:
            self.close()
            raise

    def _metadata(self):
        digest = hashlib.sha256()
        with self.sample_path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        program = self.program
        return {"size": self.sample_path.stat().st_size, "sha256": digest.hexdigest(), "format": str(program.getExecutableFormat()), "language_id": str(program.getLanguageID()), "compiler_id": str(program.getCompilerSpec().getCompilerSpecID()), "image_base": self.options.image_base or address_json(program.getImageBase())["value"], "program_image_base": address_json(program.getImageBase()), "entry_points": [address_json(a) for a in program.getSymbolTable().getExternalEntryPointIterator()], "ghidra_version": "12.1.4", "program_path": self.program_path, "revision": self.revision, "slice_offset": self.slice_offset, "options": self.options.to_json()}

    def analyze(self, timeout_s, cancel: Cancellation):
        if self.program is None:
            raise RuntimeError("Abra uma análise antes de solicitar operações do motor.")
        with task_monitor(cancel, timeout_s) as (monitor, expired):
            try:
                log = str(self.pyghidra.analyze(self.program, monitor))
            except Exception:
                if not cancel.cancelled():
                    raise
                log = "Análise interrompida. Resultados parciais preservados."
            state = "partial" if expired.is_set() else "cancelled" if cancel.cancelled() else "ready"
        self.save()
        self._decompiler.invalidate()
        self._cursors.clear()
        return {"state": state, "stages": ["import", "autoanalysis"] if state == "ready" else ["import"], "log": log[-16384:], "revision": self.revision}

    def page(self, kind, offset=0, limit=200, query=""):
        if self.program is None or kind not in RECORD_KINDS or offset < 0 or not 1 <= limit <= 200:
            raise ValueError("Programa ou página indisponível.")
        key = (kind, query)
        cursor = self._cursors.get(key)
        if cursor is None or cursor[1] != offset:
            rows = iter_records(self.program, kind, self.slice_offset)
            if query:
                rows = (row for row in rows if query.casefold() in json.dumps(row, ensure_ascii=False).casefold())
            rows = itertools.islice(rows, offset, None)
        else:
            rows = cursor[0]
        items = tuple(itertools.islice(rows, limit))
        self._cursors[key] = (rows, offset + len(items))
        # -1 explicitly means unknown while streaming; the UI indexes exact totals.
        return Page(items, offset, offset + len(items) if len(items) < limit else -1)

    def decompile(self, address: Address, timeout_s, cancel: Cancellation):
        if self.program is None:
            raise RuntimeError("Análise não está aberta.")
        with task_monitor(cancel, timeout_s + 1) as (monitor, _):
            return self._decompiler.decompile(address, java_address(self.program, address), timeout_s, monitor, self.revision)

    def rename(self, address: Address, name: str, cancel: Cancellation):
        from ghidra.program.model.symbol import SourceType
        if not name.isidentifier() or len(name) > 240:
            raise ValueError("Use um nome de função válido, sem espaços, com até 240 caracteres.")
        function = self.program.getFunctionManager().getFunctionAt(java_address(self.program, address))
        if function is None:
            raise ValueError("Nenhuma função reconhecida nesse endereço.")
        if cancel.cancelled():
            raise RuntimeError("Renomeação cancelada.")
        with self.pyghidra.transaction(self.program, "Renomear função"):
            function.setName(name, SourceType.USER_DEFINED)
            self.pyghidra.program_info(self.program).setInt("REWorkbench Revision", self.revision + 1)
        self.save()
        self.revision += 1
        self._decompiler.invalidate()
        self._cursors.clear()
        return {**function_record(function), "revision": self.revision}

    def save(self):
        if self.program is not None:
            with task_monitor(Cancellation()) as (monitor, _):
                self.program.save("REWorkbench", monitor)

    def close(self):
        try:
            if self._decompiler:
                self._decompiler.close()
                self._decompiler = None
        finally:
            self.program = None
            if self._stack:
                self._stack.close()
                self._stack = None
            self._cursors.clear()
