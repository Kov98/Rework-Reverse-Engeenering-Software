from __future__ import annotations

import weakref
from concurrent.futures import CancelledError, Future
from datetime import datetime
from pathlib import Path

from PySide6.QtCore import QSettings, Qt, QTimer, Signal
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import QComboBox, QDialog, QDockWidget, QFileDialog, QHBoxLayout, QInputDialog, QLabel, QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QProgressBar, QPushButton, QStackedWidget, QToolBar, QVBoxLayout, QWidget

from reworkbench.domain import Address, Page
from reworkbench.ui.code_view import CodeView, NavigationHistory, table
from reworkbench.ui.hex_view import HexView
from reworkbench.ui.import_dialog import ImportDialog
from reworkbench.ui.investigation import InvestigationPanel
from reworkbench.ui.models import PagedTableModel, PagingControls
from reworkbench.ui.settings import SettingsDialog
from reworkbench.ui.triage import TriageView

STATES = {'queued': 'Na fila', 'importing': 'Importando', 'analyzing': 'Analisando', 'ready': 'Pronta', 'partial': 'Parcial', 'cancelled': 'Interrompida', 'failed': 'Falhou'}


class MainWindow(QMainWindow):
    eventReceived = Signal(object)
    futureReady = Signal(object)

    def __init__(self, session, parent=None, *, settings=None):
        super().__init__(parent)
        self.session = session
        self.settings = settings or QSettings('REWorkbench', 'Workbench')
        self.analysis_id = None
        self.sample_path = None
        self._generation = 0
        self._scan_token = 0
        self._jobs = 0
        self._engine_busy = False
        self._closing = self._allow_close = False
        self._shutdown_future = None
        self.history = NavigationHistory()
        self.models = {}
        self.docks = {}
        self._analysis_details = {}
        self.setWindowTitle('Bancada de engenharia reversa')
        self.resize(1500, 940)
        self.setMinimumSize(1050, 720)
        self._actions()
        self._toolbar()
        self.code = CodeView(session, self.navigate, self.show_analysis_offset)
        self.stack = QStackedWidget()
        self.stack.addWidget(self._welcome())
        self.stack.addWidget(self.code)
        self.setCentralWidget(self.stack)
        self._explorers()
        self.hex = HexView()
        self._dock('hex', 'Bytes da amostra', self.hex, Qt.BottomDockWidgetArea)
        self.console = QPlainTextEdit()
        self.console.setReadOnly(True)
        self.console.setMaximumBlockCount(2000)
        self._dock('console', 'Atividade', self.console, Qt.BottomDockWidgetArea)
        self.tabifyDockWidget(self.docks['hex'], self.docks['console'])
        self.docks['hex'].raise_()
        self.triage = TriageView(self.show_offset)
        self._dock('triage', 'Triagem de bytes e strings ASCII', self.triage, Qt.BottomDockWidgetArea)
        self.tabifyDockWidget(self.docks['hex'], self.docks['triage'])
        self.investigation = InvestigationPanel(session, self.watch, self.navigate)
        self._dock('investigation', 'Investigação', self.investigation, Qt.RightDockWidgetArea)
        self.metadata = QPlainTextEdit()
        self.metadata.setReadOnly(True)
        self._dock('metadata', 'Informações da amostra', self.metadata, Qt.RightDockWidgetArea)
        self.tabifyDockWidget(self.docks['investigation'], self.docks['metadata'])
        self.docks['investigation'].raise_()
        self.status = QLabel('Crie ou abra um projeto para começar.')
        self.statusBar().addWidget(self.status, 1)
        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setMaximumWidth(150)
        self.progress.hide()
        self.statusBar().addPermanentWidget(self.progress)
        self.engine_status = QLabel('Motor sob demanda')
        self.statusBar().addPermanentWidget(self.engine_status)
        self._menu()
        self.eventReceived.connect(self.apply_event, Qt.QueuedConnection)
        self.futureReady.connect(self._future_ready, Qt.QueuedConnection)
        reference = weakref.ref(self)

        def emit(event):
            window = reference()
            if window is not None:
                try:
                    window.eventReceived.emit(event)
                except RuntimeError:
                    pass
        self.session.emit = emit
        geometry = self.settings.value('window/geometry')
        state = self.settings.value('window/state')
        if geometry:
            self.restoreGeometry(geometry)
        if state:
            self.restoreState(state)
        self._project_actions(False)

    def _action(self, text, callback, shortcut=None):
        action = QAction(text, self)
        action.triggered.connect(lambda checked=False: callback())
        if shortcut:
            action.setShortcut(shortcut)
        return action

    def _actions(self):
        self.new_project_action = self._action('Novo projeto…', self.new_project, QKeySequence.New)
        self.open_project_action = self._action('Abrir projeto…', self.open_project, QKeySequence.Open)
        self.import_action = self._action('Importar binário…', self.import_sample, 'Ctrl+I')
        self.bytes_action = self._action('Examinar bytes…', self.examine_bytes, 'Ctrl+Shift+O')
        self.save_action = self._action('Salvar projeto', self.save_project, QKeySequence.Save)
        self.settings_action = self._action('Configurações do motor…', self.configure)
        self.cancel_action = self._action('Interromper', self.cancel, 'Esc')
        self.cancel_action.setEnabled(False)
        self.back_action = self._action('Voltar', self.back, 'Alt+Left')
        self.forward_action = self._action('Avançar', self.forward, 'Alt+Right')
        self.jump_action = self._action('Ir para endereço…', self.jump, 'G')
        self.rename_action = self._action('Renomear função…', self.rename, 'F2')
        self.json_action = self._action('Exportar relatório JSON…', self.export_report)
        self.code_action = self._action('Exportar pseudocódigo da função…', self.export_code)
        self._history_actions()

    def _toolbar(self):
        bar = QToolBar('Principal')
        bar.setObjectName('mainToolbar')
        bar.setMovable(False)
        self.addToolBar(bar)
        for action in (self.new_project_action, self.open_project_action, self.import_action):
            bar.addAction(action)
        bar.addSeparator()
        self.analysis_combo = QComboBox()
        self.analysis_combo.setMinimumContentsLength(24)
        self.analysis_combo.setToolTip('Análises do projeto')
        self.analysis_combo.currentIndexChanged.connect(self._analysis_selected)
        bar.addWidget(self.analysis_combo)
        bar.addSeparator()
        for action in (self.back_action, self.forward_action, self.jump_action, self.rename_action):
            bar.addAction(action)
        bar.addSeparator()
        bar.addAction(self.cancel_action)

    def _menu(self):
        file_menu = self.menuBar().addMenu('Arquivo')
        for action in (self.new_project_action, self.open_project_action, self.import_action, self.bytes_action, self.save_action):
            file_menu.addAction(action)
        file_menu.addSeparator()
        file_menu.addAction(self.json_action)
        file_menu.addAction(self.code_action)
        file_menu.addSeparator()
        file_menu.addAction(self._action('Sair', self.close, QKeySequence.Quit))
        navigation = self.menuBar().addMenu('Navegar')
        for action in (self.back_action, self.forward_action, self.jump_action):
            navigation.addAction(action)
        analysis = self.menuBar().addMenu('Análise')
        for action in (self.rename_action, self.cancel_action, self.settings_action):
            analysis.addAction(action)
        view = self.menuBar().addMenu('Visualizar')
        for dock in self.docks.values():
            view.addAction(dock.toggleViewAction())
        help_menu = self.menuBar().addMenu('Ajuda')
        help_menu.addAction(self._action('Sobre a bancada', self.about))

    def _welcome(self):
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(45, 45, 45, 45)
        layout.addStretch()
        overline = QLabel('REWORKBENCH  /  ANÁLISE ESTÁTICA')
        overline.setObjectName('overline')
        layout.addWidget(overline)
        title = QLabel('Do binário ao entendimento.')
        title.setObjectName('welcomeTitle')
        layout.addWidget(title)
        text = QLabel('Explore executáveis, bibliotecas e firmware com o motor Ghidra.\nConecte funções, assembly, pseudocódigo, referências e bytes em uma investigação persistente.')
        text.setWordWrap(True)
        layout.addWidget(text)
        row = QHBoxLayout()
        for label, callback in (('Criar projeto', self.new_project), ('Abrir projeto', self.open_project), ('Examinar bytes', self.examine_bytes)):
            button = QPushButton(label)
            button.setMinimumHeight(42)
            button.clicked.connect(lambda checked=False, fn=callback: fn())
            row.addWidget(button)
        layout.addLayout(row)
        detail = QLabel('PE / ELF / Mach-O  ·  x86 / x64 / ARM / Thumb / MIPS  ·  projetos locais com SQLite')
        detail.setObjectName('muted')
        detail.setWordWrap(True)
        layout.addWidget(detail)
        layout.addStretch()
        return widget

    def _dock(self, key, title, widget, area):
        dock = QDockWidget(title, self)
        dock.setObjectName(f'{key}Dock')
        dock.setWidget(widget)
        self.addDockWidget(area, dock)
        self.docks[key] = dock
        return dock

    def _explorers(self):
        definitions = (
            ('functions', 'Funções', ('address', 'name', 'size')),
            ('symbols', 'Símbolos', ('address', 'name', 'type')),
            ('strings', 'Strings definidas', ('address', 'value')),
            ('imports', 'Imports', ('address', 'name', 'library')),
            ('regions', 'Regiões', ('start', 'name', 'size', 'permissions')),
        )
        previous = None
        for kind, title, columns in definitions:
            panel = QWidget()
            panel.setMinimumWidth(265)
            layout = QVBoxLayout(panel)
            layout.setContentsMargins(5, 5, 5, 5)
            search = QLineEdit()
            search.setPlaceholderText(f'Buscar {title.lower()}…')
            layout.addWidget(search)
            model = PagedTableModel(lambda offset, query, k=kind: self._fetch(k, offset, query), columns, self)
            self.models[kind] = model
            view = table(model)
            view.setObjectName(f'{kind}View')
            setattr(self, f'{kind}_view', view)
            view.doubleClicked.connect(lambda index, k=kind: self._record_activated(k, index))
            layout.addWidget(view)
            layout.addWidget(PagingControls(model, panel))
            model.failed.connect(self._error)
            timer = QTimer(search)
            timer.setSingleShot(True)
            timer.setInterval(220)
            search.textChanged.connect(lambda text, t=timer: t.start())
            timer.timeout.connect(lambda field=search, m=model: m.set_query(field.text()))
            dock = self._dock(kind, title, panel, Qt.LeftDockWidgetArea)
            if previous:
                self.tabifyDockWidget(previous, dock)
            previous = dock
        self.docks['functions'].raise_()

    def _fetch(self, kind, offset, query):
        if self.analysis_id is not None:
            return self.session.fetch_page(self.analysis_id, kind, offset, query)
        result = Future()
        result.set_result(Page((), 0, 0))
        return result

    def watch(self, future, callback=None, *, busy=False, scoped=True, on_error=None):
        generation = self._generation if scoped else None
        if busy:
            self._jobs += 1
            self._update_busy()
        reference = weakref.ref(self)

        def done(result):
            window = reference()
            if window is not None:
                try:
                    window.futureReady.emit((result, callback, generation, busy, on_error))
                except RuntimeError:
                    pass
        future.add_done_callback(done)
        return future

    def _future_ready(self, item):
        future, callback, generation, busy, on_error = item
        if busy:
            self._jobs = max(0, self._jobs - 1)
            self._update_busy()
        if self._closing and future is not self._shutdown_future:
            return
        if generation is not None and generation != self._generation:
            return
        try:
            result = future.result()
            if callback:
                callback(result)
        except CancelledError as exc:
            self.status.setText('Operação interrompida.')
            if on_error:
                on_error(exc)
        except Exception as exc:
            self._error(str(exc))
            if on_error:
                on_error(exc)
            if self._closing and future is self._shutdown_future:
                if self.session.closed:
                    self._allow_close = True
                    self.close()
                else:
                    self._closing = False
                    self.setEnabled(True)
                    self.investigation.mark_unsaved()
                    self._update_busy()

    def _error(self, message):
        self.status.setText(message)
        self.log(f'Erro: {message}')

    def log(self, message):
        self.console.appendPlainText(f'{datetime.now():%H:%M:%S}  {message}')

    def _update_busy(self):
        busy = self._jobs > 0 or self._engine_busy
        self.progress.setVisible(busy)
        self.cancel_action.setEnabled(busy and not self._closing)

    def _project_actions(self, enabled):
        for action in (self.import_action, self.save_action):
            action.setEnabled(enabled)
        active = enabled and self.analysis_id is not None
        self.json_action.setEnabled(active)
        self.jump_action.setEnabled(active)
        self.rename_action.setEnabled(active and self.code.address is not None)
        self.code_action.setEnabled(active and self.code.address is not None)

    def _clear_analysis(self):
        self.analysis_id = self.sample_path = None
        self._analysis_details.clear()
        self.analysis_combo.blockSignals(True)
        self.analysis_combo.clear()
        self.analysis_combo.blockSignals(False)
        self.history.clear()
        self._history_actions()
        self.code.clear()
        self.investigation.set_selection(None, None)
        self.triage.clear()
        self._scan_token += 1
        self.hex.clear()
        self.metadata.clear()
        self.stack.setCurrentIndex(0)
        for model in self.models.values():
            model.set_query('')
        self._project_actions(False)

    def _request_project(self, path, create):
        self.investigation.flush()
        future = self.session.create_project(Path(path)) if create else self.session.open_project(Path(path))
        self._generation = self.session.generation
        self.status.setText('Abrindo projeto…')
        self.setEnabled(False)

        def failed(error):
            self.setEnabled(True)
            if self.session.project_root is None:
                self._clear_analysis()
            self._project_actions(self.session.project_root is not None)
        self.watch(future, busy=True, on_error=failed)
        return future

    def save_project(self):
        self.investigation.flush()
        self.watch(self.session.save(), busy=True)

    def new_project(self):
        parent = QFileDialog.getExistingDirectory(self, 'Pasta para o novo projeto')
        if not parent:
            return
        name, accepted = QInputDialog.getText(self, 'Novo projeto', 'Nome da pasta do projeto:')
        if accepted and name.strip():
            name = name.strip()
            if name in ('.', '..') or any(char in name for char in '/\\:<>"|?*'):
                self._error('Use um nome de pasta válido.')
                return
            self._request_project(Path(parent) / name, True)

    def open_project(self):
        folder = QFileDialog.getExistingDirectory(self, 'Abrir pasta de projeto')
        if folder:
            self._request_project(folder, False)

    def import_sample(self, source=None):
        if self.session.project_root is None:
            self._error('Crie ou abra um projeto antes de importar um binário.')
            return
        if source is None:
            source, _ = QFileDialog.getOpenFileName(self, 'Selecionar binário', '', 'Binários (*.exe *.dll *.elf *.so *.macho *.bin *.rom *.img);;Todos os arquivos (*)')
        if not source:
            return
        source = Path(source)
        self._show_bytes(source)
        self.status.setText('Identificando formato e arquiteturas disponíveis…')

        def inspected(inspection):
            def catalog_ready(catalog):
                dialog = ImportDialog(source, inspection, catalog, self)
                if dialog.exec() == QDialog.Accepted:
                    self.watch(self.session.start_analysis(source, dialog.options()), busy=True)
            self.watch(self.session.catalog(), catalog_ready, busy=True)
        self.watch(self.session.inspect_sample(source), inspected, busy=True)

    def examine_bytes(self):
        source, _ = QFileDialog.getOpenFileName(self, 'Examinar bytes de um arquivo')
        if source:
            self._show_bytes(Path(source))
            self.docks['hex'].show()
            self.docks['hex'].raise_()

    def _show_bytes(self, path):
        self.sample_path = Path(path)
        self.hex.show_file(self.sample_path)
        self._scan_token += 1
        token = self._scan_token
        self.triage.summary.setText('Lendo bytes e extraindo strings ASCII…')

        def loaded(result):
            if token == self._scan_token:
                self.triage.apply(result)
        self.watch(self.session.scan_sample(self.sample_path), loaded, busy=True)

    def show_offset(self, offset):
        if self.sample_path:
            self.hex.show_file(self.sample_path, offset)
            self.docks['hex'].show()
            self.docks['hex'].raise_()

    def show_analysis_offset(self, offset):
        details = self._analysis_details.get(self.analysis_id, {})
        path = details.get('sample_path')
        if path:
            self.hex.show_file(Path(path), offset)
            self.docks['hex'].show()
            self.docks['hex'].raise_()

    def _analysis_selected(self, index):
        analysis_id = self.analysis_combo.itemData(index)
        if not analysis_id or analysis_id == self.analysis_id:
            return
        self.investigation.flush()
        self.analysis_id = analysis_id
        self.code.clear()
        self.investigation.set_selection(analysis_id, None)
        self._refresh_models()
        details = self._analysis_details.get(analysis_id)
        if details:
            self._apply_details(details)
        else:
            self.watch(self.session.analysis_details(analysis_id), self._apply_details)
        self.watch(self.session.activate_analysis(analysis_id), busy=True)

    def _refresh_models(self):
        for model in self.models.values():
            model.set_query(model.query)

    def _apply_details(self, details):
        analysis_id = details['id']
        self._analysis_details[analysis_id] = details
        if analysis_id != self.analysis_id:
            return
        path = details.get('sample_path')
        if path and self.sample_path != Path(path):
            self._show_bytes(Path(path))
        rows = [
            ('Estado', STATES.get(details.get('state'), details.get('state', '—'))),
            ('Formato', details.get('format', 'Em importação')),
            ('Linguagem', details.get('language_id', '—')),
            ('Compilador', details.get('compiler_id', '—')),
            ('Base', details.get('image_base', '—')),
            ('Tamanho', f'{details.get("size", 0):,} bytes'),
            ('SHA-256', details.get('sha256', '—')),
            ('Motor', f'Ghidra {details.get("ghidra_version", "—")}'),
            ('Revisão', details.get('revision', 0)),
            ('Entradas', ', '.join(str(Address.from_json(a)) for a in details.get('entry_points', [])) or '—'),
            ('Amostra', details.get('sample_path', '—')),
        ]
        self.metadata.setPlainText('\n\n'.join(f'{key}\n{value}' for key, value in rows))
        self._project_actions(True)

    def apply_event(self, event):
        if self._closing:
            return
        generation = event.get('generation', self._generation)
        if generation < self._generation:
            return
        self._generation = generation
        kind, data = event['kind'], event.get('payload', {})
        analysis_id = event.get('analysis_id')
        if kind == 'project_opened':
            self._clear_analysis()
            self.setEnabled(True)
            self._project_actions(True)
            self.status.setText(f'Projeto: {data["root"]}')
            self.settings.setValue('project/last', data['root'])

            def listed(analyses):
                for analysis in analyses:
                    if self.analysis_combo.findData(analysis.id) < 0:
                        self.analysis_combo.blockSignals(True)
                        self.analysis_combo.addItem(f'{analysis.id[:8]} · {STATES.get(analysis.state, analysis.state)}', analysis.id)
                        self.analysis_combo.blockSignals(False)
            self.watch(self.session.list_analyses(), listed)
        elif kind == 'analysis_created':
            self.investigation.flush()
            self.analysis_id = analysis_id
            self.analysis_combo.blockSignals(True)
            index = self.analysis_combo.findData(analysis_id)
            if index < 0:
                self.analysis_combo.addItem(f'{analysis_id[:8]} · {STATES.get(data["state"], data["state"])}', analysis_id)
                index = self.analysis_combo.count() - 1
            self.analysis_combo.setCurrentIndex(index)
            self.analysis_combo.blockSignals(False)
            self._apply_details(data)
            self.code.clear()
            self.investigation.set_selection(analysis_id, None)
            self.stack.setCurrentIndex(1)
            self._refresh_models()
        elif kind in ('analysis_opened', 'analysis_ready'):
            self._apply_details(data)
            if analysis_id == self.analysis_id:
                self._refresh_models()
                self.investigation.refresh_audit()
                if kind == 'analysis_ready':
                    self._engine_busy = False
        elif kind == 'snapshot' and analysis_id == self.analysis_id:
            model = self.models.get(data['kind'])
            if model:
                model.set_query(model.query)
            self.code.refresh_records(data['kind'])
        elif kind == 'state':
            state = data['state']
            self._engine_busy = state in ('queued', 'importing', 'analyzing')
            self.status.setText(data.get('message', STATES.get(state, state)))
            index = self.analysis_combo.findData(analysis_id)
            if index >= 0:
                self.analysis_combo.setItemText(index, f'{analysis_id[:8]} · {STATES.get(state, state)}')
            self.log(data.get('message', state))
        elif kind == 'selection_result':
            self.code.apply_result(event)
        elif kind == 'renamed' and analysis_id == self.analysis_id:
            self._refresh_models()
            self.code.refresh()
            self.log(f'Função renomeada para {data.get("name", "")} · revisão {event["revision"]}')
        elif kind == 'status':
            self.status.setText(data.get('message', ''))
            self.log(data.get('message', ''))
        elif kind == 'engine_failure':
            self._engine_busy = False
            self.engine_status.setText('Motor interrompido')
            self._error(data.get('message', 'Falha no motor.'))
        elif kind == 'heartbeat':
            self.engine_status.setText('Motor conectado')
        elif kind == 'error':
            self._engine_busy = False
            self._error(data.get('message', 'Erro durante a operação.'))
        self._update_busy()

    def _record_activated(self, kind, index):
        row = index.data(Qt.UserRole)
        if not row:
            return
        location = row.get('address') or row.get('start')
        if location:
            self.navigate(Address.from_json(location), name=row.get('name'))
        if row.get('file_offset') is not None:
            self.show_analysis_offset(row['file_offset'])

    def navigate(self, address, *, name=None, record=True):
        if self.analysis_id is None:
            return
        if record:
            self.history.push(self.analysis_id, address)
        self.stack.setCurrentIndex(1)
        self.code.show_function(self.analysis_id, address, name)
        self.investigation.set_selection(self.analysis_id, address)
        self._history_actions()
        self._project_actions(True)

    def _history_actions(self):
        self.back_action.setEnabled(self.history.index > 0)
        self.forward_action.setEnabled(self.history.index + 1 < len(self.history.items))

    def _history_open(self, item):
        if item:
            analysis_id, address = item
            if analysis_id != self.analysis_id:
                index = self.analysis_combo.findData(analysis_id)
                if index >= 0:
                    self.analysis_combo.setCurrentIndex(index)
            self.navigate(address, record=False)
        self._history_actions()

    def back(self):
        self._history_open(self.history.back())

    def forward(self):
        self._history_open(self.history.forward())

    def jump(self):
        text, accepted = QInputDialog.getText(self, 'Ir para endereço', 'Endereço virtual hexadecimal (ex.: ram:0x401000):')
        if accepted:
            try:
                space, value = text.rsplit(':', 1) if ':' in text else ('ram', text)
                self.navigate(Address(space.strip(), value.strip()))
            except ValueError as exc:
                self._error(str(exc))

    def rename(self):
        if self.analysis_id is None or self.code.address is None:
            return
        name, accepted = QInputDialog.getText(self, 'Renomear função', 'Novo nome da função:')
        if accepted:
            self.watch(self.session.rename_function(self.analysis_id, self.code.address, name.strip()), busy=True)

    def export_report(self):
        if self.analysis_id is None:
            return
        self.investigation.flush()
        path, _ = QFileDialog.getSaveFileName(self, 'Exportar relatório', f'analise-{self.analysis_id[:8]}.json', 'JSON (*.json)')
        if path:
            self.watch(self.session.export_report(self.analysis_id, Path(path)), self._exported, busy=True)

    def export_code(self):
        if self.analysis_id is None or self.code.address is None:
            return
        path, _ = QFileDialog.getSaveFileName(self, 'Exportar pseudocódigo aproximado', f'funcao-{self.code.address.value}.c', 'Código C aproximado (*.c);;Texto (*.txt)')
        if path:
            self.watch(self.session.export_functions(self.analysis_id, [self.code.address], Path(path)), self._exported, busy=True)

    def _exported(self, path):
        self.status.setText(f'Exportado: {path}')
        self.log(f'Exportado: {path}')

    def cancel(self):
        self.session.cancel_active()
        self.status.setText('Interrompendo a operação…')

    def configure(self):
        dialog = SettingsDialog(self.session.config, self, settings=self.settings)
        if dialog.exec() == QDialog.Accepted:
            self.watch(self.session.configure(dialog.selected_config()), lambda _: self.engine_status.setText('Motor sob demanda'), busy=True)
        dialog.deleteLater()

    def about(self):
        QMessageBox.about(self, 'REWorkbench', 'Bancada local de engenharia reversa\n\nInterface Qt, motor Ghidra em processo separado, projetos SQLite e triagem opcional em Rust.\n\nO pseudocódigo é uma aproximação do comportamento do binário. Esta versão realiza análise estática.')

    def closeEvent(self, event):
        if self._allow_close:
            self.settings.setValue('window/geometry', self.saveGeometry())
            self.settings.setValue('window/state', self.saveState())
            self.settings.sync()
            self.hex.shutdown()
            event.accept()
            return
        event.ignore()
        if not self._closing:
            self.investigation.flush()
            self._closing = True
            self.status.setText('Salvando e encerrando o motor…')
            self.setEnabled(False)

            def closed(_):
                self._allow_close = True
                self.close()
            self._shutdown_future = self.session.close()
            self.watch(self._shutdown_future, closed, scoped=False)
