import weakref
from concurrent.futures import Future

from PySide6.QtCore import QAbstractTableModel, QModelIndex, Qt, Signal, Slot
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget

LABELS = {"address": "Endereço", "name": "Nome", "size": "Tamanho", "value": "Texto", "library": "Biblioteca", "start": "Início", "end": "Fim", "permissions": "Acesso", "file_offset": "Offset no arquivo", "offset": "Offset no arquivo", "source": "Origem", "target": "Destino", "type": "Tipo", "bytes": "Bytes", "mnemonic": "Instrução", "operands": "Operandos"}


def display_value(value):
    if value is None:
        return "—"
    if isinstance(value, dict) and "space" in value and "value" in value:
        return value["value"] if value["space"] == "ram" else f"{value['space']}:{value['value']}"
    return str(value)


class PagedTableModel(QAbstractTableModel):
    resultReady = Signal(int, object)
    pageReady = Signal(int)
    failed = Signal(str)
    windowChanged = Signal()
    MAX_WINDOW_ROWS = 1000

    def __init__(self, fetch, columns, parent=None):
        super().__init__(parent)
        self.fetch = fetch
        self.columns = columns
        self._rows = []
        self._token = 0
        self._loading = False
        self._more = True
        self.query = ""
        self.total = 0
        self.start = 0
        self.resultReady.connect(self._apply, Qt.QueuedConnection)

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self._rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.columns)

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self._rows):
            return None
        row = self._rows[index.row()]
        if role == Qt.UserRole:
            return row
        if role in (Qt.DisplayRole, Qt.ToolTipRole):
            column = self.columns[index.column()]
            value = display_value(row.get(column))
            return value + ' … [prévia limitada]' if column == 'value' and row.get('value_truncated') else value
        return None

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role == Qt.DisplayRole:
            return LABELS.get(self.columns[section], self.columns[section]) if orientation == Qt.Horizontal else str(section + 1)
        return None

    def canFetchMore(self, parent=QModelIndex()):
        return not parent.isValid() and self._more and not self._loading and len(self._rows) < self.MAX_WINDOW_ROWS

    def fetchMore(self, parent=QModelIndex()):
        if not self.canFetchMore(parent):
            return
        self._loading = True
        self.windowChanged.emit()
        token = self._token
        reference = weakref.ref(self)
        try:
            future = self.fetch(self.start + len(self._rows), self.query)
        except Exception as exc:
            future = Future()
            future.set_exception(exc)

        def ready(done):
            model = reference()
            if model is not None:
                try:
                    model.resultReady.emit(token, done)
                except RuntimeError:
                    pass
        future.add_done_callback(ready)

    @Slot(int, object)
    def _apply(self, token, future):
        if token != self._token:
            return
        self._loading = False
        try:
            page = future.result()
        except Exception as exc:
            self._more = False
            self.windowChanged.emit()
            self.failed.emit(str(exc))
            return
        if page.items:
            first = len(self._rows)
            self.beginInsertRows(QModelIndex(), first, first + len(page.items) - 1)
            self._rows.extend(page.items)
            self.endInsertRows()
        self.total = page.total
        self._more = self.start + len(self._rows) < page.total if page.total >= 0 else len(page.items) == 200
        self.pageReady.emit(page.total)
        self.windowChanged.emit()

    def set_query(self, text):
        self._reset_window(text, 0)

    def _reset_window(self, text, start):
        self.beginResetModel()
        self._token += 1
        self.query = text
        self.start = start
        self._rows.clear()
        self._more = True
        self._loading = False
        self.total = 0
        self.endResetModel()
        self.windowChanged.emit()
        self.fetchMore()

    def next_window(self):
        if not self._loading and self._rows and self._more:
            self._reset_window(self.query, self.start + len(self._rows))

    def previous_window(self):
        if not self._loading and self.start:
            self._reset_window(self.query, max(0, self.start - self.MAX_WINDOW_ROWS))


class PagingControls(QWidget):
    """Keep table memory bounded while making every result reachable."""
    def __init__(self, model, parent=None):
        super().__init__(parent)
        self.model = model
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 2, 0, 2)
        self.previous = QPushButton('Anterior')
        self.previous.setToolTip('Intervalo anterior de até 1.000 registros')
        self.previous.clicked.connect(model.previous_window)
        self.label = QLabel()
        self.label.setAlignment(Qt.AlignCenter)
        self.next = QPushButton('Próximos')
        self.next.setToolTip('Continuar após os registros carregados')
        self.next.clicked.connect(model.next_window)
        layout.addWidget(self.previous)
        layout.addWidget(self.label, 1)
        layout.addWidget(self.next)
        model.windowChanged.connect(self.refresh)
        self.refresh()

    def refresh(self):
        model = self.model
        end = model.start + model.rowCount()
        total = f'{model.total:,}' if model.total >= 0 else '?'
        self.label.setText(f'{model.start + 1:,}–{end:,} / {total}' if model.rowCount() else ('Carregando…' if model._loading else '0 registros'))
        self.previous.setEnabled(model.start > 0 and not model._loading)
        self.next.setEnabled(bool(model.rowCount()) and model._more and not model._loading)
