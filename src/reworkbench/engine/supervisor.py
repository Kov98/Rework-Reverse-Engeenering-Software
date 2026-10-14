from __future__ import annotations

import json
import os
import secrets
import subprocess
import sys
import threading
import time
import uuid
from multiprocessing.connection import Listener
from multiprocessing import AuthenticationError
from pathlib import Path

from reworkbench.domain import EngineConfig, Envelope
from reworkbench.engine.protocol import MAX_MESSAGE, decode, encode
from reworkbench.engine.winjob import WindowsJob


class WorkerSupervisor:
    def __init__(self, config: EngineConfig, on_event, *, worker_command=None, startup_timeout_s=60, idle_timeout_s=60, cancel_grace_s=5, clock=time.monotonic, log_dir: Path | None = None):
        self.config, self.on_event = config, on_event
        self.worker_command = worker_command or [sys.executable, "-u", "-m", "reworkbench.engine.worker"]
        self.startup_timeout_s, self.idle_timeout_s, self.cancel_grace_s = startup_timeout_s, idle_timeout_s, cancel_grace_s
        self.clock = clock
        self.process = None
        self._connection = self._listener = self._job = None
        self._ready = threading.Event()
        self._stopped = threading.Event()
        self._closing = False
        self._failed = False
        self._startup_error = None
        self._lock = threading.RLock()
        self._send_lock = threading.Lock()
        self._pending = {}
        folder = log_dir or Path.cwd() / ".superpowers" / "logs"
        folder.mkdir(parents=True, exist_ok=True)
        self.log_path = folder / f"worker-{uuid.uuid4().hex}.log"

    def start(self):
        token = secrets.token_bytes(32)
        self._listener = Listener(("127.0.0.1", 0), authkey=token)
        environment = dict(os.environ)
        environment["REWORKBENCH_WORKER_AUTH"] = token.hex()
        environment["REWORKBENCH_WORKER_PORT"] = str(self._listener.address[1])
        environment["REWORKBENCH_WORKER_CONFIG"] = json.dumps({"ghidra_home": str(self.config.ghidra_home) if self.config.ghidra_home else None, "java_home": str(self.config.java_home) if self.config.java_home else None})
        self._last_received = self.clock()
        try:
            self._job = WindowsJob()
            with self.log_path.open("wb") as log:
                self.process = subprocess.Popen(self.worker_command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
            self._job.attach(self.process.pid)
            self.process.stdin.write(b"BOOT\n")
            self.process.stdin.flush()
            self.process.stdin.close()
            self._reader = threading.Thread(target=self._read, name="engine-ipc", daemon=True)
            self._watcher = threading.Thread(target=self._watch, name="engine-watchdog", daemon=True)
            self._reader.start()
            self._watcher.start()
            if not self._ready.wait(self.startup_timeout_s):
                self._fail("startup_timeout", "O motor não iniciou no prazo configurado.")
            if self._startup_error:
                raise RuntimeError(self._startup_error)
            if not self._ready.is_set() or self._failed:
                raise RuntimeError("Falha ao iniciar o motor. Consulte o log.")
        except Exception:
            self.close()
            raise

    def _send(self, message):
        data = encode(message)
        with self._send_lock:
            if self._connection is None:
                raise RuntimeError("Motor desconectado.")
            self._connection.send_bytes(data)

    def submit(self, operation, analysis_id, payload):
        if self._closing or self._stopped.is_set() or not self._ready.is_set():
            raise RuntimeError("Motor não está disponível.")
        request_id = uuid.uuid4().hex
        timeout = float(payload.get("timeout_s", 300 if operation == "analyze" else 30))
        duration = timeout + (30 if operation == "analyze" else 5) if operation in ("analyze", "decompile") else 120
        with self._lock:
            self._pending[request_id] = {"operation": operation, "analysis_id": analysis_id, "deadline": self.clock() + duration, "grace": None, "reason": "timeout"}
        try:
            self._send(Envelope(1, request_id, analysis_id, operation, payload))
        except Exception as exc:
            self._fail("transport", str(exc))
            raise
        return request_id

    def cancel(self, request_id):
        with self._lock:
            pending = self._pending.get(request_id)
            if pending is None:
                return
            pending["grace"] = self.clock() + self.cancel_grace_s
            pending["reason"] = "cancelled"
        self._send(Envelope(1, uuid.uuid4().hex, pending["analysis_id"], "cancel", {"target_request_id": request_id}))

    def _read(self):
        try:
            self._connection = self._listener.accept()
            while not self._stopped.is_set():
                message = decode(self._connection.recv_bytes(MAX_MESSAGE))
                self._last_received = self.clock()
                if message.operation == "hello":
                    if message.error:
                        self._startup_error = message.error.get("message", "Falha de inicialização.")
                    self._ready.set()
                if message.operation not in ("heartbeat", "progress"):
                    with self._lock:
                        self._pending.pop(message.request_id, None)
                self.on_event(message)
        except (EOFError, OSError, ValueError, AuthenticationError) as exc:
            if not self._closing and not self._stopped.is_set():
                self._fail("worker_exit", f"O motor foi desconectado: {exc}. Consulte {self.log_path.name}.")

    def _watch(self):
        while not self._stopped.wait(0.05):
            if self.process.poll() is not None:
                self._fail("worker_exit", "O processo do motor encerrou inesperadamente.")
                return
            now = self.clock()
            with self._lock:
                pending = list(self._pending.items())
            for request_id, item in pending:
                if item["grace"] is not None and now >= item["grace"]:
                    self._fail(item["reason"], "Operação interrompida; o motor precisou ser reiniciado.")
                    return
                if item["grace"] is None and now >= item["deadline"]:
                    try:
                        self.cancel(request_id)
                        with self._lock:
                            if request_id in self._pending:
                                self._pending[request_id]["reason"] = "timeout"
                    except OSError as exc:
                        self._fail("transport", str(exc))
                        return
            if pending and now - self._last_received > self.idle_timeout_s:
                self._fail("heartbeat_timeout", "O motor parou de responder.")
                return

    def _kill(self):
        self._stopped.set()
        if self._job:
            self._job.terminate()
        if self.process and self.process.poll() is None:
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        if self._connection:
            self._connection.close()
        if self._listener:
            self._listener.close()

    def _fail(self, code, message):
        with self._lock:
            if self._failed or self._closing:
                return
            self._failed = True
            self._startup_error = message
            pending = list(self._pending.items())
            self._pending.clear()
        self._kill()
        self._ready.set()
        for request_id, item in pending:
            self.on_event(Envelope(1, request_id, item["analysis_id"], item["operation"], {}, {"code": code, "message": message, "details": str(self.log_path)}))
        self.on_event(Envelope(1, "failure", None, "failure", {}, {"code": code, "message": message, "details": str(self.log_path)}))

    def close(self):
        if self._closing:
            return
        self._closing = True
        if self._connection and not self._stopped.is_set():
            try:
                self._send(Envelope(1, "shutdown", None, "close"))
                self.process.wait(timeout=5)
            except (OSError, RuntimeError, subprocess.TimeoutExpired):
                pass
        self._kill()
        if self._job:
            self._job.close()
