import weakref
from concurrent.futures import Future

from PySide6.QtCore import QAbstractTableModel, QModelIndex, Qt, Signal, Slot

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
            return display_value(row.get(self.columns[index.column()]))
        return None

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role == Qt.DisplayRole:
            return LABELS.get(self.columns[section], self.columns[section]) if orientation == Qt.Horizontal else str(section + 1)
        return None

    def canFetchMore(self, parent=QModelIndex()):
        return not parent.isValid() and self._more and not self._loading

    def fetchMore(self, parent=QModelIndex()):
        if not self.canFetchMore(parent):
            return
        self._loading = True
        token = self._token
        reference = weakref.ref(self)
        try:
            future = self.fetch(len(self._rows), self.query)
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
            self.failed.emit(str(exc))
            return
        if page.items:
            first = len(self._rows)
            self.beginInsertRows(QModelIndex(), first, first + len(page.items) - 1)
            self._rows.extend(page.items)
            self.endInsertRows()
        self.total = page.total
        self._more = len(self._rows) < page.total if page.total >= 0 else len(page.items) == 200
        self.pageReady.emit(page.total)

    def set_query(self, text):
        self.beginResetModel()
        self._token += 1
        self.query = text
        self._rows.clear()
        self._more = True
        self._loading = False
        self.total = 0
        self.endResetModel()
        self.fetchMore()
