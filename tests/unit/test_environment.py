from pathlib import Path
from subprocess import CompletedProcess

from reworkbench.domain import EngineConfig
from reworkbench.engine.config import validate_engine


def test_missing_engine_has_actionable_issue():
    result = validate_engine(EngineConfig())
    assert result["ready"] is False
    assert any("Ghidra" in issue["message"] for issue in result["issues"])


def test_java8_is_rejected(tmp_path, monkeypatch):
    java = tmp_path / "jdk"
    (java / "bin").mkdir(parents=True)
    (java / "bin/java.exe").touch()
    (java / "bin/javac.exe").touch()
    monkeypatch.setattr("reworkbench.engine.config.subprocess.run", lambda args, **kwargs: CompletedProcess(args, 0, "", 'java version "1.8.0_351"\n64-Bit'))
    result = validate_engine(EngineConfig(None, java))
    assert result["ready"] is False
    assert any(issue["code"] == "java_version" for issue in result["issues"])


def test_paths_are_not_shell_commands(tmp_path, monkeypatch):
    java = tmp_path / "jdk spaces & echo"
    (java / "bin").mkdir(parents=True)
    (java / "bin/java.exe").touch()
    (java / "bin/javac.exe").touch()
    calls = []

    def run(args, **kwargs):
        calls.append((args, kwargs))
        return CompletedProcess(args, 0, "", 'openjdk version "21.0.12"\n64-Bit')

    monkeypatch.setattr("reworkbench.engine.config.subprocess.run", run)
    validate_engine(EngineConfig(None, java))
    assert len(calls) == 1
    assert isinstance(calls[0][0], list)
    assert calls[0][0][0] == str(java / "bin/java.exe")
    assert not calls[0][1].get("shell", False)
