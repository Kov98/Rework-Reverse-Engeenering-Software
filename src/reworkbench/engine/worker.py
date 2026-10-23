"""Worker entry point. This is the only process that imports the Ghidra JVM."""
from __future__ import annotations

import dataclasses
import json
import os
import queue
import sys
import threading
import traceback
from multiprocessing.connection import Client
from pathlib import Path

from reworkbench.domain import Address, Cancellation, EngineConfig, Envelope, ImportOptions, Page
from reworkbench.engine.protocol import MAX_MESSAGE, decode, encode


def serve(backend_factory):
    if sys.stdin.buffer.readline() != b"BOOT\n":
        raise RuntimeError("Worker deve ser iniciado pelo supervisor.")
    auth = bytes.fromhex(os.environ.pop("REWORKBENCH_WORKER_AUTH"))
    config_data = json.loads(os.environ.pop("REWORKBENCH_WORKER_CONFIG"))
    config = EngineConfig(*(Path(config_data[key]) if config_data.get(key) else None for key in ("ghidra_home", "java_home")))
    connection = Client(("127.0.0.1", int(os.environ.pop("REWORKBENCH_WORKER_PORT"))), authkey=auth)
    sends = threading.Lock()
    active_lock = threading.Lock()
    stop = threading.Event()
    commands = queue.Queue(maxsize=256)
    active = {"id": None, "cancel": None}
    cancelled = set()

    def send(message):
        with sends:
            connection.send_bytes(encode(message))

    def receive():
        try:
            while not stop.is_set():
                message = decode(connection.recv_bytes(MAX_MESSAGE))
                if message.operation == "cancel":
                    target = message.payload.get("target_request_id")
                    with active_lock:
                        if active["id"] == target:
                            active["cancel"].cancel()
                        else:
                            cancelled.add(target)
                else:
                    commands.put(message)
        except (EOFError, OSError, ValueError):
            stop.set()
            with active_lock:
                if active["cancel"]:
                    active["cancel"].cancel()

    def heartbeat():
        while not stop.wait(5):
            try:
                send(Envelope(1, "heartbeat", None, "heartbeat", {"active_request_id": active["id"]}))
            except OSError:
                stop.set()

    threading.Thread(target=receive, daemon=True).start()
    threading.Thread(target=heartbeat, daemon=True).start()
    backend = None
    try:
        backend = backend_factory(config)
        send(Envelope(1, "hello", None, "hello", {"ready": True, "pid": os.getpid()}))
        while not stop.is_set():
            try:
                request = commands.get(timeout=0.2)
            except queue.Empty:
                continue
            cancel = Cancellation()
            with active_lock:
                active.update(id=request.request_id, cancel=cancel)
                if request.request_id in cancelled:
                    cancelled.remove(request.request_id)
                    cancel.cancel()
            try:
                args = dict(request.payload)
                if request.operation == "open":
                    args["analysis_dir"] = Path(args["analysis_dir"])
                    args["sample_path"] = Path(args["sample_path"])
                    args["options"] = ImportOptions.from_json(args["options"])
                if request.operation == "inspect":
                    args["sample_path"] = Path(args["sample_path"])
                if "address" in args:
                    args["address"] = Address.from_json(args["address"])
                if request.operation in ("open", "analyze", "decompile", "rename"):
                    args["cancel"] = cancel
                if request.operation not in {"catalog", "inspect", "open", "analyze", "page", "decompile", "rename", "save", "close"}:
                    raise ValueError("Operação não suportada.")
                result = getattr(backend, request.operation)(**args)
                if isinstance(result, Page):
                    result = {"items": list(result.items), "offset": result.offset, "total": result.total}
                elif hasattr(result, "to_json"):
                    result = result.to_json()
                send(Envelope(1, request.request_id, request.analysis_id, request.operation, result or {}))
                if request.operation == "close":
                    break
            except Exception as exc:
                traceback.print_exc()
                send(Envelope(1, request.request_id, request.analysis_id, request.operation, {}, {"code": getattr(exc, "code", "cancelled" if cancel.cancelled() else "engine_error"), "message": str(exc), "details": type(exc).__name__}))
            finally:
                with active_lock:
                    active.update(id=None, cancel=None)
    except Exception as exc:
        traceback.print_exc()
        send(Envelope(1, "hello", None, "hello", {}, {"code": "startup", "message": str(exc), "details": type(exc).__name__}))
    finally:
        stop.set()
        if backend:
            backend.close()
        connection.close()


def main():
    from reworkbench.engine.backend import GhidraBackend
    serve(GhidraBackend)


if __name__ == "__main__":
    main()
