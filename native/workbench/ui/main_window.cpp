#include "main_window.hpp"
#include "application_brand.hpp"
#include "graph_view.hpp"
#include "hex_view.hpp"
#include "import_dialog.hpp"
#include "reworkbench/decompilation_cache.hpp"
#include "code_highlighter.hpp"
#include "code_editor.hpp"
#include "assistant_panel.hpp"
#include "workspace_theme.hpp"
#include "binary_explorer.hpp"
#include "window_chrome.hpp"
#include <QApplication>
#include <QSignalBlocker>

#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QScreen>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QToolButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace reworkbench::ui {
namespace {
QString exception_text(const std::exception& error) { return QString::fromUtf8(error.what()); }
QString state_name(const QString& state) {
    if (state == QStringLiteral("ready")) return QStringLiteral("Pronta");
    if (state == QStringLiteral("partial")) return QStringLiteral("Parcial");
    if (state == QStringLiteral("cancelled")) return QStringLiteral("Interrompida");
    return state;
}
void persist_drafts(const std::shared_ptr<Project>& project, const QString& id,
                    const QMap<QString, Note>& drafts, std::stop_token stop) {
    if (!project || id.isEmpty()) return;
    for (const auto& note : drafts) { check_cancelled(stop); project->set_note(id, note); }
}
QWidget* panel(const QString& title, QWidget* widget) {
    auto* result = new QWidget;
    auto* layout = new QVBoxLayout(result);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    auto* label = new QLabel(title);
    label->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(label);
    layout->addWidget(widget);
    return result;
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), settings_(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("REWorkbench"), QStringLiteral("Native")) {
    apply_workspace_theme(*qApp);
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("Rework"));
    setWindowIcon(application_icon());
    resize(1440, 900);
    setMinimumSize(960, 540);
    build_actions();
    build_workspace();
    build_workspace_shell();
    menus_ = new QMenuBar;
    build_menus();
    chrome_ = new WindowChrome(this, menus_, actions_["back"], actions_["forward"], actions_["quick_open"],
                               actions_["toggle_sidebar"], actions_["toggle_panel"], actions_["toggle_assistant"]);
    setMenuWidget(chrome_);
    task_ = new QFutureWatcher<TaskResult>(this);
    connect(task_, &QFutureWatcher<TaskResult>::finished, this, [this] { finish_task(); });
    if (settings_.contains(QStringLiteral("window/geometry"))) restoreGeometry(settings_.value(QStringLiteral("window/geometry")).toByteArray());
    if (const auto* current_screen = screen()) {
        const auto available = current_screen->availableGeometry();
        resize(std::min(width(), available.width()), std::min(height(), available.height()));
        move(std::clamp(x(), available.left(), std::max(available.left(), available.right() - width() + 1)),
             std::clamp(y(), available.top(), std::max(available.top(), available.bottom() - height() + 1)));
    }
    restore_workspace();
    update_actions();
    log(QStringLiteral("Motor próprio C++ · %1").arg(decoder_description()));
}
MainWindow::~MainWindow() { stop_.request_stop(); selection_stop_.request_stop(); audit_stop_.request_stop(); }

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    const auto* native = static_cast<MSG*>(message);
    if (native->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
        RECT rect;
        if (GetWindowRect(native->hwnd, &rect)) {
            const int x = GET_X_LPARAM(native->lParam), y = GET_Y_LPARAM(native->lParam);
            const int border = qRound(6 * devicePixelRatioF());
            const bool left = x < rect.left + border, right = x >= rect.right - border;
            const bool top = y < rect.top + border, bottom = y >= rect.bottom - border;
            if (left || right || top || bottom) {
                *result = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP)
                              : bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM)
                                       : left ? HTLEFT : HTRIGHT;
                return true;
            }
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::build_actions() {
    const auto add = [this](const QString& key, const QString& text, const QKeySequence& shortcut, std::function<void()> callback) {
        auto* action = new QAction(text, this);
        action->setObjectName(key);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [callback] { callback(); });
        actions_.insert(key, action);
    };
    add(QStringLiteral("new"), QStringLiteral("Novo projeto…"), QKeySequence::New, [this] { new_project(); });
    add(QStringLiteral("open"), QStringLiteral("Abrir projeto…"), QKeySequence::Open, [this] { open_project(); });
    add(QStringLiteral("import"), QStringLiteral("Importar binário…"), QKeySequence(QStringLiteral("Ctrl+I")), [this] { import_sample(); });
    add(QStringLiteral("bytes"), QStringLiteral("Examinar bytes…"), QKeySequence(QStringLiteral("Ctrl+Shift+O")), [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Examinar bytes"));
        if (!path.isEmpty()) examine_path(path);
    });
    add(QStringLiteral("save"), QStringLiteral("Salvar projeto"), QKeySequence::Save, [this] { save_project(); });
    add(QStringLiteral("json"), QStringLiteral("Exportar relatório JSON…"), {}, [this] { export_report(); });
    add(QStringLiteral("ir"), QStringLiteral("Exportar IR da função…"), {}, [this] { export_ir(); });
    add(QStringLiteral("pseudocode"), QStringLiteral("Exportar pseudocódigo…"), {}, [this] { export_pseudocode(); });
    add(QStringLiteral("diff"), QStringLiteral("Comparar binários…"), {}, [this] { compare_binary(); });
    add(QStringLiteral("patch"), QStringLiteral("Exportar cópia com patch…"), {}, [this] { export_patch(); });
    add(QStringLiteral("emulate"), QStringLiteral("Emular função…"), {}, [this] { emulate_selection(); });
    add(QStringLiteral("symbols"), QStringLiteral("Importar mapa de símbolos…"), {}, [this] { import_symbols(); });
    add(QStringLiteral("enrichment"), QStringLiteral("Símbolos e metadados adicionais"), {}, [this] { show_enrichment(); });
    add(QStringLiteral("extensions"), QStringLiteral("Inspecionar extensões…"), {}, [this] { show_extensions(); });
    add(QStringLiteral("debugger"), QStringLiteral("Abrir depurador…"), {}, [this] { show_debugger(); });
    add(QStringLiteral("rename"), QStringLiteral("Renomear função…"), QKeySequence(QStringLiteral("F2")), [this] { rename_function(); });
    add(QStringLiteral("back"), QStringLiteral("Voltar"), QKeySequence(QStringLiteral("Alt+Left")), [this] { history_step(-1); });
    add(QStringLiteral("forward"), QStringLiteral("Avançar"), QKeySequence(QStringLiteral("Alt+Right")), [this] { history_step(1); });
    add(QStringLiteral("jump"), QStringLiteral("Ir para endereço…"), QKeySequence(QStringLiteral("Ctrl+G")), [this] { jump(); });
    add(QStringLiteral("cancel"), QStringLiteral("Interromper"), QKeySequence(QStringLiteral("Esc")), [this] {
        stop_.request_stop(); selection_stop_.request_stop(); status_->setText(QStringLiteral("Interrompendo…"));
    });
    add(QStringLiteral("about"), QStringLiteral("Sobre o Rework"), {}, [this] { about(); });
    add(QStringLiteral("quit"), QStringLiteral("Sair"), QKeySequence::Quit, [this] { close(); });
}

QDockWidget* MainWindow::add_dock(const QString& key, const QString& title, QWidget* widget, Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(key + QStringLiteral("Dock"));
    dock->setWidget(widget);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(area, dock);
    docks_.insert(key, dock);
    return dock;
}
void MainWindow::build_workspace() {
    analyses_ = new QComboBox;
    analyses_->setMinimumContentsLength(8);
    analyses_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    analyses_->setToolTip(QStringLiteral("Análises do projeto"));
    connect(analyses_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const auto id = analyses_->itemData(index).toString();
        if (!id.isEmpty() && id != analysis_id_ && !busy_) activate_analysis(id);
    });
    central_ = new QStackedWidget;
    auto* welcome = new QWidget;
    auto* welcome_layout = new QVBoxLayout(welcome);
    welcome_layout->setContentsMargins(45, 45, 45, 45);
    welcome_layout->addStretch();
    auto* overline = new QLabel(QStringLiteral("REWORK  /  ALFA 0.3"));
    overline->setObjectName(QStringLiteral("overline"));
    welcome_layout->addWidget(overline);
    auto* title = new QLabel(QStringLiteral("Seu espaço de\nengenharia reversa."));
    title->setObjectName(QStringLiteral("welcomeTitle"));
    welcome_layout->addWidget(title);
    auto* description = new QLabel(QStringLiteral("Um espaço próprio para conectar funções, instruções, referências e evidências.\nAnálise estática, projetos locais e um núcleo independente em C++."));
    description->setWordWrap(true);
    welcome_layout->addWidget(description);
    auto* buttons = new QHBoxLayout;
    for (const auto& key : {QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("bytes")}) {
        auto* button = new QPushButton(actions_[key]->text());
        if (key == QStringLiteral("new")) button->setObjectName("primaryButton");
        button->setMinimumHeight(32);
        connect(button, &QPushButton::clicked, actions_[key], &QAction::trigger);
        buttons->addWidget(button);
    }
    welcome_layout->addLayout(buttons);
    auto* commands = new QPushButton(QStringLiteral("Paleta de comandos                                      Ctrl+Shift+P"));
    connect(commands, &QPushButton::clicked, this, [this] { show_command_palette(); });
    welcome_layout->addWidget(commands);
    auto* formats = new QLabel(QStringLiteral("PE / ELF / Mach-O / RAW  ·  Assembly / IR / CFG  ·  Projetos locais"));
    formats->setObjectName(QStringLiteral("muted"));
    formats->setWordWrap(true);
    welcome_layout->addWidget(formats);
    welcome_layout->addStretch();
    central_->addWidget(welcome);
    auto* workspace = new QWidget;
    auto* workspace_layout = new QVBoxLayout(workspace);
    workspace_layout->setContentsMargins(0, 0, 0, 0); workspace_layout->setSpacing(0);
    documents_ = new QTabBar;
    documents_->setObjectName("functionTabs"); documents_->setDocumentMode(true);
    documents_->setMovable(true); documents_->setTabsClosable(true); documents_->setExpanding(false);
    documents_->setUsesScrollButtons(true); documents_->setElideMode(Qt::ElideMiddle);
    workspace_layout->addWidget(documents_);
    function_heading_ = new QLabel(QStringLiteral("Selecione uma função"));
    function_heading_->setObjectName(QStringLiteral("functionHeading"));
    function_heading_->setFixedHeight(26);
    function_heading_->setTextFormat(Qt::PlainText);
    function_heading_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    workspace_layout->addWidget(function_heading_);
    ir_status_ = new QLabel(QStringLiteral("A representação intermediária é parcial e não recupera o código original."));
    ir_status_->setWordWrap(true);
    ir_status_->hide();
    workspace_layout->addWidget(ir_status_);
    auto* panes = new QSplitter(Qt::Horizontal); editor_split_ = panes; panes->setHandleWidth(3);
    assembly_ = new Explorer(Dataset::Assembly, {QStringLiteral("Endereço"), QStringLiteral("Bytes"), QStringLiteral("Instrução"), QStringLiteral("Operandos")});
    assembly_->set_compact(true);
    assembly_->activated = [this](const Record& row, int) {
        if (row.other_address) navigate(*row.other_address);
        else if (row.address) select_instruction(*row.address);
    };
    assembly_->selected = [this](const Record& row) { if (row.address) select_instruction(*row.address); };
    listing_panel_ = panel(QStringLiteral("Listagem · Assembly"), assembly_);
    listing_panel_->setObjectName("listingPanel");
    panes->addWidget(listing_panel_);
    auto* representation = new QTabWidget; representation_ = representation;
    representation->setObjectName("representationTabs"); representation->setDocumentMode(true);
    representation->setMovable(true); representation->setTabsClosable(true);
    representation->setUsesScrollButtons(true); representation->setElideMode(Qt::ElideRight);
    representation->tabBar()->setExpanding(false); representation->setElideMode(Qt::ElideNone);
    pseudocode_ = new CodeEditor; pseudocode_->setObjectName("pseudocodeEditor");
    ssa_ = new CodeEditor; ssa_->setObjectName("ssaEditor");
    for (auto* editor : {pseudocode_, ssa_}) {
        editor->setReadOnly(true); editor->setFont(default_code_font());
        editor->setLineWrapMode(QPlainTextEdit::NoWrap); new CodeHighlighter(editor->document());
    }
    decompiler_status_ = new QLabel(QStringLiteral("Decompilador próprio · selecione uma função"));
    decompiler_status_->setObjectName("decompilerStatus");
    decompiler_status_->setWordWrap(false); decompiler_status_->setFixedHeight(22);
    decompiler_status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto* decompiler_panel = new QWidget;
    decompiler_panel->setObjectName("pseudocodePanel");
    auto* decompiler_layout = new QVBoxLayout(decompiler_panel);
    decompiler_layout->setContentsMargins(0, 0, 0, 0); decompiler_layout->setSpacing(0);
    decompiler_layout->addWidget(decompiler_status_); decompiler_layout->addWidget(pseudocode_);
    representation->addTab(decompiler_panel, QStringLiteral("Pseudocódigo"));
    representation->addTab(ssa_, QStringLiteral("SSA"));
    ir_ = new CodeEditor; ir_->setObjectName("irEditor");
    for (auto* editor : {pseudocode_, ssa_, ir_}) {
        static_cast<CodeEditor*>(editor)->address_selected = [this, editor](Address address) {
            select_instruction(address, editor);
        };
    }
    ir_->setReadOnly(true);
    ir_->setFont(default_code_font());
    ir_->setLineWrapMode(QPlainTextEdit::NoWrap);
    new CodeHighlighter(ir_->document());
    representation->addTab(ir_, QStringLiteral("IR"));
    graph_ = new GraphView;
    graph_->setObjectName("graphView");
    representation->addTab(graph_, QStringLiteral("Grafo"));
    graph_->activated = [this](Address address) { navigate(address); };
    graph_->selected = [this](Address address) { select_instruction(address); };
    panes->addWidget(representation);
    panes->setStretchFactor(0, 1); panes->setStretchFactor(1, 1);
    references_ = new Explorer(Dataset::References, {QStringLiteral("Origem"), QStringLiteral("Destino"), QStringLiteral("Tipo")});
    references_->set_compact(true);
    references_->activated = [this](const Record& row, int column) {
        if (column == 1 && row.other_address) navigate(*row.other_address);
        else if (row.address) navigate(*row.address);
    };
    workspace_layout->addWidget(panes, 1);
    central_->addWidget(workspace);
    setCentralWidget(central_);
    bottom_panel_ = new QTabWidget;
    bottom_panel_->setObjectName("bottomPanel"); bottom_panel_->setDocumentMode(true);
    bottom_panel_->setMinimumHeight(80);
    bottom_panel_->setMovable(true); bottom_panel_->setTabPosition(QTabWidget::North);
    bottom_panel_->addTab(references_, QStringLiteral("Referências"));
    auto* bottom_dock = add_dock("panel", QStringLiteral("Painel"), bottom_panel_, Qt::BottomDockWidgetArea);
    auto* no_header = new QWidget; no_header->setFixedHeight(0); bottom_dock->setTitleBarWidget(no_header);

    binary_explorer_ = new BinaryExplorer;
    binary_explorer_->activated = [this](Address address) { navigate(address); };
    binary_explorer_->offset_activated = [this](quint64 offset) { show_offset(offset); show_bottom_tab(hex_); };
    auto* sidebar = new QWidget;
    auto* sidebar_layout = new QVBoxLayout(sidebar); sidebar_layout->setContentsMargins(0, 0, 0, 0); sidebar_layout->setSpacing(0);
    sidebar_layout->addWidget(analyses_); sidebar_layout->addWidget(binary_explorer_, 1);
    add_dock("explorer", QStringLiteral("EXPLORADOR"), sidebar, Qt::LeftDockWidgetArea);

    struct Definition { Dataset dataset; QString key; QString title; QStringList columns; };
    const QVector<Definition> definitions{
        {Dataset::Functions, QStringLiteral("functions"), QStringLiteral("Funções"), {QStringLiteral("Endereço"), QStringLiteral("Nome"), QStringLiteral("Instruções"), QStringLiteral("Estado")}},
        {Dataset::Symbols, QStringLiteral("symbols"), QStringLiteral("Símbolos"), {QStringLiteral("Endereço"), QStringLiteral("Nome"), QStringLiteral("Tipo")}},
        {Dataset::Strings, QStringLiteral("strings"), QStringLiteral("Strings ASCII"), {QStringLiteral("Offset"), QStringLiteral("Texto")}},
        {Dataset::Imports, QStringLiteral("imports"), QStringLiteral("Imports"), {QStringLiteral("Slot"), QStringLiteral("Nome"), QStringLiteral("Biblioteca")}},
        {Dataset::Regions, QStringLiteral("regions"), QStringLiteral("Regiões"), {QStringLiteral("Endereço"), QStringLiteral("Nome"), QStringLiteral("Tamanho virtual"), QStringLiteral("Acesso"), QStringLiteral("Offset")}},
    };
    auto* datasets = new QTabWidget; datasets->setDocumentMode(true); datasets->setObjectName("datasetsPanel");
    for (const auto& definition : definitions) {
        auto* explorer = new Explorer(definition.dataset, definition.columns);
        explorer->set_compact(true);
        explorer->activated = [this](const Record& row, int) { if (row.address) navigate(*row.address); if (row.file_offset) show_offset(*row.file_offset); };
        explorers_.insert(definition.dataset, explorer);
        if (definition.dataset == Dataset::Functions) explorer->view()->setObjectName("functionTable");
        datasets->addTab(explorer, definition.title);
    }
    hex_ = new HexView;
    bottom_panel_->addTab(hex_, QStringLiteral("Hexadecimal"));
    console_ = new QPlainTextEdit;
    console_->setReadOnly(true); console_->setMaximumBlockCount(2000);
    bottom_panel_->addTab(console_, QStringLiteral("Saída"));
    bottom_panel_->addTab(datasets, QStringLiteral("Dados"));
    auto* investigation = new QTabWidget;
    auto* note_panel = new QWidget;
    auto* note_layout = new QVBoxLayout(note_panel);
    note_heading_ = new QLabel(QStringLiteral("Selecione um endereço para anotar"));
    note_heading_->setTextFormat(Qt::PlainText);
    note_heading_->setWordWrap(true);
    note_layout->addWidget(note_heading_);
    note_editor_ = new QPlainTextEdit;
    note_editor_->setPlaceholderText(QStringLiteral("Observações, hipóteses e evidências…"));
    note_layout->addWidget(note_editor_);
    bookmark_ = new QCheckBox(QStringLiteral("Marcar este endereço"));
    note_layout->addWidget(bookmark_);
    save_note_ = new QPushButton(QStringLiteral("Salvar notas"));
    connect(save_note_, &QPushButton::clicked, this, [this] { save_project(); });
    note_layout->addWidget(save_note_);
    investigation->addTab(note_panel, QStringLiteral("Notas"));
    bookmarks_ = new QListWidget;
    investigation->addTab(bookmarks_, QStringLiteral("Marcadores"));
    audit_ = new QListWidget;
    investigation->addTab(audit_, QStringLiteral("Pistas"));
    connect(note_editor_, &QPlainTextEdit::textChanged, this, [this] { record_draft(); });
    connect(bookmark_, &QCheckBox::toggled, this, [this] { record_draft(); });
    connect(bookmarks_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item->data(Qt::UserRole).toString().isEmpty()) return;
        try { navigate(parse_address(item->data(Qt::UserRole).toString())); } catch (const std::exception& exception) { error(exception_text(exception)); }
    });
    connect(audit_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item->data(Qt::UserRole).toString().isEmpty())
            try { navigate(parse_address(item->data(Qt::UserRole).toString())); } catch (const std::exception& exception) { error(exception_text(exception)); }
    });
    bottom_panel_->addTab(investigation, QStringLiteral("Anotações"));
    metadata_ = new QPlainTextEdit;
    metadata_->setReadOnly(true);
    bottom_panel_->addTab(metadata_, QStringLiteral("Amostra"));
    status_ = new QLabel(QStringLiteral("Crie ou abra um projeto nativo para começar."));
    status_->setTextFormat(Qt::PlainText);
    status_->setMaximumWidth(660);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(status_, 1);
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(170);
    progress_->setRange(0, 0); progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    sample_status_ = new QLabel(QStringLiteral("ALFA 0.3 · Sem amostra"));
    statusBar()->addPermanentWidget(sample_status_);
    statusBar()->setFixedHeight(22);
}
void MainWindow::build_menus() {
    auto* file = menus_->addMenu(QStringLiteral("Arquivo"));
    for (const auto& key : {QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("import"), QStringLiteral("bytes"), QStringLiteral("save")}) file->addAction(actions_[key]);
    file->addSeparator(); file->addAction(actions_[QStringLiteral("json")]); file->addAction(actions_[QStringLiteral("ir")]);
    file->addSeparator(); file->addAction(actions_[QStringLiteral("quit")]);
    auto* navigate_menu = menus_->addMenu(QStringLiteral("Navegar"));
    for (const auto& key : {QStringLiteral("back"), QStringLiteral("forward"), QStringLiteral("jump"), QStringLiteral("rename")}) navigate_menu->addAction(actions_[key]);
    for (const auto& key : {"quick_open", "next_document", "previous_document", "follow_target", "show_references"}) navigate_menu->addAction(actions_[key]);
    auto* analysis = menus_->addMenu(QStringLiteral("Análise"));
    analysis->addAction(actions_[QStringLiteral("cancel")]);
    for (const auto& key : {QStringLiteral("symbols"), QStringLiteral("enrichment"), QStringLiteral("emulate")}) analysis->addAction(actions_[key]);
    auto* tools = menus_->addMenu(QStringLiteral("Ferramentas"));
    for (const auto& key : {QStringLiteral("diff"), QStringLiteral("patch"), QStringLiteral("debugger"), QStringLiteral("extensions")}) tools->addAction(actions_[key]);
    file->addAction(actions_[QStringLiteral("pseudocode")]);
    auto* view = menus_->addMenu(QStringLiteral("Visualizar"));
    for (const auto& key : {"toggle_sidebar", "toggle_panel", "toggle_assistant", "toggle_listing", "focus_listing", "focus_decompiler"}) view->addAction(actions_[key]);
    view->addSeparator();
    for (auto* dock : docks_) view->addAction(dock->toggleViewAction());
    view->addSeparator();
    auto* editors = view->addMenu(QStringLiteral("Editores"));
    for (const auto& key : {"view_pseudocode", "view_ssa", "view_ir", "view_graph"}) editors->addAction(actions_[QString::fromLatin1(key)]);
    for (const auto& key : {"font_increase", "font_decrease", "font_choose", "font_reset", "reduce_motion", "layout_reset", "command_palette", "document_close"}) view->addAction(actions_[QString::fromLatin1(key)]);
    menus_->addMenu(QStringLiteral("Ajuda"))->addAction(actions_[QStringLiteral("about")]);
}

void MainWindow::start_task(const QString& label, Work work, Completion completion) {
    if (busy_) { status_->setText(QStringLiteral("Aguarde ou interrompa a operação atual.")); return; }
    busy_ = true;
    const auto task_token = ++task_token_;
    stop_ = std::stop_source{};
    completion_ = std::move(completion);
    status_->setText(label);
    progress_->setRange(0, 0); progress_->show();
    update_actions(); log(label);
    QPointer<MainWindow> reference(this);
    const auto progress = [reference, task_token](const Progress& value) {
        if (!reference) return;
        QMetaObject::invokeMethod(reference.data(), [reference, value, task_token] {
            if (reference && reference->task_token_ == task_token) reference->set_progress(value);
        }, Qt::QueuedConnection);
    };
    task_->setFuture(QtConcurrent::run([work = std::move(work), stop = stop_.get_token(), progress] {
        try { return work(stop, progress); }
        catch (const Cancelled&) { TaskResult result; result.cancelled = true; return result; }
        catch (const std::exception& exception) { TaskResult result; result.error = exception_text(exception); return result; }
        catch (...) { TaskResult result; result.error = QStringLiteral("Falha nativa inesperada."); return result; }
    }));
}
void MainWindow::finish_task() {
    const auto result = task_->result();
    busy_ = false; progress_->hide();
    auto completion = std::move(completion_);
    completion_ = {};
    if (!result.error.isEmpty()) {
        error(result.error);
        if (closing_) closing_ = false;
    } else if (result.cancelled) {
        status_->setText(QStringLiteral("Operação interrompida; dados já salvos preservados."));
    } else {
        if (!result.message.isEmpty()) { status_->setText(result.message); log(result.message); }
        if (completion) {
            try { completion(result); }
            catch (const std::exception& exception) { error(exception_text(exception)); closing_ = false; }
        }
    }
    update_actions();
    if (closing_ && !busy_ && !allow_close_) close();
}
void MainWindow::set_progress(const Progress& progress) {
    if (!busy_) return;
    status_->setText(progress.stage);
    if (progress.total && progress.completed <= progress.total) {
        progress_->setRange(0, 1000);
        progress_->setValue(static_cast<int>((static_cast<long double>(progress.completed) / progress.total) * 1000));
    } else progress_->setRange(0, 0);
}
void MainWindow::update_actions() {
    binary_explorer_->set_busy(busy_ || closing_);
    for (const auto& key : {"next_document", "previous_document"}) actions_[key]->setEnabled(documents_->count() > 1 && !busy_ && !closing_);
    actions_["follow_target"]->setEnabled(selected_function_ && !busy_ && !closing_);
    actions_["quick_open"]->setEnabled(!busy_ && !closing_);
    documents_->setEnabled(!busy_ && !closing_);
    actions_[QStringLiteral("document_close")]->setEnabled(documents_->count() > 0 && !busy_ && !closing_);
    for (const auto& key : {QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("bytes")}) actions_[key]->setEnabled(!busy_ && !closing_);
    actions_[QStringLiteral("import")]->setEnabled(project_ && !busy_ && !closing_);
    actions_[QStringLiteral("save")]->setEnabled(project_ && !busy_ && !closing_);
    actions_[QStringLiteral("json")]->setEnabled(snapshot_ && !busy_ && !closing_);
    actions_[QStringLiteral("ir")]->setEnabled(snapshot_ && selected_function_ && !busy_ && !closing_);
    for (const auto& key : {QStringLiteral("pseudocode"), QStringLiteral("emulate")}) actions_[key]->setEnabled(snapshot_ && selected_function_ && !busy_ && !closing_);
    for (const auto& key : {QStringLiteral("symbols"), QStringLiteral("enrichment"), QStringLiteral("patch")}) actions_[key]->setEnabled(snapshot_ && !busy_ && !closing_);
    for (const auto& key : {QStringLiteral("diff"), QStringLiteral("extensions"), QStringLiteral("debugger")}) actions_[key]->setEnabled(!busy_ && !closing_);
    actions_[QStringLiteral("rename")]->setEnabled(snapshot_ && selected_function_ && !busy_ && !closing_);
    actions_[QStringLiteral("jump")]->setEnabled(snapshot_ && !busy_ && !closing_);
    actions_[QStringLiteral("back")]->setEnabled(history_index_ > 0 && !busy_ && !closing_);
    actions_[QStringLiteral("forward")]->setEnabled(history_index_ + 1 < history_.size() && !busy_ && !closing_);
    actions_[QStringLiteral("cancel")]->setEnabled(busy_ && !closing_);
    analyses_->setEnabled(!busy_ && !closing_);
    note_editor_->setEnabled(note_address_ && !busy_ && !closing_);
    bookmark_->setEnabled(note_address_ && !busy_ && !closing_);
    save_note_->setEnabled(project_ && !drafts_.isEmpty() && !busy_ && !closing_);
}
void MainWindow::log(const QString& message) { console_->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + message); }
void MainWindow::error(const QString& message) { status_->setText(message); log(QStringLiteral("Erro: ") + message); }

void MainWindow::new_project() {
    const auto parent = QFileDialog::getExistingDirectory(this, QStringLiteral("Pasta para o projeto"));
    if (parent.isEmpty()) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Novo projeto nativo"), QStringLiteral("Nome da pasta:"), QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name == QStringLiteral(".") || name == QStringLiteral("..") || name.contains(QRegularExpression(QStringLiteral("[/\\\\:<>\"|?*]")))) { error(QStringLiteral("Nome de pasta inválido.")); return; }
    request_project(parent + QLatin1Char('/') + name, true);
}
void MainWindow::open_project() {
    const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("Abrir projeto nativo"));
    if (!path.isEmpty()) request_project(path, false);
}
void MainWindow::open_project_path(const QString& path, const QString& sample) { request_project(path, false, sample); }
void MainWindow::request_project(const QString& path, bool create, const QString& sample) {
    if (project_ && QFileInfo(path).canonicalFilePath() == QFileInfo(project_->root()).canonicalFilePath()) {
        if (!sample.isEmpty()) import_sample(sample);
        return;
    }
    const auto current = project_; const auto id = analysis_id_; const auto drafts = drafts_;
    start_task(QStringLiteral("Abrindo projeto nativo…"), [path, create, current, id, drafts](std::stop_token stop, ProgressCallback) {
        persist_drafts(current, id, drafts, stop); check_cancelled(stop);
        TaskResult result;
        result.project = create ? Project::create(path) : Project::open(path);
        result.summaries = result.project->analyses();
        if (!result.summaries.isEmpty()) {
            result.analysis_id = result.summaries.last().id;
            result.snapshot = std::make_shared<AnalysisSnapshot>(result.project->load_analysis(result.analysis_id, stop));
            result.notes = result.project->notes(result.analysis_id);
        }
        result.message = QStringLiteral("Projeto: ") + result.project->root();
        return result;
    }, [this, sample](const TaskResult& result) {
        install_project(result);
        if (!sample.isEmpty() && !closing_) import_sample(sample);
    });
}
void MainWindow::install_project(const TaskResult& result) {
    project_ = result.project;
    history_.clear(); history_index_ = -1;
    drafts_.clear(); notes_.clear();
    analysis_id_.clear(); snapshot_.reset(); selected_function_.reset(); selected_address_.reset(); note_address_.reset();
    binary_explorer_->set_snapshot({});
    chrome_->set_workspace(project_ ? QFileInfo(project_->root()).fileName() : QString{});
    { const QSignalBlocker blocked(documents_); while (documents_->count()) documents_->removeTab(0); }
    assistant_->set_selection({}, {}); sample_status_->setText(QStringLiteral("ALFA 0.3 · Sem amostra"));
    ++selection_token_; selection_stop_.request_stop();
    ++audit_token_; audit_stop_.request_stop();
    byte_path_.clear(); hex_->clear(); ir_->clear(); graph_->show_function(nullptr);
    pseudocode_->clear(); ssa_->clear();
    metadata_->clear(); function_heading_->setText(QStringLiteral("Selecione uma função"));
    for (auto* explorer : explorers_) explorer->set_snapshot({});
    assembly_->set_snapshot({}); references_->set_snapshot({});
    set_note_address({}); bookmarks_->clear(); audit_->clear();
    analyses_->blockSignals(true); analyses_->clear();
    for (const auto& summary : result.summaries)
        analyses_->addItem(QStringLiteral("%1 · %2").arg(summary.name.isEmpty() ? summary.id.left(8) : summary.name, state_name(summary.state)), summary.id);
    analyses_->blockSignals(false);
    central_->setCurrentIndex(result.snapshot ? 1 : 0);
    if (result.snapshot) install_snapshot(result);
    update_actions();
}

void MainWindow::import_sample(const QString& supplied) {
    if (!project_ || busy_ || closing_) return;
    const auto path = supplied.isEmpty() ? QFileDialog::getOpenFileName(this, QStringLiteral("Importar binário"), {}, QStringLiteral("Binários (*.exe *.dll *.elf *.so *.macho *.bin *.rom *.img);;Todos os arquivos (*)")) : supplied;
    if (path.isEmpty()) return;
    examine_path(path);
    start_task(QStringLiteral("Identificando formato…"), [path](std::stop_token stop, ProgressCallback) {
        check_cancelled(stop);
        TaskResult result; result.probe = probe_binary(std::make_shared<BinaryFile>(path)); return result;
    }, [this, path](const TaskResult& result) {
        if (closing_) return;
        ImportDialog dialog(path, result.probe, this);
        if (dialog.exec() == QDialog::Accepted) analyze_sample(path, dialog.options());
    });
}
void MainWindow::analyze_sample(const QString& path, const OpenOptions& options) {
    const auto project = project_; const auto old_id = analysis_id_; const auto drafts = drafts_;
    start_task(QStringLiteral("Importando e analisando com o núcleo próprio…"), [project, path, options, old_id, drafts](std::stop_token stop, ProgressCallback progress) {
        persist_drafts(project, old_id, drafts, stop);
        progress({QStringLiteral("Copiando e identificando a amostra…"), 0, 0});
        const auto sample = project->import_sample(path, stop);
        const auto stored_path = project->root() + QLatin1Char('/') + sample.relative_path;
        if (const auto reusable = project->reusable_analysis(sample, options, stop)) {
            TaskResult result; result.project = project; result.analysis_id = *reusable;
            result.snapshot = std::make_shared<AnalysisSnapshot>(project->load_analysis(*reusable, stop));
            result.summaries = project->analyses(); result.notes = project->notes(*reusable);
            result.message = QStringLiteral("Análise compatível reaproveitada pelo hash e pelas opções."); return result;
        }
        auto snapshot = std::make_shared<AnalysisSnapshot>(analyze_binary(stored_path, options, {}, stop, progress));
        if (!snapshot->metadata.sha256.isEmpty() && snapshot->metadata.sha256 != sample.sha256) throw std::runtime_error("A cópia mudou antes da análise; resultado recusado.");
        snapshot->metadata.sha256 = sample.sha256;
        snapshot->metadata.original_name = sample.original_name;
        // A cancelled analysis may still contain useful decoded evidence. Its
        // final snapshot is persisted atomically without extending exploration.
        TaskResult result; result.project = project; result.snapshot = snapshot;
        result.analysis_id = project->save_new_analysis(sample, *snapshot, {});
        result.summaries = project->analyses();
        result.notes = project->notes(result.analysis_id);
        result.message = QStringLiteral("Análise %1 · %2 funções · %3").arg(result.analysis_id.left(8)).arg(snapshot->functions.size()).arg(state_name(snapshot->state));
        return result;
    }, [this](const TaskResult& result) {
        drafts_.clear();
        analyses_->blockSignals(true); analyses_->clear();
        for (const auto& summary : result.summaries) analyses_->addItem(QStringLiteral("%1 · %2").arg(summary.name.isEmpty() ? summary.id.left(8) : summary.name, state_name(summary.state)), summary.id);
        analyses_->blockSignals(false);
        install_snapshot(result);
    });
}
void MainWindow::activate_analysis(const QString& id, std::optional<Address> navigation) {
    if (!project_ || busy_ || id == analysis_id_) return;
    const auto project = project_; const auto previous = analysis_id_; const auto drafts = drafts_;
    start_task(QStringLiteral("Carregando análise…"), [project, id, previous, drafts](std::stop_token stop, ProgressCallback) {
        persist_drafts(project, previous, drafts, stop);
        TaskResult result; result.project = project; result.analysis_id = id;
        result.snapshot = std::make_shared<AnalysisSnapshot>(project->load_analysis(id, stop));
        result.notes = project->notes(id); return result;
    }, [this, navigation](const TaskResult& result) {
        drafts_.clear(); install_snapshot(result, navigation);
    });
}
void MainWindow::install_snapshot(const TaskResult& result, std::optional<Address> navigation) {
    selection_stop_.request_stop(); ++selection_token_;
    snapshot_ = result.snapshot; analysis_id_ = result.analysis_id; selected_function_.reset(); selected_address_.reset();
    binary_explorer_->set_snapshot(snapshot_);
    chrome_->set_workspace(snapshot_->metadata.original_name);
    { const QSignalBlocker blocked(documents_); while (documents_->count()) documents_->removeTab(0); }
    assistant_->set_selection({}, {});
    notes_.clear();
    for (const auto& note : result.notes) notes_.insert(note.address.text(), note);
    analyses_->blockSignals(true);
    analyses_->setCurrentIndex(analyses_->findData(analysis_id_));
    analyses_->blockSignals(false);
    central_->setCurrentIndex(1);
    for (auto* explorer : explorers_) explorer->set_snapshot(snapshot_);
    assembly_->set_snapshot({}); references_->set_snapshot(snapshot_);
    graph_->show_function(nullptr); ir_->clear();
    pseudocode_->clear(); ssa_->clear();
    function_heading_->setText(QStringLiteral("Selecione uma função"));
    ir_status_->setText(QStringLiteral("IR própria e parcial · sem recuperação de tipos/variáveis de alto nível."));
    byte_path_ = snapshot_->metadata.source_path;
    hex_->show_file(byte_path_);
    const auto& metadata = snapshot_->metadata;
    sample_status_->setText(QStringLiteral("%1 · %2 · %3 funções").arg(format_name(metadata.format), architecture_name(metadata.architecture)).arg(snapshot_->functions.size()));
    status_->setText(QFileInfo(project_->root()).fileName() + QStringLiteral("  ·  ") + state_name(snapshot_->state));
    status_->setToolTip(project_->root());
    QStringList lines{
        QStringLiteral("Amostra\n%1").arg(metadata.original_name),
        QStringLiteral("Formato / arquitetura\n%1 · %2 · %3 bits").arg(format_name(metadata.format), architecture_name(metadata.architecture)).arg(metadata.bits),
        QStringLiteral("Estado\n%1").arg(state_name(snapshot_->state)),
        QStringLiteral("SHA-256\n%1").arg(metadata.sha256),
        QStringLiteral("Tamanho / base\n%1 bytes · %2").arg(metadata.size).arg(hex(metadata.image_base)),
        QStringLiteral("Motor\n%1\n%2").arg(snapshot_->engine_version, decoder_description()),
        QStringLiteral("Capacidades\n%1").arg(snapshot_->capabilities.join(QStringLiteral(", "))),
        QStringLiteral("Entropia global\n%1 bits/byte").arg(snapshot_->entropy, 0, 'f', 3),
        QStringLiteral("Strings\n%1%2").arg(snapshot_->strings.size()).arg(snapshot_->strings_truncated ? QStringLiteral(" · saída limitada") : QString{}),
    };
    for (const auto& warning : metadata.warnings) lines.push_back(QStringLiteral("Limite do carregador\n") + warning);
    for (const auto& warning : snapshot_->warnings) lines.push_back(QStringLiteral("Limite da análise\n") + warning);
    metadata_->setPlainText(lines.join(QStringLiteral("\n\n")));
    set_note_address({}); update_bookmarks(); update_audit();
    if (navigation) navigate(*navigation, false);
    else if (!snapshot_->functions.isEmpty()) navigate(snapshot_->functions.first().address);
    update_actions();
}

void MainWindow::navigate(Address address, bool record_history) {
    if (!snapshot_ || busy_ || closing_) return;
    central_->setCurrentIndex(1);
    qsizetype index = -1;
    for (qsizetype current = 0; current < snapshot_->functions.size(); ++current) {
        const auto& function = snapshot_->functions[current];
        if (function.address == address) { index = current; break; }
    }
    if (index < 0)
        for (qsizetype current = 0; current < snapshot_->functions.size() && index < 0; ++current)
            for (const auto& instruction : snapshot_->functions[current].instructions)
                if (instruction.address == address) { index = current; break; }
    if (record_history) {
        const auto item = std::pair<QString, Address>(analysis_id_, address);
        if (history_index_ < 0 || history_[history_index_] != item) {
            history_.resize(history_index_ + 1); history_.push_back(item); history_index_ = history_.size() - 1;
        }
    }
    if (index >= 0 && selected_function_ && *selected_function_ == index && document_snapshot_.lock() == snapshot_) {
        // Moving inside a loaded function keeps its documents, Find query and
        // dataflow jobs intact. Only the exact source location changes.
        const auto& function = snapshot_->functions[index];
        open_function_tab(function.address, function.name);
        select_instruction(address);
        update_actions();
        return;
    }
    selected_function_.reset();
    selected_address_ = address;
    selection_stop_.request_stop(); selection_stop_ = std::stop_source{};
    const auto token = ++selection_token_;
    set_note_address(address);
    if (index < 0) {
        binary_explorer_->select_address(address);
        assistant_->set_selection({}, {});
        function_heading_->setText(QStringLiteral("Endereço · ") + address.text());
        ir_->clear(); ir_status_->setText(QStringLiteral("Nenhuma função descoberta nesse endereço. Consulte as referências e os bytes."));
        pseudocode_->clear(); ssa_->clear(); decompiler_status_->setText(QStringLiteral("Nenhuma função descoberta para decompilar."));
        assembly_->set_snapshot({}); references_->set_snapshot(snapshot_); graph_->show_function(nullptr);
        if (const auto offset = reworkbench::file_offset(snapshot_->metadata, address)) show_offset(*offset);
        update_actions(); return;
    }
    selected_function_ = index;
    document_snapshot_ = snapshot_;
    const auto& function = snapshot_->functions[index];
    binary_explorer_->select_address(function.address);
    open_function_tab(function.address, function.name);
    assistant_->set_selection(analysis_id_ + QLatin1Char('/') + function.address.text(), function.name + QStringLiteral(" · ") + function.address.text());
    function_heading_->setText(snapshot_->metadata.original_name + QStringLiteral("  ›  ") + function.name + QStringLiteral("  ›  ") + hex(address.value));
    assembly_->set_snapshot(snapshot_, index); references_->set_snapshot(snapshot_, index);
    graph_->show_function(&function);
    ir_->setPlainText(QStringLiteral("Construindo representação intermediária…"));
    pseudocode_->setPlainText(QStringLiteral("Analisando fluxo de dados e construindo pseudocódigo…")); ssa_->clear();
    decompiler_status_->setText(QStringLiteral("Decompilando…"));
    auto* decompilation = new QFutureWatcher<DecompilationResult>(this);
    connect(decompilation, &QFutureWatcher<DecompilationResult>::finished, this, [this, decompilation, token] {
        const auto result = decompilation->result(); decompilation->deleteLater();
        if (token != selection_token_ || closing_) return;
        pseudocode_->setPlainText(result.pseudocode); ssa_->setPlainText(result.ssa_text);
        static_cast<CodeEditor*>(pseudocode_)->set_source_addresses(result.pseudocode_addresses);
        static_cast<CodeEditor*>(ssa_)->set_source_addresses(result.ssa_addresses);
        if (selected_address_) {
            static_cast<CodeEditor*>(pseudocode_)->select_address(*selected_address_);
            static_cast<CodeEditor*>(ssa_)->select_address(*selected_address_);
        }
        decompiler_status_->setText(QStringLiteral("%1/%2 instruções modeladas · fluxo de dados %3%4").arg(result.modeled_instructions).arg(result.total_instructions)
            .arg(result.dataflow_converged ? QStringLiteral("convergente") : QStringLiteral("parcial"), result.output_truncated ? QStringLiteral(" · saída limitada") : QString{}));
        decompiler_status_->setToolTip(result.warnings.join(QLatin1Char('\n')));
    });
    decompilation->setFuture(QtConcurrent::run([snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_), project = project_, index, stop = selection_stop_.get_token()] {
        try { return cached_decompile(project, *snapshot, index, stop); }
        catch (const std::exception& exception) { DecompilationResult result; result.pseudocode = exception_text(exception); return result; }
        catch (...) { DecompilationResult result; result.pseudocode = QStringLiteral("Decompilação indisponível."); return result; }
    }));
    auto* watcher = new QFutureWatcher<RenderedFunction>(this);
    connect(watcher, &QFutureWatcher<RenderedFunction>::finished, this, [this, watcher, token] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (token != selection_token_ || closing_) return;
        ir_->setPlainText(result.text);
        static_cast<CodeEditor*>(ir_)->set_source_addresses(result.source_addresses);
        if (selected_address_) static_cast<CodeEditor*>(ir_)->select_address(*selected_address_);
        ir_status_->setText(QStringLiteral("IR própria · %1/%2 instruções modeladas · operações não modeladas permanecem opacas").arg(result.modeled_instructions).arg(result.total_instructions));
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_), index, stop = selection_stop_.get_token()] {
        try { return render_ir(snapshot->functions[index], snapshot->metadata.architecture, stop); }
        catch (const std::exception& exception) { RenderedFunction result; result.text = exception_text(exception); return result; }
        catch (...) { RenderedFunction result; result.text = QStringLiteral("IR indisponível para a função."); return result; }
    }));
    select_instruction(address);
    update_actions();
}
void MainWindow::history_step(int direction) {
    const auto next = history_index_ + direction;
    if (next < 0 || next >= history_.size() || busy_) return;
    history_index_ = next;
    const auto [id, address] = history_[history_index_];
    if (id == analysis_id_) navigate(address, false);
    else activate_analysis(id, address);
    update_actions();
}
void MainWindow::jump() {
    bool accepted = false;
    const auto text = QInputDialog::getText(this, QStringLiteral("Ir para endereço"), QStringLiteral("Endereço virtual hexadecimal:"), QLineEdit::Normal, QStringLiteral("ram:0x"), &accepted);
    if (accepted) try { navigate(parse_address(text)); } catch (const std::exception& exception) { error(exception_text(exception)); }
}
void MainWindow::examine_path(const QString& path) {
    byte_path_ = path; hex_->show_file(path);
    show_bottom_tab(hex_);
}
void MainWindow::show_offset(quint64 offset) {
    const auto path = snapshot_ ? snapshot_->metadata.source_path : byte_path_;
    if (path.isEmpty()) return;
    hex_->show_file(path, offset); show_bottom_tab(hex_);
}

void MainWindow::set_note_address(std::optional<Address> address) {
    note_address_ = address; loading_note_ = true;
    Note note;
    if (address) note = drafts_.contains(address->text()) ? drafts_.value(address->text()) : notes_.value(address->text(), Note{*address, {}, false});
    note_editor_->setPlainText(note.text); bookmark_->setChecked(note.bookmarked);
    note_heading_->setText(address ? address->text() : QStringLiteral("Selecione um endereço para anotar"));
    loading_note_ = false; update_actions();
}
void MainWindow::record_draft() {
    if (loading_note_ || !note_address_ || !project_) return;
    drafts_.insert(note_address_->text(), Note{*note_address_, note_editor_->toPlainText(), bookmark_->isChecked()});
    note_heading_->setText(note_address_->text() + QStringLiteral(" · alterações não salvas"));
    update_actions();
}
void MainWindow::save_project() {
    if (!project_ || busy_) return;
    const auto project = project_; const auto id = analysis_id_; const auto drafts = drafts_;
    start_task(QStringLiteral("Salvando notas…"), [project, id, drafts](std::stop_token stop, ProgressCallback) {
        persist_drafts(project, id, drafts, stop);
        TaskResult result; result.notes = id.isEmpty() ? QVector<Note>{} : project->notes(id);
        result.message = QStringLiteral("Projeto salvo."); return result;
    }, [this](const TaskResult& result) {
        drafts_.clear(); notes_.clear();
        for (const auto& note : result.notes) notes_.insert(note.address.text(), note);
        update_bookmarks(); set_note_address(note_address_);
    });
}
void MainWindow::update_bookmarks() {
    bookmarks_->clear();
    qsizetype count = 0;
    for (const auto& note : notes_)
        if (note.bookmarked) {
            if (count++ >= 200) {
                bookmarks_->addItem(QStringLiteral("Lista limitada a 200 marcadores. Use Ir para endereço para acessar outras notas."));
                break;
            }
            auto* item = new QListWidgetItem(note.address.text() + QStringLiteral(" · ") + note.text.section(QLatin1Char('\n'), 0, 0).left(80), bookmarks_);
            item->setData(Qt::UserRole, note.address.text());
        }
}
void MainWindow::update_audit() {
    audit_stop_.request_stop(); audit_stop_ = std::stop_source{};
    const auto token = ++audit_token_;
    audit_->clear();
    audit_->addItem(QStringLiteral("Pistas de imports · exigem revisão manual; não comprovam vulnerabilidade."));
    if (!snapshot_) return;
    using Rows = QVector<std::pair<QString, QString>>;
    auto* watcher = new QFutureWatcher<Rows>(this);
    connect(watcher, &QFutureWatcher<Rows>::finished, this, [this, watcher, token] {
        const auto rows = watcher->result(); watcher->deleteLater();
        if (token != audit_token_ || closing_) return;
        for (const auto& [text, address] : rows) {
            auto* item = new QListWidgetItem(text, audit_);
            if (!address.isEmpty()) item->setData(Qt::UserRole, address);
        }
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_), stop = audit_stop_.get_token()] {
        Rows rows;
        try {
            const QStringList names{QStringLiteral("gets"), QStringLiteral("strcpy"), QStringLiteral("strcat"), QStringLiteral("sprintf")};
            QSet<QString> import_slots;
            bool limited = false;
            for (const auto& imported : snapshot->metadata.imports) {
                check_cancelled(stop);
                auto name = imported.name.section(QLatin1Char('@'), 0, 0);
                if (name.startsWith(QStringLiteral("__imp_"))) name.remove(0, 6);
                if (name.startsWith(QLatin1Char('_'))) name.remove(0, 1);
                if (!names.contains(name)) continue;
                if (rows.size() >= 100) { limited = true; continue; }
                const auto address = imported.slot ? imported.slot->text() : QString{};
                rows.append({imported.name.left(512) + QStringLiteral(" · ") + imported.library.left(512) + QStringLiteral(" · revisar limites da entrada/destino"), address});
                if (imported.slot) import_slots.insert(address);
            }
            // One pass over references, bounded output, no quadratic GUI work.
            for (const auto& reference : snapshot->references) {
                check_cancelled(stop);
                if (!import_slots.contains(reference.target.text())) continue;
                if (rows.size() >= 199) { limited = true; break; }
                rows.append({reference.source.text() + QStringLiteral(" → ") + reference.target.text() + QStringLiteral(" · ") + reference.type.left(128), reference.source_function.value_or(reference.source).text()});
            }
            if (limited) rows.append({QStringLiteral("Lista limitada a 200 registros. Consulte imports e referências para continuar."), {}});
        } catch (const Cancelled&) { rows.clear(); }
        catch (const std::exception& exception) { rows = {{exception_text(exception), {}}}; }
        catch (...) { rows = {{QStringLiteral("Não foi possível carregar as pistas."), {}}}; }
        return rows;
    }));
}
void MainWindow::rename_function() {
    if (!project_ || !snapshot_ || !selected_function_ || busy_) return;
    const auto index = *selected_function_; const auto address = snapshot_->functions[index].address;
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Renomear função"), QStringLiteral("Novo nome:"), QLineEdit::Normal, snapshot_->functions[index].name, &accepted).trimmed();
    if (!accepted) return;
    if (!QRegularExpression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,239}$")).match(name).hasMatch()) { error(QStringLiteral("Use um identificador de até 240 caracteres, sem espaços.")); return; }
    const auto project = project_; const auto id = analysis_id_;
    start_task(QStringLiteral("Salvando nome da função…"), [project, id, address, name](std::stop_token stop, ProgressCallback) {
        check_cancelled(stop); project->rename_function(id, address, name); TaskResult result; result.message = QStringLiteral("Função renomeada."); return result;
    }, [this, index, address, name](const TaskResult&) {
        auto updated = std::make_shared<AnalysisSnapshot>(*snapshot_);
        updated->functions[index].name = name; ++updated->revision; snapshot_ = std::move(updated);
        for (auto* explorer : explorers_) explorer->set_snapshot(snapshot_);
        navigate(address, false);
    });
}
void MainWindow::export_report() {
    if (!project_ || !snapshot_ || busy_) return;
    const auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar investigação"), QStringLiteral("analise-%1.json").arg(analysis_id_.left(8)), QStringLiteral("JSON (*.json)"));
    if (destination.isEmpty()) return;
    const auto project = project_; const auto id = analysis_id_; const auto drafts = drafts_;
    start_task(QStringLiteral("Exportando investigação…"), [project, id, drafts, destination](std::stop_token stop, ProgressCallback) {
        persist_drafts(project, id, drafts, stop); project->export_report(id, destination, stop);
        TaskResult result; result.notes = project->notes(id); result.message = QStringLiteral("Exportado: ") + destination; return result;
    }, [this](const TaskResult& result) {
        drafts_.clear(); notes_.clear(); for (const auto& note : result.notes) notes_.insert(note.address.text(), note);
        update_bookmarks(); set_note_address(note_address_);
    });
}
void MainWindow::export_ir() {
    if (!project_ || !snapshot_ || !selected_function_ || busy_) return;
    const auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar IR parcial"), QStringLiteral("funcao-%1.ir.txt").arg(hex(snapshot_->functions[*selected_function_].address.value)), QStringLiteral("IR em texto (*.txt)"));
    if (destination.isEmpty()) return;
    const auto project = project_; const auto id = analysis_id_; const auto snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_); const auto index = *selected_function_;
    start_task(QStringLiteral("Exportando representação intermediária…"), [project, id, snapshot, index, destination](std::stop_token stop, ProgressCallback) {
        const auto result = render_ir(snapshot->functions[index], snapshot->metadata.architecture, stop);
        check_cancelled(stop); project->export_ir(id, snapshot->functions[index].address, result.text, destination, stop);
        TaskResult task; task.message = QStringLiteral("Exportado: ") + destination; return task;
    });
}
void MainWindow::about() {
    QMessageBox dialog(QMessageBox::NoIcon, QStringLiteral("Rework"),
        QStringLiteral("Bancada independente de engenharia reversa\n\nAplicação e núcleo próprios em C++20, interface Qt, carregadores nativos, projetos SQLite e IR parcial.\n\nGhidra é inspiração de produto e não é uma dependência desta aplicação.\n%1\n\nA decompilação de alto nível está em desenvolvimento; a IR mostra explicitamente semântica ainda não modelada.").arg(decoder_description()), QMessageBox::Ok, this);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setWindowIcon(application_icon());
    dialog.setIconPixmap(application_icon().pixmap(QSize(80, 80), devicePixelRatioF()));
    dialog.setInformativeText(QStringLiteral("Codicons — Microsoft e colaboradores. Ícones sob CC BY 4.0; código sob MIT.\nSeti UI — Jesse Weed, MIT.\nhttps://github.com/microsoft/vscode-codicons\nhttps://github.com/jesseweed/seti-ui"));
    QString attribution;
    for (const auto* path : {":/codicons/NOTICE.md", ":/codicons/LICENSE", ":/codicons/LICENSE-CODE", ":/seti/NOTICE.md", ":/seti/LICENSE.md"}) {
        QFile file(QString::fromLatin1(path));
        if (file.open(QIODevice::ReadOnly)) attribution += QStringLiteral("\n\n") + QString::fromUtf8(file.readAll());
    }
    if (!attribution.isEmpty()) dialog.setDetailedText(attribution);
    dialog.exec();
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (allow_close_ || (!busy_ && (!project_ || drafts_.isEmpty()))) {
        allow_close_ = true;
        selection_stop_.request_stop();
        audit_stop_.request_stop();
        settings_.setValue(QStringLiteral("window/geometry"), saveGeometry());
        save_workspace();
        settings_.sync(); event->accept(); return;
    }
    event->ignore(); closing_ = true; selection_stop_.request_stop();
    if (busy_) { stop_.request_stop(); status_->setText(QStringLiteral("Interrompendo antes de fechar…")); update_actions(); return; }
    if (project_ && !drafts_.isEmpty()) {
        const auto project = project_; const auto id = analysis_id_; const auto drafts = drafts_;
        start_task(QStringLiteral("Salvando notas antes de fechar…"), [project, id, drafts](std::stop_token, ProgressCallback) {
            persist_drafts(project, id, drafts, {}); TaskResult result; return result;
        }, [this](const TaskResult&) { drafts_.clear(); allow_close_ = true; close(); });
    }
}
} // namespace reworkbench::ui
