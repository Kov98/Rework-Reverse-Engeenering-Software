"""Integration checks against the actual C++ CLI, never the legacy engine.

Set REWORKBENCH_NATIVE_CLI to select a build. Its Qt/compiler runtime DLLs
must be deployed beside it or available through PATH.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("REWORKBENCH_NATIVE_CLI", ROOT / "build/bin/reworkbench-cli.exe"))
FIXTURES = ROOT / "tests/fixtures/bin"
pytestmark = pytest.mark.skipif(not CLI.is_file(), reason="Build the native CLI first")


def invoke(*args, request=None, success=True):
    command = [str(CLI), *map(str, args)]
    if request is not None:
        command += ["--request", "-"]
    completed = subprocess.run(
        command, input=json.dumps(request) if request is not None else None,
        capture_output=True, text=True, encoding="utf-8", timeout=45,
    )
    assert completed.returncode == (0 if success else 1), (completed.stdout, completed.stderr)
    result = json.loads(completed.stdout)
    assert result["ok"] is success, result
    if not success:
        assert result["error"]
    return result


@pytest.mark.parametrize("name,format_,arch", [
    ("pe32.exe", "PE", "x86"),
    ("pe64.exe", "PE", "x86-64"),
    ("aarch64.elf", "ELF", "AArch64"),
    ("macho_x86_64", "Mach-O", "x86-64"),
    ("macho_aarch64", "Mach-O", "AArch64"),
])
def test_inspect_real_format_and_architecture(name, format_, arch):
    result = invoke("inspect", "--input", FIXTURES / name)
    assert result["format"] == format_
    assert result["architecture"] == arch


def test_universal_requires_explicit_slice():
    result = invoke("inspect", "--input", FIXTURES / "universal.macho")
    assert result["selection_required"] is True
    assert len(result["slices"]) == 2
    selected = invoke("inspect", "--input", FIXTURES / "universal.macho", "--slice", "0")
    # The container remains universal; selection chooses the inner architecture.
    assert selected["format"] == "Mach-O universal"
    assert selected["architecture"] == "x86-64"
    invoke("inspect", "--input", FIXTURES / "universal.macho", "--slice", "2", success=False)


def test_truncated_pe_is_rejected(tmp_path):
    sample = tmp_path / "truncated.exe"
    sample.write_bytes(b"MZ" + b"\0" * 8)
    invoke("inspect", "--input", sample, success=False)


def test_raw_requires_architecture(tmp_path):
    sample = tmp_path / "code.bin"
    sample.write_bytes(bytes.fromhex("b82a00000083c001c3"))
    invoke("analyze", "--input", sample, "--raw", success=False)


@pytest.fixture
def raw_project(tmp_path):
    sample = tmp_path / "código.bin"
    # mov eax,42; add eax,1; ret. Expected result is independently 43.
    sample.write_bytes(bytes.fromhex("b82a00000083c001c3"))
    project = tmp_path / "investigação"
    result = invoke("analyze", "--input", sample, "--raw", "--arch", "x86-64",
                    "--base", "0x1000", "--entry", "0x1000", "--project", project,
                    "--create-project")
    return sample, project, result


def require_decoder():
    if not invoke("capabilities")["decoder"].startswith("Capstone 5."):
        pytest.skip("Requires the Capstone-enabled build")


def test_project_reopens_and_reuses_analysis(raw_project):
    sample, project, first = raw_project
    assert first["sha256"] == hashlib.sha256(sample.read_bytes()).hexdigest()
    reopened = invoke("analyze", "--project", project)
    assert reopened["analysis_id"] == first["analysis_id"]
    reused = invoke("analyze", "--project", project, "--input", sample, "--raw",
                    "--arch", "x86-64", "--base", "0x1000", "--entry", "0x1000")
    # Partial snapshots must be reanalyzed rather than accepted as ready cache.
    assert (reused["analysis_id"] == first["analysis_id"]) is (first["state"] == "ready")


def test_project_survives_move(raw_project, tmp_path):
    _, project, first = raw_project
    moved = tmp_path / "movido"
    shutil.move(project, moved)
    result = invoke("analyze", "--project", moved)
    assert result["sha256"] == first["sha256"]
    assert result["analysis_id"] == first["analysis_id"]


def test_project_rejects_modified_sample(raw_project):
    _, project, _ = raw_project
    copied = next((project / "samples").iterdir())
    data = bytearray(copied.read_bytes())
    data[0] ^= 1
    copied.write_bytes(data)
    invoke("analyze", "--project", project, success=False)


def test_known_scalar_function_emulates_to_43(raw_project):
    require_decoder()
    _, project, result = raw_project
    assert result["functions"] >= 1
    output = invoke("emulate", "--project", project, "--function", "0x1000",
                    request={"initial": {"max_steps": 16}})
    assert output["status"] == "returned"
    assert output["registers"]["rax"]["fully_known"] is True
    assert int(output["registers"]["rax"]["value"], 16) == 43


def test_decompilation_is_repeatable_through_cache(raw_project):
    require_decoder()
    _, project, _ = raw_project
    first = invoke("decompile", "--project", project, "--function", "0x1000")
    assert first["total"] == 3
    assert first["modeled"] == 3
    assert first["pseudocode"] and first["ssa"]
    assert invoke("decompile", "--project", project, "--function", "0x1000") == first


@pytest.mark.parametrize("argument,expected", [(0, 9), (1, 7)])
def test_conditional_control_flow_uses_known_flags(tmp_path, argument, expected):
    require_decoder()
    sample = tmp_path / "diamond.bin"
    # test edi,edi; je else; mov eax,7; jmp end; else: mov eax,9; end: ret
    sample.write_bytes(bytes.fromhex("85ff7407b807000000eb05b809000000c3"))
    result = invoke("emulate", "--input", sample, "--raw", "--arch", "x86-64",
                    "--base", "0x1000", "--entry", "0x1000",
                    request={"initial": {"registers": {"rdi": hex(argument)}, "max_steps": 16}})
    assert result["status"] == "returned"
    assert int(result["registers"]["rax"]["value"], 16) == expected


def test_interpreter_stops_on_unknown_input(tmp_path):
    require_decoder()
    sample = tmp_path / "unknown.bin"
    sample.write_bytes(bytes.fromhex("4883c001c3"))  # add rax,1; ret
    result = invoke("emulate", "--input", sample, "--raw", "--arch", "x86-64",
                    "--base", "0x1000", request={"initial": {"max_steps": 16}})
    assert result["status"] == "stopped"
    assert result["steps"] == 0


def test_interpreter_bounds_infinite_loop(tmp_path):
    require_decoder()
    sample = tmp_path / "loop.bin"
    sample.write_bytes(bytes.fromhex("ebfe"))  # jmp to itself
    result = invoke("emulate", "--input", sample, "--raw", "--arch", "x86-64",
                    "--base", "0x1000", request={"initial": {"max_steps": 3}})
    assert result["status"] == "stopped"
    assert result["reason"] == "step-limit"
    assert result["steps"] == 3


def test_patch_writes_copy_and_preserves_source(tmp_path):
    source, output = tmp_path / "source.bin", tmp_path / "patched.bin"
    source.write_bytes(b"abcdef")
    invoke("patch", "--input", source, "--output", output,
           request={"patches": [{"offset": "0x2", "expected": "6364", "replacement": "5859"}]})
    assert source.read_bytes() == b"abcdef"
    assert output.read_bytes() == b"abXYef"


def test_patch_rejects_wrong_expected_bytes_without_writing(tmp_path):
    source, output = tmp_path / "source.bin", tmp_path / "patched.bin"
    source.write_bytes(b"abcdef")
    invoke("patch", "--input", source, "--output", output, success=False,
           request={"patches": [{"offset": "0x2", "expected": "0000", "replacement": "5859"}]})
    assert not output.exists()
    assert source.read_bytes() == b"abcdef"


def test_patch_rejects_source_as_destination(tmp_path):
    source = tmp_path / "source.bin"
    source.write_bytes(b"abcdef")
    invoke("patch", "--input", source, "--output", source, success=False,
           request={"patches": [{"offset": "0x2", "expected": "6364", "replacement": "5859"}]})
    assert source.read_bytes() == b"abcdef"


def test_debugger_requires_explicit_execution_consent():
    invoke("debug", request={"mode": "launch", "executable": "does-not-exist.exe"}, success=False)


@pytest.mark.skipif(os.name != "nt", reason="Windows debugger")
def test_debugger_launches_fixture_reads_registers_and_detaches():
    target = CLI.parent / "reworkbench-test-target.exe"
    if not target.is_file():
        pytest.skip("Enable REWORKBENCH_BUILD_TEST_FIXTURES")
    result = invoke("debug", request={
        "execute_target": True, "executable": str(target.resolve()),
        "operations": [{"op": "wait", "timeout_ms": 5000}, {"op": "registers"}, {"op": "detach"}],
    })
    assert "error" not in result, result
    assert result["detached"] is True
    assert int(result["process_id"]) > 0
    registers = next(event["values"] for event in result["events"] if event["kind"] == "registers")
    assert registers


def test_trusted_example_extension_obeys_hash_and_opt_in():
    candidates = list(CLI.parent.glob("*reworkbench_example_extension.dll"))
    if not candidates:
        pytest.skip("Enable REWORKBENCH_BUILD_EXAMPLE_EXTENSION")
    extension = candidates[0]
    sha = hashlib.sha256(extension.read_bytes()).hexdigest()
    invoke("plugin-run", "--input", extension, "--sha256", sha, "--command", "describe", success=False)
    invoke("plugin-run", "--input", extension, "--sha256", "0" * 64,
           "--command", "describe", "--allow-plugin", success=False)
    result = invoke("plugin-run", "--input", extension, "--sha256", sha,
                    "--command", "describe", "--allow-plugin", request={"arguments": {"test": True}})
    assert result["result"]["id"] == "org.reworkbench.example"
    assert result["result"]["received_argument_fields"] == 1


def test_list_rejects_negative_count(raw_project):
    _, project, _ = raw_project
    invoke("functions", "--project", project, "--count", "-1", success=False)



def test_abi_hint_windows_x64_pe():
    hint = invoke("inspect", "--input", FIXTURES / "pe64.exe")["abi_hint"]
    assert hint["status"] == "candidate_from_file_format"
    assert hint["verified_for_function"] is False
    assert hint["id"] == "windows-x64"
    assert hint["integer_args"] == ["rcx", "rdx", "r8", "r9"]
    assert hint["vector_args"] == ["xmm0", "xmm1", "xmm2", "xmm3"]
    assert hint["entry_rsp_first_stack_arg_offset"] == 40
    assert hint["shadow_space_bytes"] == 32
    assert hint["red_zone_bytes"] == 0
    assert "rdi" in hint["callee_saved_gprs"]
    assert "rsi" in hint["callee_saved_gprs"]
    assert "xmm6" in hint["callee_saved_vector_registers"]
    assert hint["vector_args_follow_position"] is True


def test_abi_hint_system_v_elf_x64(tmp_path):
    # Preserve a valid self-authored ELF fixture's layout; change only e_machine.
    # The test inspects metadata and never executes the fixture as x86-64 code.
    elf = bytearray((FIXTURES / "aarch64.elf").read_bytes())
    assert elf[:4] == bytes([0x7F, 0x45, 0x4C, 0x46])
    assert elf[4] == 2 and elf[5] == 1  # ELF64 little endian
    elf[18:20] = (62).to_bytes(2, "little")  # EM_X86_64
    target = tmp_path / "abi-x64.elf"
    target.write_bytes(elf)
    hint = invoke("inspect", "--input", target)["abi_hint"]
    assert hint["id"] == "sysv-amd64"
    assert hint["status"] == "candidate_from_file_format"
    assert hint["verified_for_function"] is False
    assert hint["integer_args"] == ["rdi", "rsi", "rdx", "rcx", "r8", "r9"]
    assert hint["entry_rsp_first_stack_arg_offset"] == 8
    assert hint["red_zone_bytes"] == 128
    assert hint["shadow_space_bytes"] == 0
    assert "rdi" in hint["caller_saved_gprs"]
    assert "rdi" not in hint["callee_saved_gprs"]
    assert hint["vector_args_follow_position"] is False


def test_abi_is_not_guessed_for_unrelated_formats(tmp_path):
    assert invoke("inspect", "--input", FIXTURES / "pe32.exe")["abi_hint"]["status"] == "unknown"
    assert invoke("inspect", "--input", FIXTURES / "macho_x86_64")["abi_hint"]["status"] == "unknown"
    raw = tmp_path / "no-platform.bin"
    raw.write_bytes(bytes.fromhex("b82a000000c3"))
    hint = invoke("inspect", "--input", raw, "--raw", "--arch", "x86-64",
                  "--base", "0x1000", "--entry", "0x1000")["abi_hint"]
    assert hint["status"] == "unknown"
