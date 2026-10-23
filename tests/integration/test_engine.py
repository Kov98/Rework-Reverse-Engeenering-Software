import json
from pathlib import Path

import pytest

from tests.helpers.client import EngineClient

FIXTURES = Path(__file__).resolve().parents[1] / "fixtures"
MANIFEST = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
BY_ID = {f["id"]: f for f in MANIFEST["fixtures"]}


@pytest.fixture(scope="module")
def engine():
    client = EngineClient()
    yield client
    client.close()


def options_for(fixture):
    return {key: fixture[key] for key in ("loader_id", "language_id", "compiler_id", "image_base", "entries") if key in fixture}


def load(engine, tmp_path, identifier, extra=None):
    fixture = BY_ID[identifier]
    options = options_for(fixture)
    options.update(extra or {})
    folder = tmp_path / identifier
    folder.mkdir(exist_ok=True)
    engine.call("open", {"analysis_dir": str(folder), "sample_path": str(FIXTURES / fixture["path"]), "options": options})
    summary = engine.call("analyze", {"timeout_s": 60})
    assert summary["state"] == "ready", summary
    return folder


@pytest.mark.parametrize("identifier", ["pe32", "pe64", "elf_aarch64", "macho_x64", "arm", "thumb", "mips"])
def test_real_import_and_pseudocode(engine, tmp_path, identifier):
    load(engine, tmp_path, identifier)
    functions = engine.call("page", {"kind": "functions"})["items"]
    assert functions
    function = next((f for f in functions if "compute" in f["name"]), functions[0])
    result = engine.call("decompile", {"address": function["address"], "timeout_s": 30})
    assert result["code"] and result["error"] is None, result
    instructions = engine.call("page", {"kind": "assembly"})["items"]
    assert instructions and all("mnemonic" in row for row in instructions)
    if identifier.startswith("pe"):
        imports = engine.call("page", {"kind": "imports"})["items"]
        assert any("strcpy" in row["name"] for row in imports)
        strings = engine.call("page", {"kind": "strings"})["items"]
        assert any("reworkbench fixture" in row["value"] for row in strings)
        assert engine.call("page", {"kind": "xrefs"})["items"]
    else:
        assert result["address"]["space"] == "ram"


def test_raw_requires_explicit_configuration(engine, tmp_path):
    with pytest.raises(RuntimeError, match="configura"):
        engine.call("open", {"analysis_dir": str(tmp_path / "raw"), "sample_path": str(FIXTURES / BY_ID["arm"]["path"]), "options": {}})


def test_thumb_context_is_preserved(engine, tmp_path):
    load(engine, tmp_path, "thumb")
    instructions = engine.call("page", {"kind": "assembly"})["items"]
    assert instructions[0]["address"]["value"] == "0x1000"
    assert len(instructions[0]["bytes"].replace(" ", "")) == 4


def test_fat_macho_requires_slice_choice(engine, tmp_path):
    source = FIXTURES / BY_ID["macho_fat"]["path"]
    inspected = engine.call("inspect", {"sample_path": str(source)})
    assert len(inspected["choices"]) == 2
    with pytest.raises(RuntimeError, match="imagem"):
        load(engine, tmp_path, "macho_fat")
    load(engine, tmp_path, "macho_fat", {"load_spec_id": inspected["choices"][0]["id"]})
    assert engine.call("page", {"kind": "functions"})["items"]


def test_rename_survives_new_worker(engine, tmp_path):
    folder = load(engine, tmp_path, "pe64")
    function = next(f for f in engine.call("page", {"kind": "functions"})["items"] if "compute" in f["name"])
    engine.call("rename", {"address": function["address"], "name": "funcao_investigada"})
    engine.call("open", {"analysis_dir": str(tmp_path / "release_previous"), "sample_path": str(FIXTURES / BY_ID["pe32"]["path"]), "options": {}})
    other = EngineClient()
    try:
        other.call("open", {"analysis_dir": str(folder), "sample_path": str(FIXTURES / BY_ID["pe64"]["path"]), "options": {}})
        assert any(f["name"] == "funcao_investigada" for f in other.call("page", {"kind": "functions"})["items"])
    finally:
        other.close()


def test_unbacked_region_has_no_file_offset(engine, tmp_path):
    load(engine, tmp_path, "elf_aarch64")
    regions = engine.call("page", {"kind": "regions"})["items"]
    assert any(r["file_offset"] is None for r in regions)


def test_failure_of_one_function_does_not_poison_next(engine, tmp_path):
    load(engine, tmp_path, "pe32")
    result = engine.call("decompile", {"address": {"space": "ram", "value": "0x0"}, "timeout_s": 30})
    assert result["error"] and result["code"] is None
    function = engine.call("page", {"kind": "functions"})["items"][0]
    assert engine.call("decompile", {"address": function["address"], "timeout_s": 30})["code"]
