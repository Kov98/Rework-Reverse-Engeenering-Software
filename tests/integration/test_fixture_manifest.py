import hashlib
import json
from pathlib import Path


def test_manifest_matches_headers_and_hashes():
    folder = Path(__file__).resolve().parents[1] / "fixtures"
    manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8"))
    by_id = {f["id"]: f for f in manifest["fixtures"]}
    assert {"pe32", "pe64", "elf_aarch64", "macho_x64", "macho_fat", "arm", "thumb", "mips"} <= by_id.keys()
    for entry in manifest["fixtures"]:
        content = (folder / entry["path"]).read_bytes()
        assert hashlib.sha256(content).hexdigest() == entry["sha256"]
        assert entry["commands"]
        if entry["format"] == "PE":
            assert content[:2] == b"MZ"
        elif entry["format"] == "ELF":
            assert content[:4] == b"\x7fELF"
        elif entry["format"] == "Mach-O":
            assert content[:4] in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe")
        else:
            assert entry["image_base"] == "0x1000"
            assert entry["entries"][0]["address"]["value"] == "0x1000"
    assert by_id["thumb"]["entries"][0]["context"]["TMode"] == "1"
