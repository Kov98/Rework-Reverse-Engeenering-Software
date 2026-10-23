import weakref
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import QHBoxLayout, QLabel, QLineEdit, QPlainTextEdit, QPushButton, QVBoxLayout, QWidget

from reworkbench.project.bytes import read_range


class HexView(QWidget):
    loaded = Signal(int, object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="hex-reader")
        executor = self._executor
        self.destroyed.connect(lambda: executor.shutdown(wait=False, cancel_futures=True))
        self._token = 0
        self.path = None
        self.offset = 0
        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 6, 6, 6)
        toolbar = QHBoxLayout()
        toolbar.addWidget(QLabel("Offset no arquivo"))
        self.offset_input = QLineEdit("0x0")
        self.offset_input.setMaximumWidth(180)
        self.offset_input.returnPressed.connect(self._jump)
        toolbar.addWidget(self.offset_input)
        for label, direction in (("Anterior", -1), ("Próxima", 1)):
            button = QPushButton(label)
            button.clicked.connect(lambda checked=False, step=direction: self.show_file(self.path, max(0, self.offset + step * 65536)) if self.path else None)
            toolbar.addWidget(button)
        toolbar.addStretch()
        self.detail = QLabel("Nenhuma amostra selecionada")
        toolbar.addWidget(self.detail)
        layout.addLayout(toolbar)
        self.text = QPlainTextEdit()
        self.text.setReadOnly(True)
        self.text.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.text.setFont(QFont("Consolas", 10))
        self.text.setPlaceholderText("Os bytes da amostra aparecem aqui, por intervalos de até 64 KiB.")
        layout.addWidget(self.text)
        self.loaded.connect(self._apply, Qt.QueuedConnection)

    def _jump(self):
        try:
            offset = int(self.offset_input.text(), 0)
            if self.path:
                self.show_file(self.path, offset)
        except ValueError:
            self.detail.setText("Use um offset válido, como 0x1000.")

    def clear(self):
        self._token += 1
        self.path = None
        self.offset = 0
        self.text.clear()
        self.offset_input.setText('0x0')
        self.detail.setText('Nenhuma amostra selecionada')

    def shutdown(self):
        self._token += 1
        self._executor.shutdown(wait=False, cancel_futures=True)

    def show_file(self, path: Path, offset=0):
        if path is None:
            return
        self.path, self.offset = Path(path), offset
        self._token += 1
        token = self._token
        reference = weakref.ref(self)
        future = self._executor.submit(read_range, self.path, offset)

        def done(result):
            view = reference()
            if view:
                try:
                    view.loaded.emit(token, result)
                except RuntimeError:
                    pass
        future.add_done_callback(done)

    def _apply(self, token, future):
        if token != self._token:
            return
        try:
            content = future.result()
        except Exception as exc:
            self.detail.setText(str(exc))
            return
        rows = []
        for index in range(0, len(content), 16):
            block = content[index:index+16]
            hexadecimal = " ".join(f"{b:02x}" for b in block)
            characters = "".join(chr(b) if 32 <= b < 127 else "." for b in block)
            rows.append(f"{self.offset + index:08x}  {hexadecimal:<47}  │ {characters}")
        self.text.setPlainText("\n".join(rows))
        self.offset_input.setText(hex(self.offset))
        self.detail.setText(f"{len(content):,} bytes · somente leitura")
