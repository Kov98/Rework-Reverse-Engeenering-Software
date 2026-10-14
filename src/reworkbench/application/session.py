from __future__ import annotations

import threading
import uuid
from concurrent.futures import CancelledError, Future, ThreadPoolExecutor
from pathlib import Path

from reworkbench.domain import Address, Cancellation, DecompileResult, EngineConfig, ImportOptions, Page, RECORD_KINDS
from reworkbench.engine.supervisor import WorkerSupervisor
from reworkbench.project.repository import ProjectRepository


class Session:
    def __init__(self, config: EngineConfig, emit, *, supervisor_factory=WorkerSupervisor):
        self.config, self.emit = config, emit
        self._factory = supervisor_factory
        self._operations = ThreadPoolExecutor(max_workers=1, thread_name_prefix="semantic")
        self._io = ThreadPoolExecutor(max_workers=2, thread_name_prefix="project-io")
        self._writes = ThreadPoolExecutor(max_workers=1, thread_name_prefix='project-writes')
        self._write_lock = threading.RLock()
        self._write_failures = {}
        self._transitioning = False
        self._rpc_lock = threading.RLock()
        self._project_lock = threading.RLock()
        self._pending = {}
        self._repo = None
        self._worker = None
        self._worker_epoch = 0
        self._opened_analysis = None
        self._needs_reconcile = set()
        self._generation = 0
        self._selection = None
        self._imports = []
        self._current_cancel = None
        self._rpc_cancel = None
        self._active_request = None
        self._close_future = None
        self._closing = False
        self._closed = False
        self._scans = []
        self._background_cancels = []

    @property
    def project_root(self):
        return self._repo.root if self._repo else None

    @property
    def generation(self):
        return self._generation

    @property
    def closed(self):
        return self._closed

    def _event(self, kind, analysis_id=None, request_id=None, revision=0, payload=None, generation=None):
        self.emit({"kind": kind, "analysis_id": analysis_id, "request_id": request_id, "revision": revision, "generation": self._generation if generation is None else generation, "payload": payload or {}})

    def _on_engine_event(self, message):
        if message.operation in ("heartbeat", "hello", "progress"):
            if message.operation == "heartbeat":
                self._event("heartbeat", payload=message.payload)
            return
        with self._rpc_lock:
            future = self._pending.pop(message.request_id, None)
            if future and not future.done():
                if message.error:
                    future.set_exception(RuntimeError(message.error["message"]))
                else:
                    future.set_result(message.payload)
            if message.operation == "failure":
                for pending in self._pending.values():
                    if not pending.done():
                        pending.set_exception(RuntimeError(message.error["message"]))
                self._pending.clear()
                self._opened_analysis = None
        if message.operation == "failure":
            self._event("engine_failure", payload=message.error)

    def _get_worker(self):
        if self._closing:
            raise CancelledError('A aplicação está sendo encerrada.')
        if self._worker is not None and getattr(self._worker, "_failed", False):
            self._drop_worker()
        if self._worker is None:
            directory = self._repo.root / "logs" if self._repo else None
            self._worker_epoch += 1
            epoch = self._worker_epoch

            def receive(message):
                if epoch == self._worker_epoch:
                    self._on_engine_event(message)
            self._worker = self._factory(self.config, receive, log_dir=directory)
            try:
                self._event("status", payload={"message": "Iniciando motor de análise…"})
                self._worker.start()
            except Exception:
                self._worker.close()
                self._worker = None
                raise
        return self._worker

    def _rpc(self, operation, analysis_id=None, payload=None, cancel=None):
        worker = self._get_worker()
        cancel = cancel or Cancellation()
        future = Future()
        with self._rpc_lock:
            request_id = worker.submit(operation, analysis_id, payload or {})
            self._pending[request_id] = future
            self._active_request = request_id
            self._rpc_cancel = cancel
        cancel.bind(lambda: worker.cancel(request_id))
        try:
            return future.result()
        finally:
            cancel.bind(lambda: None)
            self._active_request = None
            self._rpc_cancel = None

    def _change_project(self, root, create, generation):
        if generation != self._generation or self._closing:
            return
        try:
            self._drain_writes()
            self._drop_worker()
            with self._project_lock:
                if self._repo:
                    self._repo.close()
                    self._repo = None
                self._repo = ProjectRepository.create(root) if create else ProjectRepository.open(root)
                self._needs_reconcile.clear()
        finally:
            with self._write_lock:
                if generation == self._generation:
                    self._transitioning = False
        if generation != self._generation:
            return
        self._event("project_opened", payload={"root": str(self._repo.root)}, generation=generation)
        analyses = self._repo.analyses()
        if analyses:
            latest = analyses[-1]
            self._event("analysis_created", latest.id, payload=self._details(latest.id), generation=generation)
            if self._repo.get_metadata(latest.id).get("program_path"):
                try:
                    self._ensure_analysis_open(latest.id, reconcile=True, generation=generation)
                except Exception as exc:
                    self._event("error", latest.id, payload={"message": str(exc)}, generation=generation)

    def _request_project(self, root, create):
        if self._closing:
            raise RuntimeError('A aplicação está sendo encerrada.')
        with self._write_lock:
            self._transitioning = True
            self._generation += 1
        self._selection = None
        self.cancel_active()
        for cancel in tuple(self._imports):
            try:
                cancel.cancel()
            except (OSError, RuntimeError):
                pass
        return self._operations.submit(self._change_project, root, create, self._generation)

    def create_project(self, root: Path) -> Future:
        return self._request_project(root, True)

    def open_project(self, root: Path) -> Future:
        return self._request_project(root, False)

    def configure(self, config: EngineConfig) -> Future:
        def change():
            self._drop_worker()
            self.config = config
        return self._operations.submit(change)

    def catalog(self) -> Future:
        return self._operations.submit(self._rpc, "catalog")

    def inspect_sample(self, source: Path) -> Future:
        return self._operations.submit(self._rpc, "inspect", None, {"sample_path": str(source)})

    def list_analyses(self) -> Future:
        return self._project_io(lambda repo: repo.analyses())

    def _project_io(self, operation):
        repo, generation = self._repo, self._generation
        if self._closing:
            raise RuntimeError('A aplicação está sendo encerrada.')

        def run():
            with self._project_lock:
                if repo is None or repo is not self._repo or generation != self._generation:
                    raise CancelledError('O projeto foi alterado.')
                return operation(repo)
        return self._io.submit(run)

    def _details(self, analysis_id):
        analysis = self._repo.get_analysis(analysis_id)
        sample = self._repo.get_sample(analysis.sample_sha256)
        return {**self._repo.get_metadata(analysis_id), "id": analysis_id, "state": analysis.state, "revision": analysis.revision, "sha256": sample.sha256, "size": sample.size, "sample_path": str(self._repo.root / sample.relative_path), "options": analysis.options.to_json()}

    def analysis_details(self, analysis_id) -> Future:
        return self._project_io(lambda repo: self._details(analysis_id))

    def _state(self, analysis_id, state, message=None, generation=None):
        self._repo.set_state(analysis_id, state)
        self._event("state", analysis_id, payload={"state": state, "message": message or state}, generation=generation)

    def start_analysis(self, source: Path, options: ImportOptions) -> Future:
        cancel = Cancellation()
        self._imports.append(cancel)
        generation = self._generation
        return self._operations.submit(self._analyze, source, options, cancel, generation)

    def _analyze(self, source, options, cancel, generation):
        analysis_id = None
        self._current_cancel = cancel
        try:
            if cancel.cancelled() or generation != self._generation or self._closing:
                return None
            if self._repo is None:
                raise RuntimeError("Crie ou abra um projeto antes de importar.")
            self._event("status", payload={"message": "Copiando e verificando amostra…"}, generation=generation)
            sample = self._repo.import_sample(source, cancel)
            analysis = self._repo.create_analysis(sample, options)
            analysis_id = analysis.id
            provenance = {'original_path': str(source.resolve()), 'original_name': source.name}
            self._repo.set_metadata(analysis_id, provenance)
            self._event("analysis_created", analysis_id, payload=self._details(analysis_id), generation=generation)
            self._state(analysis_id, "importing", "Importando no Ghidra…", generation)
            metadata = {**provenance, **self._rpc("open", analysis_id, {"analysis_dir": str(self._repo.root / "analyses" / analysis_id), "sample_path": str(self._repo.root / sample.relative_path), "options": options.to_json()}, cancel)}
            self._opened_analysis = analysis_id
            self._needs_reconcile.add(analysis_id)
            self._repo.set_metadata(analysis_id, metadata)
            self._event("analysis_opened", analysis_id, payload=self._details(analysis_id), generation=generation)
            if cancel.cancelled():
                self._state(analysis_id, "cancelled", generation=generation)
                return analysis_id
            self._state(analysis_id, "analyzing", "Executando autoanálise…", generation)
            result = self._rpc("analyze", analysis_id, {"timeout_s": options.analysis_timeout_s}, cancel)
            metadata.update(result)
            self._repo.set_metadata(analysis_id, metadata)
            self._repo.set_revision(analysis_id, metadata.get("revision", 0))
            self._index(analysis_id, generation)
            self._state(analysis_id, result["state"], "Análise concluída." if result["state"] == "ready" else "Resultados parciais disponíveis.", generation)
            self._event("analysis_ready", analysis_id, payload=self._details(analysis_id), generation=generation)
            return analysis_id
        except CancelledError:
            if analysis_id:
                self._state(analysis_id, "cancelled", generation=generation)
                self._event('analysis_ready', analysis_id, payload=self._details(analysis_id), generation=generation)
            return analysis_id
        except Exception as exc:
            if analysis_id:
                self._state(analysis_id, "cancelled" if cancel.cancelled() else "failed", str(exc), generation)
            self._event("error", analysis_id, payload={"message": str(exc)}, generation=generation)
            raise
        finally:
            if cancel in self._imports:
                self._imports.remove(cancel)
            self._current_cancel = None

    def _index(self, analysis_id, generation=None):
        self._needs_reconcile.add(analysis_id)
        previous, interrupt = self._current_cancel, Cancellation()
        self._current_cancel = interrupt
        try:
            for kind in RECORD_KINDS:
                if generation is not None and generation != self._generation:
                    raise CancelledError('O projeto foi alterado.')
                self._event('status', analysis_id, payload={'message': f'Indexando {kind}…'}, generation=generation)

                def pages():
                    offset = 0
                    while True:
                        if interrupt.cancelled() or self._closing:
                            raise CancelledError('Indexação interrompida; resultados anteriores preservados.')
                        result = self._rpc('page', analysis_id, {'kind': kind, 'offset': offset, 'limit': 200}, interrupt)
                        page = Page(tuple(result['items']), result['offset'], result['total'])
                        yield page
                        if len(page.items) < 200:
                            break
                        offset += len(page.items)
                self._repo.replace_records(analysis_id, kind, pages())
                self._event('snapshot', analysis_id, payload={'kind': kind}, generation=generation)
            self._needs_reconcile.discard(analysis_id)
        except Exception:
            if interrupt.cancelled():
                raise CancelledError('Indexação interrompida.') from None
            raise
        finally:
            self._current_cancel = previous

    def _ensure_analysis_open(self, analysis_id, reconcile=False, generation=None):
        generation = self._generation if generation is None else generation
        if generation != self._generation:
            raise CancelledError('O projeto foi alterado.')
        if self._opened_analysis == analysis_id and analysis_id not in self._needs_reconcile and not reconcile:
            return
        if self._opened_analysis != analysis_id or reconcile:
            analysis = self._repo.get_analysis(analysis_id)
            sample = self._repo.get_sample(analysis.sample_sha256)
            metadata = self._rpc("open", analysis_id, {"analysis_dir": str(self._repo.root / "analyses" / analysis_id), "sample_path": str(self._repo.root / sample.relative_path), "options": analysis.options.to_json()})
            self._opened_analysis = analysis_id
            self._repo.set_metadata(analysis_id, {**self._repo.get_metadata(analysis_id), **metadata})
            self._repo.set_revision(analysis_id, metadata.get("revision", 0))
        self._index(analysis_id, generation)
        self._event("analysis_opened", analysis_id, payload=self._details(analysis_id), generation=generation)

    def activate_analysis(self, analysis_id) -> Future:
        self._selection = None
        return self._operations.submit(self._ensure_analysis_open, analysis_id)

    def select_function(self, analysis_id, address: Address) -> str:
        token = uuid.uuid4().hex
        self._selection = token
        generation = self._generation

        def select():
            if token != self._selection or generation != self._generation:
                return
            try:
                self._ensure_analysis_open(analysis_id, generation=generation)
                analysis = self._repo.get_analysis(analysis_id)
                result = self._repo.get_cache(analysis_id, address, analysis.revision)
                if result is not None and (result.error or not result.code):
                    result = None
                if result is None:
                    result = DecompileResult.from_json(self._rpc("decompile", analysis_id, {"address": address.to_json(), "timeout_s": analysis.options.decompile_timeout_s}))
                    if result.code and not result.error:
                        self._repo.put_cache(analysis_id, result)
                if token == self._selection and generation == self._generation:
                    self._event("selection_result", analysis_id, token, result.revision, result.to_json(), generation)
            except Exception as exc:
                if token == self._selection and generation == self._generation:
                    self._event("selection_result", analysis_id, token, payload={"address": address.to_json(), "code": None, "error": str(exc)}, generation=generation)
        self._operations.submit(select)
        return token

    def fetch_page(self, analysis_id, kind, offset=0, query="") -> Future:
        return self._project_io(lambda repo: repo.page(analysis_id, kind, offset=offset, query=query))

    def rename_function(self, analysis_id, address, name) -> Future:
        def rename():
            self._ensure_analysis_open(analysis_id)
            self._needs_reconcile.add(analysis_id)
            result = self._rpc("rename", analysis_id, {"address": address.to_json(), "name": name})
            self._repo.set_revision(analysis_id, result["revision"])
            self._repo.set_metadata(analysis_id, {**self._repo.get_metadata(analysis_id), 'revision': result['revision']})
            self._index(analysis_id)
            self._event("renamed", analysis_id, revision=result["revision"], payload=result)
        return self._operations.submit(rename)

    def set_note(self, analysis_id, address, text, bookmarked) -> Future:
        with self._write_lock:
            if self._closing or self._transitioning or self._repo is None:
                raise RuntimeError('Não é possível gravar uma nota durante a troca ou fechamento do projeto.')
            repo = self._repo
            key = (analysis_id, address)
            future = self._writes.submit(repo.set_note, analysis_id, address, text, bookmarked)

            def written(done):
                error = done.exception()
                with self._write_lock:
                    if error:
                        self._write_failures[key] = error
                    else:
                        self._write_failures.pop(key, None)
                if error:
                    self._event('error', analysis_id, payload={'message': f'A nota não foi salva: {error}'})
            future.add_done_callback(written)
            return future

    def _drain_writes(self):
        # A dedicated FIFO keeps accepted edits durable even when ordinary I/O
        # reads are invalidated by a project generation change.
        self._writes.submit(lambda: None).result()
        with self._write_lock:
            if self._write_failures:
                error = next(iter(self._write_failures.values()))
                raise RuntimeError(f'Existem notas que não foram salvas. Tente salvá-las novamente: {error}')

    def notes(self, analysis_id) -> Future:
        return self._project_io(lambda repo: repo.notes(analysis_id))

    def audit_hints(self, analysis_id) -> Future:
        from reworkbench.application.audit import review_imports
        cancel = Cancellation()
        self._background_cancels.append(cancel)

        def review(repo):
            try:
                return review_imports(repo, analysis_id, cancel)
            finally:
                if cancel in self._background_cancels:
                    self._background_cancels.remove(cancel)
        future = self._project_io(review)

        def finished(_):
            if cancel in self._background_cancels:
                self._background_cancels.remove(cancel)
        future.add_done_callback(finished)
        return future

    def scan_sample(self, source: Path) -> Future:
        from reworkbench.application.byte_scan import scan_bytes
        for previous in tuple(self._scans):
            previous.cancel()
        cancel = Cancellation()
        self._scans.append(cancel)
        native_root = Path(__file__).resolve().parents[3] / 'native'
        candidates = (
            native_root / 'bytecore_cpp' / 'build' / 'Release' / 'bytecore_cpp.exe',
            native_root / 'bytecore_cpp' / 'build' / 'bytecore_cpp.exe',
            native_root / 'bytecore' / 'target' / 'release' / 'bytecore.exe',
        )

        def scan():
            try:
                executable = next((candidate for candidate in candidates if candidate.is_file()), None)
                return scan_bytes(source, cancel=cancel, native_executable=executable)
            finally:
                if cancel in self._scans:
                    self._scans.remove(cancel)
        return self._io.submit(scan)

    def save(self) -> Future:
        def save():
            self._drain_writes()
            if self._opened_analysis:
                self._rpc('save', self._opened_analysis)
            self._event('status', payload={'message': 'Projeto salvo. Notas e amostras são persistidas automaticamente.'})
        return self._operations.submit(save)

    def export_report(self, analysis_id, destination) -> Future:
        from reworkbench.application.audit import review_imports
        from reworkbench.application.exports import export_report
        cancel = Cancellation()

        def export():
            self._current_cancel = cancel
            try:
                if self._closing:
                    raise CancelledError('A aplicação está sendo encerrada.')
                self._drain_writes()
                return export_report(self._repo, analysis_id, destination, review_imports(self._repo, analysis_id, cancel), cancel)
            finally:
                self._current_cancel = None
        return self._operations.submit(export)

    def export_functions(self, analysis_id, addresses, destination) -> Future:
        from reworkbench.application.exports import export_pseudocode
        cancel = Cancellation()

        def export():
            self._current_cancel = cancel
            try:
                if self._closing:
                    raise CancelledError('A aplicação está sendo encerrada.')
                self._ensure_analysis_open(analysis_id)
                analysis = self._repo.get_analysis(analysis_id)

                def results():
                    for address in addresses:
                        if cancel.cancelled():
                            raise CancelledError('Exportação interrompida.')
                        self._event('status', analysis_id, payload={'message': f'Exportando {address}…'})
                        cached = self._repo.get_cache(analysis_id, address, analysis.revision)
                        if cached is not None and (cached.error or not cached.code):
                            cached = None
                        if cached is None:
                            try:
                                self._ensure_analysis_open(analysis_id)
                                cached = DecompileResult.from_json(self._rpc('decompile', analysis_id, {'address': address.to_json(), 'timeout_s': analysis.options.decompile_timeout_s}, cancel))
                                if cached.code and not cached.error:
                                    self._repo.put_cache(analysis_id, cached)
                            except Exception as exc:
                                if cancel.cancelled() or self._closing:
                                    raise CancelledError('Exportação interrompida.') from exc
                                cached = DecompileResult(address, None, str(exc), analysis.revision)
                                if self._worker is not None and getattr(self._worker, '_failed', False):
                                    self._drop_worker()
                        yield cached
                return export_pseudocode(self._repo, analysis_id, results(), destination)
            finally:
                self._current_cancel = None
        return self._operations.submit(export)

    def cancel_active(self):
        cancellations = [self._current_cancel, self._rpc_cancel]
        if not any(cancellations) and self._imports:
            cancellations.append(self._imports[0])
        for cancel in cancellations:
            if cancel:
                try:
                    cancel.cancel()
                except (OSError, RuntimeError):
                    # A disconnected worker is already being handled by its
                    # watchdog; cancellation must remain safe from the UI.
                    pass
        for cancel in tuple(self._scans):
            cancel.cancel()
        for cancel in tuple(self._background_cancels):
            cancel.cancel()
        if self._worker is not None and self._active_request is None:
            interrupt = getattr(self._worker, 'interrupt_startup', None)
            if interrupt:
                interrupt()

    def _drop_worker(self):
        self._worker_epoch += 1
        if self._worker:
            self._worker.close()
            self._worker = None
        self._opened_analysis = None

    def close(self) -> Future:
        if self._close_future is not None:
            return self._close_future
        self._closing = True
        self._selection = None
        self.cancel_active()
        for cancel in tuple(self._imports):
            try:
                cancel.cancel()
            except (OSError, RuntimeError):
                pass

        def cleanup():
            # Refuse a clean close while an accepted note failed to reach disk.
            # Executors remain available so the editor can retry that write.
            self._drain_writes()
            try:
                try:
                    self._drop_worker()
                finally:
                    self._io.shutdown(wait=True, cancel_futures=True)
                    self._writes.shutdown(wait=True)
                    if self._repo:
                        self._repo.close()
                        self._repo = None
            finally:
                self._closed = True

        def finalized(done):
            if self._closed:
                self._operations.shutdown(wait=False)
            else:
                self._closing = False
                self._close_future = None
        close_future = self._operations.submit(cleanup)
        self._close_future = close_future
        close_future.add_done_callback(finalized)
        return close_future
