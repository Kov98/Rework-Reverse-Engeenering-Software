"""Adapt hidden Windows paths to Ghidra's project URL name restrictions."""
import hashlib
import os
import subprocess
from pathlib import Path


def ghidra_project_path(path: Path, cleanup) -> Path:
    if not any(part.startswith(".") for part in path.parts):
        return path
    base = Path(os.environ["LOCALAPPDATA"]) / "REWorkbench" / "project-links"
    base.mkdir(parents=True, exist_ok=True)
    name = hashlib.sha256(str(path.resolve()).encode("utf-8")).hexdigest()[:24] + f"-{os.getpid()}"
    alias = base / name
    if os.path.lexists(alias):
        if not alias.is_junction() or alias.resolve() != path.resolve():
            raise RuntimeError("Alias de projeto já existe com destino incompatível.")
    else:
        environment = dict(os.environ, REWORKBENCH_LINK_PATH=str(alias), REWORKBENCH_LINK_TARGET=str(path))
        script = "$ErrorActionPreference='Stop'; New-Item -ItemType Junction -Path $env:REWORKBENCH_LINK_PATH -Target $env:REWORKBENCH_LINK_TARGET | Out-Null"
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-WindowStyle", "Hidden", "-Command", script], env=environment, capture_output=True, timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode and not (alias.is_junction() and alias.resolve() == path.resolve()):
            details = result.stderr.decode('utf-8', errors='replace').strip()
            raise RuntimeError(f'Não foi possível criar o alias de projeto para o Ghidra: {details}')

    def release():
        # rmdir removes only the junction, never its target or contents.
        if alias.is_junction():
            try:
                alias.rmdir()
            except OSError:
                pass

    cleanup(release)
    return alias
