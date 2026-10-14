"""Deterministic external-engine boundary for session state/race tests."""
import json
import threading
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from reworkbench.domain import Envelope


class SessionWorker:
    def __init__(self, config, on_event, **kwargs):
        self.on_event = on_event
        self.executor = ThreadPoolExecutor(max_workers=1)
        self.analysis_gate = threading.Event()
        self.analysis_gate.set()
        self.decompile_gate = threading.Event()
        self.decompile_gate.set()
        self.cancelled = threading.Event()
        self.opens = []
        self.revision = 0

    def start(self):
        pass

    def submit(self, operation, analysis_id, payload):
        request_id = uuid.uuid4().hex
        self.executor.submit(self._execute, request_id, operation, analysis_id, payload)
        return request_id

    def _execute(self, request_id, operation, analysis_id, payload):
        if operation == "open":
            self.folder = Path(payload["analysis_dir"])
            self.folder.mkdir(parents=True, exist_ok=True)
            self.opens.append(analysis_id)
            self.saved = self.folder / "test-engine.json"
            self.names = json.loads(self.saved.read_text()) if self.saved.exists() else {"0x1000": "compute", "0x2000": "caller"}
            result = {"program_path": "/sample", "ghidra_version": "test-boundary", "language_id": "x86:LE:64:default", "revision": self.revision, "format": "fixture"}
        elif operation == "analyze":
            self.analysis_gate.wait(5)
            result = {"state": "cancelled" if self.cancelled.is_set() else "ready", "stages": ["import", "autoanalysis"]}
        elif operation == "page":
            kind = payload["kind"]
            rows = [{"key": address, "address": {"space": "ram", "value": address}, "name": name, "size": 16} for address, name in self.names.items()] if kind == "functions" else []
            offset = payload.get("offset", 0)
            result = {"items": rows[offset:offset+200], "offset": offset, "total": len(rows)}
        elif operation == "decompile":
            self.decompile_gate.wait(5)
            result = {"address": payload["address"], "code": "return 3;", "error": None, "revision": self.revision}
        elif operation == "rename":
            self.names[payload["address"]["value"]] = payload["name"]
            self.saved.write_text(json.dumps(self.names))
            self.revision += 1
            result = {"revision": self.revision, "name": payload["name"], "address": payload["address"]}
        else:
            result = {}
        self.on_event(Envelope(1, request_id, analysis_id, operation, result))

    def cancel(self, request_id):
        self.cancelled.set()
        self.analysis_gate.set()
        self.decompile_gate.set()

    def close(self):
        self.analysis_gate.set()
        self.decompile_gate.set()
        self.executor.shutdown(wait=True)
