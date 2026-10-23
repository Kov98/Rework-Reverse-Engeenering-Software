"""Streaming byte statistics; input samples are opened only as data."""

from __future__ import annotations

import json
import math
import os
import stat
import subprocess
import tempfile
import time
from concurrent.futures import CancelledError
from pathlib import Path

from reworkbench.domain import Cancellation

_CHUNK_SIZE = 64 * 1024
_VALUE_LIMIT = 4096
_NATIVE_TIMEOUT_S = 300.0


def _check_cancel(cancel: Cancellation | None) -> None:
    if cancel is not None and cancel.cancelled():
        raise CancelledError("Byte scan cancelled.")


def _validate_result(result: object, min_length: int, max_strings: int) -> dict:
    """Treat output from the optional process as an untrusted protocol message."""
    if not isinstance(result, dict) or set(result) != {"size", "entropy", "strings", "strings_truncated"}:
        raise ValueError("Invalid bytecore result.")
    size, entropy, strings = result["size"], result["entropy"], result["strings"]
    if type(size) is not int or size < 0:
        raise ValueError("Invalid bytecore size.")
    if type(entropy) not in (int, float) or not math.isfinite(entropy) or not 0 <= entropy <= 8:
        raise ValueError("Invalid bytecore entropy.")
    if type(result["strings_truncated"]) is not bool or not isinstance(strings, list) or len(strings) > max_strings:
        raise ValueError("Invalid bytecore string collection.")
    previous_end = -1
    for item in strings:
        if not isinstance(item, dict) or set(item) != {"offset", "value"}:
            raise ValueError("Invalid bytecore string.")
        offset, value = item["offset"], item["value"]
        if type(offset) is not int or offset <= previous_end or not isinstance(value, str):
            raise ValueError("Invalid bytecore string location.")
        if not min(min_length, _VALUE_LIMIT) <= len(value) <= _VALUE_LIMIT:
            raise ValueError("Invalid bytecore string length.")
        if any(not 0x20 <= ord(character) <= 0x7E for character in value):
            raise ValueError("Invalid bytecore string encoding.")
        if offset < 0 or offset + max(min_length, len(value)) > size:
            raise ValueError("Bytecore string exceeds file size.")
        previous_end = offset + len(value) - 1
    return result


def _stop_child(process: subprocess.Popen) -> None:
    if process.poll() is None:
        try:
            process.terminate()
        except OSError:
            pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def _native_scan(path: Path, executable: Path, min_length: int, max_strings: int, cancel: Cancellation | None) -> dict:
    # Disk-backed output avoids a PIPE deadlock and unbounded pipe buffering while
    # the worker is running. Only a bounded, validated JSON message is read back.
    output_limit = max_strings * (_VALUE_LIMIT * 6 + 128) + 2048
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        _check_cancel(cancel)
        process = subprocess.Popen(
            [str(executable), str(path), "--min-length", str(min_length), "--max-strings", str(max_strings)],
            stdin=subprocess.DEVNULL,
            stdout=output,
            stderr=errors,
            shell=False,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        deadline = time.monotonic() + _NATIVE_TIMEOUT_S
        try:
            while True:
                _check_cancel(cancel)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("Bytecore timed out.")
                try:
                    returncode = process.wait(timeout=min(0.1, remaining))
                    break
                except subprocess.TimeoutExpired:
                    continue
            _check_cancel(cancel)
            if returncode != 0:
                raise ValueError("Bytecore did not complete successfully.")
            if os.fstat(output.fileno()).st_size > output_limit:
                raise ValueError("Bytecore output exceeds its configured limit.")
            output.seek(0)
            raw = output.read(output_limit + 1)
            if len(raw) > output_limit:
                raise ValueError("Bytecore output exceeds its configured limit.")
            return _validate_result(json.loads(raw), min_length, max_strings)
        finally:
            _stop_child(process)


def _python_scan(path: Path, min_length: int, max_strings: int, cancel: Cancellation | None) -> dict:
    histogram = [0] * 256
    strings = []
    size = run_start = run_length = 0
    prefix = bytearray()
    truncated = False

    def finish_run() -> None:
        nonlocal truncated
        if run_length >= min_length:
            if len(strings) < max_strings:
                strings.append({"offset": run_start, "value": prefix.decode("ascii")})
                if run_length > _VALUE_LIMIT:
                    truncated = True
            else:
                truncated = True
        prefix.clear()

    with path.open("rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise ValueError("Input must be a regular file.")
        while True:
            _check_cancel(cancel)
            block = stream.read(_CHUNK_SIZE)
            if not block:
                break
            for index, byte in enumerate(block):
                if index % 16384 == 0:
                    _check_cancel(cancel)
                histogram[byte] += 1
                if 0x20 <= byte <= 0x7E:
                    if run_length == 0:
                        run_start = size
                    run_length += 1
                    if len(strings) < max_strings and len(prefix) < _VALUE_LIMIT:
                        prefix.append(byte)
                else:
                    finish_run()
                    run_length = 0
                size += 1
    finish_run()
    _check_cancel(cancel)
    entropy = -sum((count / size) * math.log2(count / size) for count in histogram if count) if size else 0.0
    return {"size": size, "entropy": max(0.0, min(8.0, entropy)), "strings": strings, "strings_truncated": truncated}


def scan_bytes(
    path: Path,
    min_length: int = 4,
    max_strings: int = 10000,
    cancel: Cancellation | None = None,
    native_executable: Path | None = None,
) -> dict:
    """Read a regular file, returning whole-file entropy and bounded ASCII runs.

    ``strings_truncated`` signals omitted runs or a retained run longer than
    4096 characters. ``max_strings=0`` collects statistics without string values.
    A caller may explicitly select an already installed bytecore executable;
    unavailable, failed, timed-out or malformed accelerators use the fallback.
    Cancellation raises ``concurrent.futures.CancelledError`` in either path.
    """
    if type(min_length) is not int or min_length < 1:
        raise ValueError("min_length must be a positive integer.")
    if type(max_strings) is not int or max_strings < 0:
        raise ValueError("max_strings must be a nonnegative integer.")
    _check_cancel(cancel)
    path = Path(path).resolve(strict=True)
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError("Input must be a regular file.")
    if native_executable is not None:
        try:
            executable = Path(native_executable).resolve(strict=True)
            # On Windows only a native .exe is accepted: batch files may invoke
            # a command interpreter even when subprocess shell=False is used.
            usable = executable.is_file() and (os.name != "nt" or executable.suffix.lower() == ".exe")
            if usable and not executable.samefile(path):
                result = _native_scan(path, executable, min_length, max_strings, cancel)
                _check_cancel(cancel)
                return result
        except (OSError, ValueError, TimeoutError, OverflowError, RecursionError):
            _check_cancel(cancel)
    return _python_scan(path, min_length, max_strings, cancel)
