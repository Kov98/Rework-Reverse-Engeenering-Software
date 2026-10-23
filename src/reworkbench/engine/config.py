from __future__ import annotations

import importlib.util
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from reworkbench.domain import EngineConfig


def discover_config() -> EngineConfig:
    data = {}
    for parent in (Path.cwd(), *Path(__file__).resolve().parents):
        path = parent / ".tools" / "environment.json"
        if path.is_file():
            try:
                data = json.loads(path.read_text(encoding="utf-8-sig"))
                break
            except (OSError, ValueError):
                continue
    ghidra = os.environ.get("REWORKBENCH_GHIDRA_HOME") or os.environ.get("GHIDRA_INSTALL_DIR") or data.get("ghidra_home")
    java = os.environ.get("REWORKBENCH_JAVA_HOME") or data.get("java_home") or os.environ.get("JAVA_HOME")
    return EngineConfig(Path(ghidra) if ghidra else None, Path(java) if java else None)


def validate_engine(config: EngineConfig) -> dict:
    issues = []
    versions = {"python": sys.version.split()[0]}

    def issue(code, message):
        issues.append({"code": code, "message": message})

    properties = config.ghidra_home / "Ghidra/application.properties" if config.ghidra_home else None
    if properties is None or not properties.is_file():
        issue("ghidra_missing", "Indique a pasta do Ghidra 12.1.4 em Configurações.")
    else:
        text = properties.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"^application\.version=(.+)$", text, re.MULTILINE)
        versions["ghidra"] = match.group(1).strip() if match else "desconhecida"
        if versions["ghidra"] != "12.1.4":
            issue("ghidra_version", "Esta versão foi validada com Ghidra 12.1.4; configure essa instalação.")
    java = config.java_home / "bin/java.exe" if config.java_home else None
    javac = config.java_home / "bin/javac.exe" if config.java_home else None
    if java is None or not java.is_file() or not javac.is_file():
        issue("jdk_missing", "Indique um JDK 21 de 64 bits completo (com java e javac).")
    else:
        try:
            result = subprocess.run([str(java), "-version"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            output = result.stdout + result.stderr
            match = re.search(r'version\s+"(\d+)(?:\.(\d+))?', output)
            major = int(match.group(2) if match and match.group(1) == "1" else match.group(1)) if match else 0
            versions["java"] = major
            if result.returncode or major != 21 or "64-Bit" not in output:
                issue("java_version", "O motor requer JDK 21 de 64 bits; o Java selecionado é incompatível.")
        except (OSError, subprocess.SubprocessError) as exc:
            issue("java_unavailable", f"Não foi possível consultar o JDK: {exc}")
    if importlib.util.find_spec("pyghidra") is None:
        issue("pyghidra_missing", "Instale PyGhidra no ambiente virtual conforme o README.")
    return {"ready": not issues, "issues": issues, "versions": versions, "ghidra_home": str(config.ghidra_home) if config.ghidra_home else None, "java_home": str(config.java_home) if config.java_home else None}


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--ghidra-home", type=Path)
    parser.add_argument("--java-home", type=Path)
    args = parser.parse_args()
    defaults = discover_config()
    diagnostic = validate_engine(EngineConfig(args.ghidra_home or defaults.ghidra_home, args.java_home or defaults.java_home))
    print(json.dumps(diagnostic, ensure_ascii=False, indent=2))
    raise SystemExit(0 if diagnostic["ready"] else 1)
