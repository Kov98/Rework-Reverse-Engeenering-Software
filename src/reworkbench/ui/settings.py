from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import weakref

from PySide6.QtCore import QSettings, Qt, Signal
from PySide6.QtWidgets import QDialog, QDialogButtonBox, QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit, QPushButton, QVBoxLayout

from reworkbench.domain import EngineConfig
from reworkbench.engine.config import validate_engine


class SettingsDialog(QDialog):
    diagnosticReady = Signal(object)

    def __init__(self, config, parent=None, *, settings=None):
        super().__init__(parent)
        self.setWindowTitle("Configurações do motor")
        self.setMinimumWidth(620)
        self.settings = settings or QSettings("REWorkbench", "Workbench")
        self._executor = ThreadPoolExecutor(max_workers=1)
        executor = self._executor
        self.destroyed.connect(lambda: executor.shutdown(wait=False, cancel_futures=True))
        layout = QVBoxLayout(self)
        heading = QLabel("Motor de análise")
        heading.setObjectName("sectionTitle")
        layout.addWidget(heading)
        description = QLabel("Selecione Ghidra 12.1.4 e JDK 21 de 64 bits. A configuração é usada pelo processo de análise.")
        description.setWordWrap(True)
        layout.addWidget(description)
        form = QFormLayout()
        self.ghidra_path = QLineEdit(str(self.settings.value("engine/ghidra_home", str(config.ghidra_home or ""))))
        self.java_path = QLineEdit(str(self.settings.value("engine/java_home", str(config.java_home or ""))))
        for title, edit in (("Pasta do Ghidra", self.ghidra_path), ("Pasta do JDK", self.java_path)):
            row = QHBoxLayout()
            row.addWidget(edit)
            browse = QPushButton("Procurar…")
            browse.clicked.connect(lambda checked=False, field=edit: self._browse(field))
            row.addWidget(browse)
            form.addRow(title, row)
        layout.addLayout(form)
        self.diagnostic = QLabel("A interface pode ser usada para visualizar bytes mesmo sem o motor configurado.")
        self.diagnostic.setWordWrap(True)
        layout.addWidget(self.diagnostic)
        self.check = QPushButton("Verificar instalação")
        self.check.clicked.connect(self._check)
        layout.addWidget(self.check)
        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.button(QDialogButtonBox.Save).setText("Salvar")
        buttons.button(QDialogButtonBox.Cancel).setText("Cancelar")
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self.diagnosticReady.connect(self._diagnostic, Qt.QueuedConnection)

    def _browse(self, field):
        folder = QFileDialog.getExistingDirectory(self, "Selecionar instalação", field.text())
        if folder:
            field.setText(folder)

    def selected_config(self):
        return EngineConfig(Path(self.ghidra_path.text().strip()) if self.ghidra_path.text().strip() else None, Path(self.java_path.text().strip()) if self.java_path.text().strip() else None)

    def _check(self):
        self.check.setEnabled(False)
        self.diagnostic.setText("Verificando…")
        future = self._executor.submit(validate_engine, self.selected_config())
        reference = weakref.ref(self)

        def ready(done):
            dialog = reference()
            if dialog is not None:
                try:
                    dialog.diagnosticReady.emit(done)
                except RuntimeError:
                    pass
        future.add_done_callback(ready)

    def _diagnostic(self, future):
        self.check.setEnabled(True)
        try:
            result = future.result()
            self.diagnostic.setText("Motor pronto para análise." if result["ready"] else "\n".join(issue["message"] for issue in result["issues"]))
        except Exception as exc:
            self.diagnostic.setText(str(exc))

    def accept(self):
        config = self.selected_config()
        self.settings.setValue("engine/ghidra_home", str(config.ghidra_home or ""))
        self.settings.setValue("engine/java_home", str(config.java_home or ""))
        self.settings.sync()
        super().accept()
