from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import QCheckBox, QLabel, QListWidget, QListWidgetItem, QPlainTextEdit, QPushButton, QTabWidget, QVBoxLayout, QWidget

from reworkbench.domain import Address


class InvestigationPanel(QTabWidget):
    def __init__(self, session, watch, navigate, parent=None):
        super().__init__(parent)
        self.session, self.watch, self.navigate = session, watch, navigate
        self.analysis_id = self.address = None
        self._dirty = self._loading = False
        self._token = 0
        page = QWidget()
        layout = QVBoxLayout(page)
        self.location = QLabel('Selecione um endereço para anotar')
        self.location.setTextInteractionFlags(Qt.TextSelectableByMouse)
        layout.addWidget(self.location)
        self.note = QPlainTextEdit()
        self.note.setPlaceholderText('Hipóteses, tipos identificados e observações da investigação…')
        layout.addWidget(self.note)
        self.bookmark = QCheckBox('Marcar este endereço')
        layout.addWidget(self.bookmark)
        self.save = QPushButton('Salvar nota')
        self.save.clicked.connect(self.flush)
        layout.addWidget(self.save)
        self.addTab(page, 'Notas')
        self.bookmarks = QListWidget()
        self.bookmarks.itemDoubleClicked.connect(self._bookmark_opened)
        self.addTab(self.bookmarks, 'Marcadores')
        audit = QWidget()
        audit_layout = QVBoxLayout(audit)
        description = QLabel('Regras v1: imports gets, strcpy, strcat e sprintf. Cada pista exige revisão manual do uso e não comprova vulnerabilidade.')
        description.setWordWrap(True)
        audit_layout.addWidget(description)
        self.hints = QListWidget()
        self.hints.itemDoubleClicked.connect(self._hint_opened)
        audit_layout.addWidget(self.hints)
        self.addTab(audit, 'Pistas de auditoria')
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.setInterval(700)
        self.timer.timeout.connect(self.flush)
        self.note.textChanged.connect(self._changed)
        self.bookmark.toggled.connect(self._changed)
        self._enable(False)

    def _enable(self, enabled):
        for widget in (self.note, self.bookmark, self.save):
            widget.setEnabled(enabled)

    def _changed(self):
        if not self._loading and self.address is not None:
            self._dirty = True
            self.timer.start()

    def flush(self):
        self.timer.stop()
        if not self._dirty or self.analysis_id is None or self.address is None:
            return
        analysis_id, address = self.analysis_id, self.address
        self._dirty = False
        future = self.session.set_note(analysis_id, address, self.note.toPlainText(), self.bookmark.isChecked())
        self.watch(future, lambda _: self.refresh_notes() if analysis_id == self.analysis_id else None)

    def set_selection(self, analysis_id, address):
        self.flush()
        self.analysis_id, self.address = analysis_id, address
        self._token += 1
        self._loading = True
        self.note.clear()
        self.bookmark.setChecked(False)
        self._loading = False
        self._dirty = False
        self.location.setText(str(address) if address else 'Selecione um endereço para anotar')
        self._enable(address is not None)
        if analysis_id:
            self.refresh_notes()
        else:
            self.bookmarks.clear()
            self.hints.clear()

    def refresh_notes(self):
        if not self.analysis_id:
            return
        token, analysis_id = self._token, self.analysis_id

        def loaded(notes):
            if token != self._token or analysis_id != self.analysis_id:
                return
            self.bookmarks.clear()
            for note in notes:
                address = Address.from_json(note['address'])
                if address == self.address and not self._dirty:
                    self._loading = True
                    self.note.setPlainText(note['text'])
                    self.bookmark.setChecked(note['bookmarked'])
                    self._loading = False
                if note['bookmarked']:
                    summary = note['text'].splitlines()[0][:100] if note['text'] else 'Marcador'
                    item = QListWidgetItem(f'{address} · {summary}')
                    item.setData(Qt.UserRole, note['address'])
                    self.bookmarks.addItem(item)
        self.watch(self.session.notes(analysis_id), loaded)

    def refresh_audit(self):
        if not self.analysis_id:
            return
        analysis_id = self.analysis_id

        def loaded(hints):
            if analysis_id != self.analysis_id:
                return
            self.hints.clear()
            if not hints:
                self.hints.addItem('Nenhum import coberto pelas regras foi localizado nesta análise.')
            for hint in hints:
                references = hint['references']
                title = f'{hint["name"]} · {hint.get("library") or "biblioteca desconhecida"} · {len(references)} referência(s)'
                item = QListWidgetItem(title)
                item.setToolTip(hint['message'])
                item.setData(Qt.UserRole, hint.get('address'))
                self.hints.addItem(item)
                for reference in references:
                    child = QListWidgetItem(f'    ↳ {reference["source"]["value"]} · {reference.get("type") or "referência"}')
                    child.setData(Qt.UserRole, reference.get('source_function') or reference['source'])
                    child.setToolTip(hint['message'])
                    self.hints.addItem(child)
        self.watch(self.session.audit_hints(analysis_id), loaded)

    def _bookmark_opened(self, item):
        address = item.data(Qt.UserRole)
        if address:
            self.navigate(Address.from_json(address))

    def _hint_opened(self, item):
        address = item.data(Qt.UserRole)
        if address:
            self.navigate(Address.from_json(address))
