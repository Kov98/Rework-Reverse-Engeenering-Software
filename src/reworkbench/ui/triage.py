from concurrent.futures import Future

from PySide6.QtCore import Qt

from PySide6.QtWidgets import QLabel, QLineEdit, QVBoxLayout, QWidget

from reworkbench.domain import Page
from reworkbench.ui.code_view import table
from reworkbench.ui.models import PagedTableModel, PagingControls


class TriageView(QWidget):
    def __init__(self, show_offset, parent=None):
        super().__init__(parent)
        self.rows = []
        self.show_offset = show_offset
        layout = QVBoxLayout(self)
        self.summary = QLabel('Triagem estática de bytes · nenhuma amostra selecionada')
        layout.addWidget(self.summary)
        self.search = QLineEdit()
        self.search.setPlaceholderText('Buscar strings ASCII da amostra…')
        layout.addWidget(self.search)
        self.model = PagedTableModel(self._fetch, ('offset', 'value'), self)
        self.view = table(self.model)
        self.view.doubleClicked.connect(self._activated)
        layout.addWidget(self.view)
        layout.addWidget(PagingControls(self.model, self))
        self.search.textChanged.connect(self.model.set_query)

    def _fetch(self, offset, query):
        rows = self.rows if not query else [row for row in self.rows if query.casefold() in row['value'].casefold()]
        result = Future()
        result.set_result(Page(tuple(rows[offset:offset + 200]), offset, len(rows)))
        return result

    def clear(self):
        self.rows = []
        self.summary.setText('Triagem estática de bytes · nenhuma amostra selecionada')
        self.model.set_query('')

    def apply(self, result):
        self.rows = [{'offset': hex(row['offset']), 'file_offset': row['offset'], 'value': row['value']} for row in result['strings']]
        suffix = ' · lista limitada' if result['strings_truncated'] else ''
        self.summary.setText(f'{result["size"]:,} bytes · entropia {result["entropy"]:.3f} bits/byte · {len(self.rows):,} strings ASCII{suffix}')
        self.model.set_query(self.search.text())

    def _activated(self, index):
        row = index.data(Qt.UserRole)
        if row:
            self.show_offset(row['file_offset'])
