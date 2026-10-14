from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox, QComboBox, QDialog, QDialogButtonBox, QFormLayout, QLabel, QLineEdit, QMessageBox, QPlainTextEdit, QSpinBox, QVBoxLayout

from reworkbench.domain import Address, EntryPoint, ImportOptions


class ImportDialog(QDialog):
    def __init__(self, source, inspection, catalog, parent=None):
        super().__init__(parent)
        self.setWindowTitle('Configurar importação')
        self.resize(720, 560)
        self.inspection = inspection
        self.languages = sorted(catalog.get('languages', []), key=lambda item: item['id'])
        layout = QVBoxLayout(self)
        title = QLabel(str(source))
        title.setTextInteractionFlags(Qt.TextSelectableByMouse)
        title.setWordWrap(True)
        layout.addWidget(title)
        form = QFormLayout()
        self.choice = QComboBox()
        choices = inspection.get('choices', [])
        for choice in choices:
            self.choice.addItem(choice['label'], choice)
        preferred = next((i for i, item in enumerate(choices) if item.get('preferred')), 0)
        if choices:
            self.choice.setCurrentIndex(preferred)
        form.addRow('Formato / imagem', self.choice)
        self.raw = QCheckBox('Importar como firmware bruto (BinaryLoader)')
        self.raw.setChecked(inspection.get('raw_required', False))
        self.raw.setEnabled(not inspection.get('container'))
        form.addRow(self.raw)
        self.language = QComboBox()
        self.language.setEditable(True)
        self.language.addItem('Automático', None)
        for item in self.languages:
            self.language.addItem(f'{item["id"]} · {item["name"]}', item['id'])
        self.language.setInsertPolicy(QComboBox.NoInsert)
        form.addRow('Linguagem / arquitetura', self.language)
        self.compiler = QComboBox()
        form.addRow('Especificação de compilador', self.compiler)
        self.base = QLineEdit('0x0')
        form.addRow('Base na memória (firmware bruto)', self.base)
        self.entries = QPlainTextEdit()
        self.entries.setMaximumHeight(110)
        self.entries.setPlaceholderText('Uma entrada por linha, com contexto opcional:\nram:0x8000\nram:0x9000 TMode=1')
        form.addRow('Pontos de entrada e contexto', self.entries)
        self.analysis_timeout = QSpinBox()
        self.analysis_timeout.setRange(1, 86400)
        self.analysis_timeout.setValue(300)
        self.analysis_timeout.setSuffix(' s')
        form.addRow('Limite da autoanálise', self.analysis_timeout)
        self.decompile_timeout = QSpinBox()
        self.decompile_timeout.setRange(1, 3600)
        self.decompile_timeout.setValue(30)
        self.decompile_timeout.setSuffix(' s por função')
        form.addRow('Limite da decompilação', self.decompile_timeout)
        layout.addLayout(form)
        hint = QLabel('Firmware bruto exige a arquitetura, a base e pelo menos uma entrada. Para Thumb, use uma entrada alinhada com TMode=1. Em arquivos universais, escolha a imagem explicitamente.')
        hint.setWordWrap(True)
        layout.addWidget(hint)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self.language.currentIndexChanged.connect(self._language_changed)
        self.choice.currentIndexChanged.connect(self._choice_changed)
        self.raw.toggled.connect(self._raw_changed)
        self._choice_changed()
        self._raw_changed()

    def _choice_changed(self):
        choice = self.choice.currentData() or {}
        language = choice.get('language_id')
        self.language.setCurrentIndex(max(0, self.language.findData(language)))
        self._language_changed()
        compiler = choice.get('compiler_id')
        if compiler:
            self.compiler.setCurrentIndex(max(0, self.compiler.findData(compiler)))

    def _language_changed(self):
        self.compiler.clear()
        self.compiler.addItem('Automático', None)
        selected = self.language.currentData()
        description = next((item for item in self.languages if item['id'] == selected), {})
        for item in description.get('compilers', []):
            self.compiler.addItem(f'{item["id"]} · {item["name"]}', item['id'])

    def _raw_changed(self):
        self.base.setEnabled(self.raw.isChecked())
        self.choice.setEnabled(not self.raw.isChecked() and self.choice.count() > 0)

    def options(self):
        choice = self.choice.currentData() or {}
        language = self.language.currentData()
        # Editable combo supports searching existing entries; arbitrary unknown IDs
        # are rejected here rather than silently selecting another architecture.
        if self.language.currentText() != self.language.itemText(self.language.currentIndex()):
            raise ValueError('Selecione uma linguagem existente na lista.')
        entries = []
        for line in self.entries.toPlainText().splitlines():
            if not line.strip():
                continue
            parts = line.split()
            location = parts[0]
            space, value = location.rsplit(':', 1) if ':' in location else ('ram', location)
            context = {}
            for field in parts[1:]:
                name, setting = field.split('=', 1)
                if not name or name in context:
                    raise ValueError('Registrador de contexto vazio ou repetido.')
                int(setting, 0)
                context[name] = setting
            entries.append(EntryPoint(Address(space, value), context))
        raw = self.raw.isChecked()
        if raw and (not language or not entries):
            raise ValueError('Defina a arquitetura e ao menos um ponto de entrada para o firmware bruto.')
        if self.inspection.get('container') and not choice.get('id'):
            raise ValueError('Escolha a imagem do arquivo universal.')
        return ImportOptions(loader_id='BinaryLoader' if raw else choice.get('loader_id'), load_spec_id=choice.get('id'), language_id=language, compiler_id=self.compiler.currentData(), image_base=Address('ram', self.base.text().strip()).value if raw else None, entries=tuple(entries), analysis_timeout_s=self.analysis_timeout.value(), decompile_timeout_s=self.decompile_timeout.value())

    def accept(self):
        try:
            self.options()
        except (ValueError, TypeError) as exc:
            QMessageBox.warning(self, 'Configuração incompleta', str(exc))
            return
        super().accept()

    def selected_options(self):
        return self.options()
