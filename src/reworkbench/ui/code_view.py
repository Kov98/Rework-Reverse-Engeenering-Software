from __future__ import annotations

from concurrent.futures import Future

from PySide6.QtCore import QRegularExpression, Qt
from PySide6.QtGui import QColor, QFont, QSyntaxHighlighter, QTextCharFormat
from PySide6.QtWidgets import QLabel, QPlainTextEdit, QSplitter, QTableView, QVBoxLayout, QWidget

from reworkbench.domain import Address, Page
from reworkbench.ui.models import PagedTableModel


class NavigationHistory:
    def __init__(self):
        self.items = []
        self.index = -1

    def clear(self):
        self.items.clear()
        self.index = -1

    def push(self, analysis_id, address):
        item = (analysis_id, address)
        if self.index >= 0 and self.items[self.index] == item:
            return
        del self.items[self.index + 1:]
        self.items.append(item)
        self.index = len(self.items) - 1

    def back(self):
        if self.index <= 0:
            return None
        self.index -= 1
        return self.items[self.index]

    def forward(self):
        if self.index + 1 >= len(self.items):
            return None
        self.index += 1
        return self.items[self.index]


class CHighlighter(QSyntaxHighlighter):
    def __init__(self, document):
        super().__init__(document)
        self.rules = []
        for pattern, color in (
            (r'\b(if|else|for|while|do|switch|case|return|goto|break|continue|sizeof)\b', '#c6a0f6'),
            (r'\b(void|int|char|short|long|float|double|unsigned|signed|struct|const|bool|uint\w*|undefined\w*)\b', '#8bd5ca'),
            (r'\b(0x[0-9a-fA-F]+|\d+)\b', '#f5a97f'),
            (r'"([^"\\]|\\.)*"', '#a6da95'),
            (r'//[^\n]*', '#7f8b9d'),
        ):
            style = QTextCharFormat()
            style.setForeground(QColor(color))
            self.rules.append((QRegularExpression(pattern), style))
        self.comment_style = QTextCharFormat()
        self.comment_style.setForeground(QColor('#7f8b9d'))

    def highlightBlock(self, text):
        for pattern, style in self.rules:
            matches = pattern.globalMatch(text)
            while matches.hasNext():
                match = matches.next()
                self.setFormat(match.capturedStart(), match.capturedLength(), style)
        start = 0 if self.previousBlockState() == 1 else text.find('/*')
        self.setCurrentBlockState(0)
        while start >= 0:
            end = text.find('*/', start + (0 if self.previousBlockState() == 1 and start == 0 else 2))
            if end < 0:
                self.setCurrentBlockState(1)
                self.setFormat(start, len(text) - start, self.comment_style)
                break
            self.setFormat(start, end + 2 - start, self.comment_style)
            start = text.find('/*', end + 2)


def table(model, parent=None):
    view = QTableView(parent)
    view.setModel(model)
    view.setSelectionBehavior(QTableView.SelectRows)
    view.setSelectionMode(QTableView.SingleSelection)
    view.setAlternatingRowColors(True)
    view.setSortingEnabled(False)
    view.setWordWrap(False)
    view.verticalHeader().hide()
    view.horizontalHeader().setStretchLastSection(True)
    view.setFont(QFont('Consolas', 10))
    return view


class CodeView(QWidget):
    def __init__(self, session, navigate, show_offset, parent=None):
        super().__init__(parent)
        self.session, self.navigate, self.show_offset = session, navigate, show_offset
        self.analysis_id = None
        self.address = None
        self.request_token = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self.heading = QLabel('Selecione uma função para explorar')
        self.heading.setObjectName('codeHeading')
        layout.addWidget(self.heading)
        self.message = QLabel('O pseudocódigo aproxima o comportamento do binário; nomes e tipos originais podem não existir.')
        self.message.setWordWrap(True)
        layout.addWidget(self.message)
        panes = QSplitter(Qt.Horizontal)
        self.assembly_model = PagedTableModel(lambda offset, query: self._fetch('assembly', offset, query), ('address', 'bytes', 'mnemonic', 'operands', 'file_offset'), self)
        self.assembly = table(self.assembly_model)
        self.assembly.setObjectName('assemblyView')
        self.assembly.doubleClicked.connect(self._assembly_activated)
        self.pseudocode = QPlainTextEdit()
        self.pseudocode.setObjectName('pseudocodeView')
        self.pseudocode.setReadOnly(True)
        self.pseudocode.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.pseudocode.setFont(QFont('Consolas', 11))
        self.highlighter = CHighlighter(self.pseudocode.document())
        panes.addWidget(self._panel('ASSEMBLY · clique duas vezes para ver os bytes', self.assembly))
        panes.addWidget(self._panel('PSEUDOCÓDIGO C · Ghidra', self.pseudocode))
        panes.setStretchFactor(0, 1)
        panes.setStretchFactor(1, 1)
        self.xrefs_model = PagedTableModel(lambda offset, query: self._fetch('xrefs', offset, query), ('source', 'target', 'type'), self)
        self.xrefs = table(self.xrefs_model)
        self.xrefs.doubleClicked.connect(self._xref_activated)
        vertical = QSplitter(Qt.Vertical)
        vertical.addWidget(panes)
        vertical.addWidget(self._panel('REFERÊNCIAS · origem e destino da seleção e de sua função', self.xrefs))
        vertical.setStretchFactor(0, 4)
        vertical.setStretchFactor(1, 1)
        layout.addWidget(vertical)
        self.assembly_model.failed.connect(self.message.setText)
        self.xrefs_model.failed.connect(self.message.setText)

    @staticmethod
    def _panel(title, child):
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(4, 4, 4, 4)
        label = QLabel(title)
        label.setObjectName('panelTitle')
        layout.addWidget(label)
        layout.addWidget(child)
        return panel

    def _fetch(self, kind, offset, query):
        if self.analysis_id is None:
            result = Future()
            result.set_result(Page((), 0, 0))
            return result
        return self.session.fetch_page(self.analysis_id, kind, offset, query)

    def clear(self):
        self.analysis_id = self.address = self.request_token = None
        self.heading.setText('Selecione uma função para explorar')
        self.pseudocode.clear()
        self.assembly_model.set_query('')
        self.xrefs_model.set_query('')

    def show_function(self, analysis_id, address: Address, name=None):
        self.analysis_id, self.address = analysis_id, address
        self.heading.setText(f'{name or "Endereço"}   ·   {address}')
        self.message.setText('Pseudocódigo aproximado · seleção em processamento…')
        self.pseudocode.setPlainText('Aguardando o decompilador…')
        query = f'@address={address}'
        self.assembly_model.set_query(query)
        self.xrefs_model.set_query(query)
        self.request_token = self.session.select_function(analysis_id, address)

    def apply_result(self, event):
        if event['analysis_id'] != self.analysis_id or event['request_id'] != self.request_token:
            return
        result = event['payload']
        if result.get('error'):
            self.pseudocode.clear()
            self.pseudocode.setPlaceholderText(result['error'])
            self.message.setText(result['error'])
        else:
            self.pseudocode.setPlainText(result.get('code') or '')
            self.message.setText(f'Pseudocódigo aproximado · revisão {event.get("revision", 0)} · confirme tipos e fluxo no assembly.')

    def refresh(self):
        if self.address is not None:
            self.show_function(self.analysis_id, self.address)

    def _assembly_activated(self, index):
        row = index.data(Qt.UserRole)
        if row and row.get('file_offset') is not None:
            self.show_offset(row['file_offset'])
        else:
            self.message.setText('Essa instrução não possui bytes correspondentes no arquivo original.')

    def _xref_activated(self, index):
        row = index.data(Qt.UserRole)
        if not row:
            return
        source = index.column() == 0
        target = row.get('source_function' if source else 'target_function') or row.get('source' if source else 'target')
        if target:
            self.navigate(Address.from_json(target))
