"""Windows smoke check of the real desktop executable and its close path."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import time

import pytest

ROOT = Path(__file__).resolve().parents[2]
GUI = Path(os.environ.get("REWORKBENCH_NATIVE_GUI", ROOT / "build/bin/reworkbench.exe"))
pytestmark = pytest.mark.skipif(os.name != "nt" or not GUI.is_file(), reason="Requires the Windows native GUI build")


def test_desktop_opens_and_closes_cleanly():
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    environment = dict(os.environ, QT_QPA_PLATFORM="windows")
    process = subprocess.Popen([str(GUI)], env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    windows = []

    @callback_type
    def find_window(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == process.pid and user32.IsWindowVisible(handle):
            title = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(handle, title, len(title))
            if title.value == "Rework":
                windows.append(handle)
        return True

    try:
        deadline = time.monotonic() + 30
        while not windows and time.monotonic() < deadline and process.poll() is None:
            user32.EnumWindows(find_window, 0)
            if not windows:
                time.sleep(0.1)
        assert windows, f"No main window; exit={process.poll()}"
        assert user32.PostMessageW(windows[0], 0x0010, 0, 0)  # WM_CLOSE, normal cleanup
        stdout, stderr = process.communicate(timeout=30)
        assert process.returncode == 0, (stdout, stderr)
        assert b"QSqlDatabase" not in stderr and b"Cannot load" not in stderr, stderr
    finally:
        if process.poll() is None:
            process.terminate()
            process.communicate(timeout=10)
