"""Run the C++/Qt interaction suite against the real desktop components."""
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "build/bin/reworkbench-ui-tests.exe"


@pytest.mark.skipif(not TESTS.is_file(), reason="Enable REWORKBENCH_BUILD_UI_TESTS")
def test_native_workspace_interactions(tmp_path):
    report = tmp_path / "qt-interactions.txt"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    if os.name == "nt":
        # The offscreen plugin has no Windows font database integration.
        # Read installed fonts; never bundle Microsoft fonts with the product.
        env["QT_QPA_FONTDIR"] = str(Path(os.environ["WINDIR"]) / "Fonts")
    result = subprocess.run(
        [str(TESTS), "-o", f"{report},txt"], env=env,
        capture_output=True, text=True, encoding="utf-8", timeout=120,
    )
    detail = report.read_text(encoding="utf-8") if report.is_file() else ""
    assert result.returncode == 0, detail + result.stdout + result.stderr
