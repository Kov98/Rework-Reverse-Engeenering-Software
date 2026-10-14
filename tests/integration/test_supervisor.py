import ctypes
import queue
import sys
import time
from pathlib import Path

import pytest

from reworkbench.domain import EngineConfig
from reworkbench.engine.supervisor import WorkerSupervisor

HELPER = Path(__file__).resolve().parents[1] / "helpers/worker_process.py"


def receive(events, request_id, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        event = events.get(timeout=max(0.01, until-time.monotonic()))
        if event.request_id == request_id:
            return event
    raise TimeoutError(request_id)


@pytest.fixture
def worker():
    events = queue.Queue()
    worker = WorkerSupervisor(EngineConfig(), events.put, worker_command=[sys.executable, str(HELPER)], startup_timeout_s=10)
    worker.start()
    yield worker, events
    worker.close()


def test_engine_logs_are_not_protocol(worker):
    supervisor, events = worker
    request_id = supervisor.submit("page", "a", {"kind": "functions", "query": "ação"})
    result = receive(events, request_id)
    assert result.error is None
    assert result.payload["items"][0]["name"] == "ação"
    assert "not JSON" in supervisor.log_path.read_text(encoding="utf-8")


def test_cancel_while_operation_blocks(worker):
    supervisor, events = worker
    request_id = supervisor.submit("analyze", "a", {"timeout_s": 300})
    supervisor.cancel(request_id)
    result = receive(events, request_id)
    assert result.payload["state"] == "cancelled"


def test_bad_auth_is_bounded():
    supervisor = WorkerSupervisor(EngineConfig(), lambda event: None, worker_command=[sys.executable, str(HELPER), "--bad-auth"], startup_timeout_s=2)
    with pytest.raises(RuntimeError):
        supervisor.start()
    supervisor.close()


def test_heartbeat_does_not_extend_operation_deadline():
    events = queue.Queue()
    clock = [0.0]
    supervisor = WorkerSupervisor(EngineConfig(), events.put, worker_command=[sys.executable, str(HELPER)], clock=lambda: clock[0], startup_timeout_s=10, idle_timeout_s=10000, cancel_grace_s=0.2)
    try:
        supervisor.start()
        request_id = supervisor.submit("analyze", "a", {"timeout_s": 888})
        clock[0] = 1000
        time.sleep(0.2)
        clock[0] = 1100
        result = receive(events, request_id)
        assert result.error is not None
        assert result.error["code"] == "timeout"
        assert supervisor.process.poll() is not None
    finally:
        supervisor.close()


def test_windows_job_kills_grandchild(worker):
    supervisor, events = worker
    request_id = supervisor.submit("analyze", "a", {"timeout_s": 999})
    pid = receive(events, request_id).payload["grandchild_pid"]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    handle = kernel.OpenProcess(0x100000, False, pid)
    assert handle
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    try:
        supervisor.close()
        assert kernel.WaitForSingleObject(handle, 3000) == 0
    finally:
        kernel.CloseHandle(handle)
