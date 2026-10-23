from pathlib import Path

from PySide6.QtCore import QSettings

from reworkbench.application.session import Session
from reworkbench.domain import EngineConfig
from reworkbench.ui.window import MainWindow
from reworkbench.ui.settings import SettingsDialog
from reworkbench.ui.hex_view import HexView


def test_window_opens_without_engine(qtbot, tmp_path):
    session = Session(EngineConfig(), lambda event: None)
    window = MainWindow(session, settings=QSettings(str(tmp_path / "settings.ini"), QSettings.IniFormat))
    qtbot.addWidget(window)
    window.show()
    assert window.windowTitle() == "Bancada de engenharia reversa"
    assert window.findChild(type(window.functions_view), "functionsView") is not None
    assert window.new_project_action.isEnabled()
    window.close()
    qtbot.waitUntil(lambda: not window.isVisible(), timeout=10000)


def test_configured_paths_survive_restart(qtbot, tmp_path):
    settings = QSettings(str(tmp_path / "settings.ini"), QSettings.IniFormat)
    dialog = SettingsDialog(EngineConfig(), settings=settings)
    qtbot.addWidget(dialog)
    dialog.ghidra_path.setText("C:/Tools/Ghidra")
    dialog.java_path.setText("C:/Tools/JDK21")
    dialog.accept()
    other = SettingsDialog(EngineConfig(), settings=settings)
    qtbot.addWidget(other)
    assert other.selected_config().ghidra_home == Path("C:/Tools/Ghidra")


def test_hex_window_is_bounded(qtbot, tmp_path):
    path = tmp_path / "sample.bin"
    path.write_bytes(b"ABCDEF" * 20000)
    view = HexView()
    qtbot.addWidget(view)
    view.show_file(path)
    qtbot.waitUntil(lambda: "41 42 43 44" in view.text.toPlainText(), timeout=10000)
    assert len(view.text.toPlainText()) < 500000
    assert "00000000" in view.text.toPlainText()


def test_active_job_has_progress_and_cancel(qtbot, tmp_path):
    session = Session(EngineConfig(), lambda event: None)
    window = MainWindow(session, settings=QSettings(str(tmp_path / "settings.ini"), QSettings.IniFormat))
    qtbot.addWidget(window)
    window.show()
    window.apply_event({"kind": "state", "generation": 0, "analysis_id": "a", "payload": {"state": "analyzing", "message": "Analisando…"}})
    assert window.cancel_action.isEnabled()
    assert window.progress.isVisible()
    window.close()
    qtbot.waitUntil(lambda: not window.isVisible(), timeout=10000)
