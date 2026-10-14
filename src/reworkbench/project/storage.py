from __future__ import annotations

import hashlib
import os
import uuid
from concurrent.futures import CancelledError
from pathlib import Path

from reworkbench.domain import Cancellation, Sample


class ProjectError(RuntimeError):
    pass


def contained_path(root: Path, relative: str) -> Path:
    target = (root / relative).resolve()
    if not target.is_relative_to(root.resolve()):
        raise ProjectError("Caminho escapa do diretório do projeto.")
    return target


def copy_sample(root: Path, source: Path, cancel: Cancellation) -> Sample:
    folder = contained_path(root, "samples")
    folder.mkdir(exist_ok=True)
    temporary = folder / f"{uuid.uuid4().hex}.partial"
    before = source.stat()
    digest = hashlib.sha256()
    size = 0
    try:
        with source.open("rb") as incoming, temporary.open("xb") as outgoing:
            while True:
                if cancel.cancelled():
                    raise CancelledError("Importação cancelada.")
                block = incoming.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
                size += len(block)
                outgoing.write(block)
            outgoing.flush()
            os.fsync(outgoing.fileno())
        after = source.stat()
        signature = lambda s: (s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
        if signature(before) != signature(after) or size != before.st_size:
            raise ProjectError("O arquivo de origem foi alterado durante a importação.")
        if cancel.cancelled():
            raise CancelledError("Importação cancelada.")
        sha256 = digest.hexdigest()
        relative = f"samples/{sha256}.bin"
        destination = contained_path(root, relative)
        os.replace(temporary, destination)
        return Sample(sha256, size, relative)
    finally:
        temporary.unlink(missing_ok=True)


class ProjectLock:
    def __init__(self, root: Path):
        self.stream = (root / ".project.lock").open("a+b")
        self.stream.seek(0, os.SEEK_END)
        if self.stream.tell() == 0:
            self.stream.write(b"\0")
            self.stream.flush()
        self.stream.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self.stream.close()
            raise ProjectError("O projeto já está em uso por outro processo.") from exc

    def close(self):
        if self.stream.closed:
            return
        self.stream.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(self.stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(self.stream.fileno(), fcntl.LOCK_UN)
        self.stream.close()
