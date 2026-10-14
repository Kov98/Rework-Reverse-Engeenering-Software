"""A deliberately slow external engine for process supervision tests only."""
import os
import subprocess
import sys
import time

from reworkbench.engine.worker import serve


class SlowBackend:
    def __init__(self, config):
        print("engine log: this is not JSON", flush=True)

    def page(self, kind, offset=0, limit=200, query=""):
        from reworkbench.domain import Page
        return Page(({"key": "echo", "name": query},), offset, 1)

    def analyze(self, timeout_s, cancel):
        if timeout_s == 999:
            process = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(120)"], creationflags=subprocess.CREATE_NO_WINDOW)
            return {"grandchild_pid": process.pid}
        until = time.monotonic() + 20
        while time.monotonic() < until:
            if timeout_s != 888 and cancel.cancelled():
                return {"state": "cancelled"}
            time.sleep(0.01)
        return {"state": "ready"}

    def close(self):
        pass


if "--bad-auth" in sys.argv:
    os.environ["REWORKBENCH_WORKER_AUTH"] = "00" * 32
serve(SlowBackend)
