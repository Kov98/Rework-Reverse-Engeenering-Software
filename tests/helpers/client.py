import threading
import time

from reworkbench.engine.config import discover_config, validate_engine
from reworkbench.engine.supervisor import WorkerSupervisor


class EngineClient:
    def __init__(self):
        self.condition = threading.Condition()
        self.responses = {}
        config = discover_config()
        diagnostic = validate_engine(config)
        assert diagnostic["ready"], diagnostic
        self.supervisor = WorkerSupervisor(config, self._receive)
        self.supervisor.start()

    def _receive(self, message):
        with self.condition:
            self.responses[message.request_id] = message
            self.condition.notify_all()

    def call(self, operation, payload=None, analysis_id="integration", timeout=120):
        request_id = self.supervisor.submit(operation, analysis_id, payload or {})
        until = time.monotonic() + timeout
        with self.condition:
            while request_id not in self.responses:
                remaining = until - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"{operation}; log={self.supervisor.log_path}")
                self.condition.wait(remaining)
            response = self.responses.pop(request_id)
        if response.error:
            raise RuntimeError(response.error)
        return response.payload

    def close(self):
        self.supervisor.close()
