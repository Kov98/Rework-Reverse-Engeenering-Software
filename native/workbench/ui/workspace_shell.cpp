#include "main_window.hpp"
#include "assistant_panel.hpp"
#include "application_brand.hpp"
#include "hex_view.hpp"
#include "graph_view.hpp"
#include "import_dialog.hpp"
#include "binary_explorer.hpp"
#include "window_chrome.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"
#include "quick_picker.hpp"
#include "code_editor.hpp"
#include "reworkbench/binary_image.hpp"
#include <QDialog>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFontDialog>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QSet>
#include <QListWidget>
#include <QShortcut>
#include <QToolButton>
#include <QFrame>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <tuple>

namespace reworkbench::ui {
namespace {
const QStringList default_views{"pseudocode", "ssa", "ir", "graph"};
QIcon activity_icon(int kind, bool active = false) {
    const QStringList names{"files", "search", "type-hierarchy", "debug", "sparkle", "settings-gear"};
    return workbench_icon(names.value(kind, "files"), theme_color(active ? "activityBar.foreground" : "activityBar.inactiveForeground"), 24);
}
}
void MainWindow::build_workspace_shell() {
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks | GroupedDragging);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    assistant_ = new AssistantPanel([this] { return assistant_context(); });
    add_dock("assistant", QStringLiteral("Assistente IA"), assistant_, Qt::RightDockWidgetArea);
    if (auto* old_header = assistant_->findChild<QFrame*>("assistantChatHeader")) old_header->hide();
    views_.insert("pseudocode", {representation_->widget(0), QStringLiteral("Pseudocódigo")});
    views_.insert("ssa", {ssa_, QStringLiteral("SSA")});
    views_.insert("ir", {ir_, QStringLiteral("IR")});
    views_.insert("graph", {graph_, QStringLiteral("Grafo")});
    const auto add = [this](const QString& key, const QString& text, const QKeySequence& shortcut, std::function<void()> run) {
        auto* action = new QAction(text, this); action->setObjectName(key); action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [run] { run(); }); actions_.insert(key, action); addAction(action);
    };
    for (const auto& key : default_views) {
        auto* action = new QAction(views_[key].second, this); action->setObjectName("view_" + key);
        action->setCheckable(true); action->setChecked(true); actions_.insert("view_" + key, action);
        connect(action, &QAction::toggled, this, [this, key](bool visible) { set_view_visible(key, visible); });
    }
    connect(representation_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto* widget = representation_->widget(index);
        for (auto it = views_.cbegin(); it != views_.cend(); ++it)
            if (it.value().first == widget) { actions_["view_" + it.key()]->setChecked(false); return; }
    });
    connect(documents_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && !busy_ && !closing_) navigate(parse_address(documents_->tabData(index).toString()));
    });
    connect(documents_, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0 && index == documents_->currentIndex() && !selected_function_ && !busy_ && !closing_)
            navigate(parse_address(documents_->tabData(index).toString()));
    });
    connect(documents_, &QTabBar::tabCloseRequested, this, [this](int index) { close_function_tab(index); });
    add("document_close", QStringLiteral("Fechar aba da função"), QKeySequence("Ctrl+W"), [this] { close_function_tab(documents_->currentIndex()); });
    add("command_palette", QStringLiteral("Paleta de comandos…"), QKeySequence("Ctrl+Shift+P"), [this] { show_command_palette(); });
    add("quick_open", QStringLiteral("Ir para função…"), QKeySequence("Ctrl+P"), [this] { show_quick_open(); });
    add("next_document", QStringLiteral("Próxima aba"), QKeySequence("Ctrl+Tab"), [this] {
        if (documents_->count() > 1) documents_->setCurrentIndex((documents_->currentIndex() + 1) % documents_->count());
    });
    add("previous_document", QStringLiteral("Aba anterior"), QKeySequence("Ctrl+Shift+Tab"), [this] {
        if (documents_->count() > 1) documents_->setCurrentIndex((documents_->currentIndex() + documents_->count() - 1) % documents_->count());
    });
    for (const auto& entry : {std::tuple{"toggle_sidebar", "Alternar explorador", "Ctrl+B", "explorer"},
                              std::tuple{"toggle_panel", "Alternar painel inferior", "Ctrl+J", "panel"},
                              std::tuple{"toggle_assistant", "Alternar assistente IA", "Ctrl+Alt+B", "assistant"}}) {
        const QString key = std::get<0>(entry), dock_key = std::get<3>(entry);
        add(key, QString::fromUtf8(std::get<1>(entry)), QKeySequence(std::get<2>(entry)), [this, dock_key] {
            auto* dock = docks_[dock_key]; dock->setVisible(!dock->isVisible()); if (dock->isVisible()) dock->raise();
        });
        actions_[key]->setCheckable(true); actions_[key]->setChecked(true);
        connect(docks_[dock_key], &QDockWidget::visibilityChanged, actions_[key], &QAction::setChecked);
    }
    add("toggle_listing", QStringLiteral("Alternar listagem lado a lado"), QKeySequence("Ctrl+\\"), [this] {
        listing_panel_->setVisible(!listing_panel_->isVisible());
    });
    add("focus_listing", QStringLiteral("Focar listagem"), QKeySequence("Ctrl+1"), [this] {
        listing_panel_->show(); central_->setCurrentIndex(1); assembly_->view()->setFocus();
    });
    add("focus_decompiler", QStringLiteral("Focar pseudocódigo"), QKeySequence("Ctrl+2"), [this] {
        actions_["view_pseudocode"]->setChecked(true); representation_->setCurrentWidget(views_["pseudocode"].first); central_->setCurrentIndex(1); pseudocode_->setFocus();
    });
    add("follow_target", QStringLiteral("Seguir destino da instrução"), QKeySequence("F12"), [this] { assembly_->activate_current(); });
    add("show_references", QStringLiteral("Mostrar referências"), QKeySequence("Shift+F12"), [this] { show_bottom_tab(references_); references_->view()->setFocus(); });
    add("layout_reset", QStringLiteral("Restaurar layout padrão"), {}, [this] { reset_workspace(); });
    auto* reduced_motion = new QAction(QStringLiteral("Reduzir movimento"), this);
    reduced_motion->setObjectName("reduce_motion"); reduced_motion->setCheckable(true);
    const bool os_reduced_motion = system_reduces_motion();
    reduced_motion->setChecked(os_reduced_motion || settings_.value("workspace/reducedMotion", false).toBool());
    reduced_motion->setEnabled(!os_reduced_motion);
    reduced_motion->setToolTip(os_reduced_motion ? QStringLiteral("Movimento reduzido pela configuração do Windows.") : QStringLiteral("Desativar transições de painéis e menus."));
    actions_.insert("reduce_motion", reduced_motion); addAction(reduced_motion);
    static const bool native_menu_animation = QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
    const auto apply_motion = [this, os_reduced_motion](bool reduced) {
        auto options = dockOptions(); options.setFlag(AnimatedDocks, !reduced && !os_reduced_motion); setDockOptions(options);
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, native_menu_animation && !reduced && !os_reduced_motion);
    };
    connect(reduced_motion, &QAction::toggled, this, [this, apply_motion](bool reduced) {
        apply_motion(reduced); settings_.setValue("workspace/reducedMotion", reduced);
    });
    apply_motion(reduced_motion->isChecked());
    add("font_increase", QStringLiteral("Aumentar fonte do código"), QKeySequence::ZoomIn, [this] {
        code_font_.setPointSizeF(std::min(24.0, code_font_.pointSizeF() + 1)); apply_code_font();
        settings_.setValue("workspace/fontCustomized", true);
    });
    add("font_decrease", QStringLiteral("Diminuir fonte do código"), QKeySequence::ZoomOut, [this] {
        code_font_.setPointSizeF(std::max(8.0, code_font_.pointSizeF() - 1)); apply_code_font();
        settings_.setValue("workspace/fontCustomized", true);
    });
    add("font_reset", QStringLiteral("Restaurar fonte do código"), QKeySequence("Ctrl+0"), [this] { code_font_ = default_code_font(); apply_code_font(); settings_.setValue("workspace/fontCustomized", false); });
    add("font_choose", QStringLiteral("Escolher fonte do código…"), {}, [this] {
        bool accepted = false;
        const auto font = QFontDialog::getFont(&accepted, code_font_, this, QStringLiteral("Fonte do código"), QFontDialog::DontUseNativeDialog);
        if (accepted) { code_font_ = font; code_font_.setPointSizeF(std::clamp(font.pointSizeF(), 8.0, 24.0)); apply_code_font(); settings_.setValue("workspace/fontCustomized", true); }
    });
    // Give real commands their own action glyphs; an unknown command keeps an
    // empty aligned slot instead of presenting a misleading generic symbol.
    const std::pair<const char*, const char*> command_icons[]{
        {"new", "folder"}, {"open", "folder"}, {"import", "file-binary"}, {"bytes", "file-binary"},
        {"json", "export"}, {"ir", "export"}, {"pseudocode", "export"}, {"patch", "export"},
        {"diff", "split-horizontal"}, {"emulate", "chip"}, {"symbols", "symbol-namespace"},
        {"enrichment", "symbol-namespace"}, {"extensions", "library"}, {"debugger", "debug"},
        {"jump", "arrow-right"}, {"cancel", "close"}, {"document_close", "close"},
        {"command_palette", "search"}, {"quick_open", "symbol-method"},
        {"next_document", "arrow-right"}, {"previous_document", "arrow-left"},
        {"toggle_listing", "split-horizontal"}, {"focus_listing", "file-binary"},
        {"focus_decompiler", "symbol-method"}, {"follow_target", "arrow-right"},
        {"show_references", "references"}, {"layout_reset", "layout-panel"},
        {"font_choose", "settings-gear"}, {"font_increase", "add"}, {"font_decrease", "zoom-out"},
        {"view_graph", "graph"}, {"view_ssa", "type-hierarchy"}, {"view_ir", "chip"},
        {"view_pseudocode", "symbol-method"}
    };
    for (const auto& [key, glyph] : command_icons)
        if (auto* action = actions_.value(QLatin1String(key)); action && action->icon().isNull())
            action->setIcon(workbench_icon(QLatin1String(glyph)));
    actions_["about"]->setIcon(application_icon());
    auto* activity = new QToolBar(QStringLiteral("Atividades"), this); activity->setObjectName("activityBar");
    activity->setMovable(false); activity->setFloatable(false); activity->setIconSize(QSize(24, 24)); addToolBar(Qt::LeftToolBarArea, activity);
    auto* explorer = activity->addAction(activity_icon(0, true), QStringLiteral("Explorador"));
    explorer->setCheckable(true); explorer->setChecked(true);
    connect(explorer, &QAction::triggered, actions_["toggle_sidebar"], &QAction::trigger);
    connect(docks_["explorer"], &QDockWidget::visibilityChanged, explorer, [explorer](bool visible) {
        explorer->setChecked(visible); explorer->setIcon(activity_icon(0, visible));
    });
    auto* search = activity->addAction(activity_icon(1), QStringLiteral("Buscar funções · Ctrl+P"));
    connect(search, &QAction::triggered, actions_["quick_open"], &QAction::trigger);
    auto* graph = activity->addAction(activity_icon(2), QStringLiteral("Grafo da função"));
    connect(graph, &QAction::triggered, this, [this] {
        actions_["view_graph"]->setChecked(true); representation_->setCurrentWidget(graph_);
        if (snapshot_) central_->setCurrentIndex(1);
    });
    actions_["debugger"]->setIcon(activity_icon(3)); activity->addAction(actions_["debugger"]);
    auto* ai = activity->addAction(activity_icon(4, true), QStringLiteral("Assistente IA · preparação local"));
    ai->setCheckable(true); ai->setChecked(true);
    connect(ai, &QAction::triggered, actions_["toggle_assistant"], &QAction::trigger);
    connect(docks_["assistant"], &QDockWidget::visibilityChanged, ai, [ai](bool visible) {
        ai->setChecked(visible); ai->setIcon(activity_icon(4, visible));
    });
    auto* spacer = new QWidget; spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding); activity->addWidget(spacer);
    auto* settings = activity->addAction(activity_icon(5), QStringLiteral("Personalizar espaço de trabalho"));
    connect(settings, &QAction::triggered, this, [this, activity, settings] {
        QMenu menu(this);
        for (const auto& key : {"font_choose", "font_increase", "font_decrease", "font_reset", "reduce_motion", "layout_reset"}) menu.addAction(actions_[key]);
        const auto* button = activity->widgetForAction(settings);
        menu.exec(button->mapToGlobal(QPoint(button->width(), 0)));
    });
    for (const auto& key : {"explorer", "assistant"}) {
        auto* dock = docks_[key]; auto* header = new QWidget; header->setObjectName("dockHeader"); header->setFixedHeight(33);
        auto* row = new QHBoxLayout(header); row->setContentsMargins(12, 0, 4, 0); row->setSpacing(2);
        auto* label = new QLabel(key == QStringLiteral("explorer") ? QStringLiteral("Explorador") : QStringLiteral("Assistente IA"));
        label->setAttribute(Qt::WA_TransparentForMouseEvents); row->addWidget(label); row->addStretch();
        auto* more = new QToolButton; more->setIcon(chrome_icon(Glyph::More)); more->setToolTip(QStringLiteral("Opções do painel")); row->addWidget(more);
        connect(more, &QToolButton::clicked, this, [this, dock, more] {
            QMenu menu(this);
            if (dock == docks_["explorer"]) { menu.addAction(actions_["import"]); menu.addAction(actions_["open"]); menu.addSeparator(); }
            menu.addAction(QStringLiteral("Destacar painel"), dock, [dock] { dock->setFloating(true); });
            menu.addAction(actions_["layout_reset"]); menu.exec(more->mapToGlobal(QPoint(0, more->height())));
        });
        auto* close = new QToolButton; close->setIcon(chrome_icon(Glyph::Close)); close->setToolTip(QStringLiteral("Ocultar painel"));
        connect(close, &QToolButton::clicked, dock, &QDockWidget::hide); row->addWidget(close); dock->setTitleBarWidget(header);
    }
    auto* panel_tools = new QWidget; auto* tools = new QHBoxLayout(panel_tools); tools->setContentsMargins(0, 0, 4, 0); tools->setSpacing(1);
    auto* more = new QToolButton; more->setIcon(chrome_icon(Glyph::More)); more->setToolTip(QStringLiteral("Posição do painel")); tools->addWidget(more);
    connect(more, &QToolButton::clicked, this, [this, more] {
        QMenu menu(this); auto* dock = docks_["panel"];
        menu.addAction(QStringLiteral("Mover para baixo"), this, [this, dock] { dock->setFloating(false); addDockWidget(Qt::BottomDockWidgetArea, dock); });
        menu.addAction(QStringLiteral("Mover para a direita"), this, [this, dock] { dock->setFloating(false); addDockWidget(Qt::RightDockWidgetArea, dock); });
        menu.addAction(QStringLiteral("Destacar painel"), dock, [dock] { dock->setFloating(true); });
        menu.exec(more->mapToGlobal(QPoint(0, more->height())));
    });
    auto* close = new QToolButton; close->setIcon(chrome_icon(Glyph::Close)); close->setToolTip(QStringLiteral("Ocultar painel · Ctrl+J"));
    connect(close, &QToolButton::clicked, actions_["toggle_panel"], &QAction::trigger); tools->addWidget(close);
    bottom_panel_->setCornerWidget(panel_tools);
    auto* split = new QToolButton; split->setIcon(chrome_icon(Glyph::Split)); split->setToolTip(QStringLiteral("Alternar listagem · Ctrl+\\"));
    connect(split, &QToolButton::clicked, actions_["toggle_listing"], &QAction::trigger); representation_->setCornerWidget(split);
    resizeDocks({docks_["explorer"]}, {232}, Qt::Horizontal);
    resizeDocks({docks_["assistant"]}, {290}, Qt::Horizontal);
    resizeDocks({docks_["panel"]}, {150}, Qt::Vertical);
    default_layout_ = saveState(3);
}
void MainWindow::set_view_visible(const QString& key, bool visible) {
    if (!views_.contains(key)) return;
    const auto& view = views_[key]; const int index = representation_->indexOf(view.first);
    if (visible && index < 0) representation_->addTab(view.first, view.second);
    if (!visible && index >= 0) representation_->removeTab(index);
}
void MainWindow::restore_workspace() {
    if (settings_.contains("workspace/v2/state")) restoreState(settings_.value("workspace/v2/state").toByteArray(), 3);
    const auto order = settings_.value("workspace/v2/views", default_views).toStringList();
    while (representation_->count()) representation_->removeTab(0);
    QSet<QString> restored;
    for (const auto& key : order) if (views_.contains(key) && !restored.contains(key)) {
        representation_->addTab(views_[key].first, views_[key].second); restored.insert(key);
    }
    for (auto it = views_.cbegin(); it != views_.cend(); ++it) {
        const QSignalBlocker blocked(actions_["view_" + it.key()]); actions_["view_" + it.key()]->setChecked(restored.contains(it.key()));
    }
    const auto active = settings_.value("workspace/v2/activeView").toString();
    if (views_.contains(active) && restored.contains(active)) representation_->setCurrentWidget(views_[active].first);
    if (settings_.contains("workspace/v2/editorSplit")) editor_split_->restoreState(settings_.value("workspace/v2/editorSplit").toByteArray());
    else editor_split_->setSizes({340, 470});
    listing_panel_->setVisible(settings_.value("workspace/v2/listing", true).toBool());
    bottom_panel_->setCurrentIndex(std::clamp(settings_.value("workspace/v2/panelTab", 0).toInt(), 0, bottom_panel_->count() - 1));
    code_font_ = default_code_font();
    if (settings_.contains("workspace/codeFamily"))
        code_font_ = QFont(settings_.value("workspace/codeFamily").toString(), std::clamp(settings_.value("workspace/codeSize", 11).toInt(), 8, 24));
    if (settings_.contains("workspace/codeFont")) {
        QFont restored;
        if (restored.fromString(settings_.value("workspace/codeFont").toString())) code_font_ = restored;
    }
    if (code_font_.pointSizeF() <= 0) code_font_.setPointSizeF(code_font_.pixelSize() * 72.0 / logicalDpiY());
    code_font_.setPointSizeF(std::clamp(code_font_.pointSizeF(), 8.0, 24.0));
    if (settings_.value("workspace/typographyVersion", 0).toInt() < 3) {
        QFont legacy(code_font_.family(), code_font_.pointSize());
        legacy.setStyleHint(QFont::Monospace); legacy.setFixedPitch(true);
        if ((code_font_.family() == "Consolas" || code_font_.family() == "Cascadia Code") &&
            (code_font_.pointSizeF() == 10 || code_font_.pointSizeF() == 11) && code_font_ == legacy &&
            !settings_.value("workspace/fontCustomized", false).toBool())
            code_font_ = default_code_font();
        settings_.setValue("workspace/typographyVersion", 3);
    }
    code_font_ = resolve_code_font(code_font_);
    apply_code_font();
}
void MainWindow::save_workspace() {
    settings_.setValue("workspace/v2/state", saveState(3));
    settings_.setValue("workspace/v2/editorSplit", editor_split_->saveState());
    settings_.setValue("workspace/v2/listing", !listing_panel_->isHidden());
    settings_.setValue("workspace/v2/panelTab", bottom_panel_->currentIndex());
    QStringList order; QString active;
    for (int index = 0; index < representation_->count(); ++index)
        for (auto it = views_.cbegin(); it != views_.cend(); ++it) if (representation_->widget(index) == it.value().first) {
            order << it.key(); if (index == representation_->currentIndex()) active = it.key();
        }
    settings_.setValue("workspace/v2/views", order); settings_.setValue("workspace/v2/activeView", active);
}
void MainWindow::reset_workspace() {
    for (auto* dock : docks_) { dock->setFloating(false); dock->show(); }
    restoreState(default_layout_, 3);
    while (representation_->count()) representation_->removeTab(0);
    for (const auto& key : default_views) {
        set_view_visible(key, true);
        const QSignalBlocker blocked(actions_["view_" + key]); actions_["view_" + key]->setChecked(true);
    }
    editor_split_->setOrientation(Qt::Horizontal); editor_split_->setSizes({340, 470}); listing_panel_->show();
    docks_["explorer"]->show(); docks_["explorer"]->raise();
    docks_["assistant"]->show(); docks_["assistant"]->raise();
    docks_["panel"]->show(); bottom_panel_->setCurrentWidget(references_);
    resizeDocks({docks_["explorer"]}, {232}, Qt::Horizontal);
    resizeDocks({docks_["assistant"]}, {290}, Qt::Horizontal);
    resizeDocks({docks_["panel"]}, {150}, Qt::Vertical);
    save_workspace();
}
void MainWindow::apply_code_font() {
    for (auto* editor : {ir_, pseudocode_, ssa_, console_}) editor->setFont(code_font_);
    for (auto* editor : hex_->findChildren<QPlainTextEdit*>()) editor->setFont(code_font_);
    const auto table_font = [this](QTableView* view) {
        view->setFont(code_font_);
        view->horizontalHeader()->setFont(interface_font());
        const int height = std::max(23, view->fontMetrics().height() + 6);
        view->verticalHeader()->setMinimumSectionSize(height);
        view->verticalHeader()->setDefaultSectionSize(height);
    };
    table_font(assembly_->view());
    for (auto* explorer : explorers_) table_font(explorer->view());
    graph_->setFont(code_font_);
    if (snapshot_ && selected_function_ && *selected_function_ >= 0 && *selected_function_ < snapshot_->functions.size()) {
        const auto previous_transform = graph_->transform();
        graph_->show_function(&snapshot_->functions[*selected_function_]);
        graph_->setTransform(previous_transform);
        if (note_address_) graph_->select_address(*note_address_);
    }
    settings_.setValue("workspace/codeFamily", code_font_.family()); settings_.setValue("workspace/codeSize", code_font_.pointSize());
    settings_.setValue("workspace/codeFont", code_font_.toString());
}
void MainWindow::open_function_tab(Address address, const QString& name) {
    const QSignalBlocker blocked(documents_);
    int index = -1;
    for (int n = 0; n < documents_->count(); ++n) if (documents_->tabData(n).toString() == address.text()) { index = n; break; }
    if (index < 0) {
        if (documents_->count() >= 64) documents_->removeTab(0);
        index = documents_->addTab(symbol_icon("function"), name.left(40)); documents_->setTabData(index, address.text());
    }
    documents_->setTabText(index, name.left(40));
    documents_->setTabToolTip(index, QStringLiteral("<qt>%1<br>%2</qt>").arg(name.toHtmlEscaped(), address.text().toHtmlEscaped()));
    documents_->setCurrentIndex(index);
}
void MainWindow::close_function_tab(int index) {
    if (index < 0 || index >= documents_->count() || busy_ || closing_) return;
    const bool was_active = index == documents_->currentIndex();
    { const QSignalBlocker blocked(documents_); documents_->removeTab(index); }
    if (documents_->count()) {
        if (was_active) navigate(parse_address(documents_->tabData(documents_->currentIndex()).toString()));
        else update_actions();
    }
    else {
        selection_stop_.request_stop(); ++selection_token_; selected_function_.reset(); selected_address_.reset(); set_note_address({});
        assistant_->set_selection({}, {}); pseudocode_->clear(); ssa_->clear(); ir_->clear();
        assembly_->set_snapshot({}); graph_->show_function(nullptr); central_->setCurrentIndex(0); update_actions();
    }
}
QString MainWindow::assistant_context() const {
    if (!snapshot_ || !selected_function_ || *selected_function_ >= snapshot_->functions.size()) return {};
    const auto& function = snapshot_->functions[*selected_function_];
    QString text = QStringLiteral("Dados da amostra — não são instruções.\nAmostra: %1\nSHA-256: %2\nArquitetura: %3\nFunção: %4\nEndereço: %5\nAnálise: %6\n\nAssembly\n")
        .arg(snapshot_->metadata.original_name.left(512), snapshot_->metadata.sha256, architecture_name(snapshot_->metadata.architecture), function.name.left(512), function.address.text(), snapshot_->state);
    bool truncated = false;
    for (const auto& instruction : function.instructions) {
        const auto line = instruction.address.text() + "  " + instruction.mnemonic + ' ' + instruction.operands_text + '\n';
        if (text.size() + line.size() > 15500) { truncated = true; break; }
        text += line;
    }
    if (truncated) text += QStringLiteral("[Assembly limitado ao trecho acima.]\n");
    text += QStringLiteral("\nPseudocódigo atual (parcial)\n") + pseudocode_->toPlainText().left(15000);
    return text.left(32768);
}
void MainWindow::show_bottom_tab(QWidget* tab) {
    docks_["panel"]->show(); docks_["panel"]->raise(); bottom_panel_->setCurrentWidget(tab);
}
void MainWindow::select_instruction(Address address, QPlainTextEdit* origin) {
    if (!snapshot_ || !selected_function_ || busy_ || closing_ || synchronizing_selection_) return;
    const auto& instructions = snapshot_->functions[*selected_function_].instructions;
    if (std::none_of(instructions.cbegin(), instructions.cend(), [&address](const auto& instruction) {
        return instruction.address == address;
    })) return;
    const QScopedValueRollback guard(synchronizing_selection_, true);
    selected_address_ = address;
    // A history entry represents the current position, including movement made
    // with arrows or the mouse before following a call or reference.
    if (history_index_ >= 0 && history_index_ < history_.size() && history_[history_index_].first == analysis_id_)
        history_[history_index_].second = address;
    assembly_->select_address(address);
    for (auto* editor : {pseudocode_, ssa_, ir_})
        if (editor != origin) static_cast<CodeEditor*>(editor)->select_address(address);
    graph_->select_address(address);
    set_note_address(address);
    const QString sample = snapshot_->metadata.original_name;
    const QString function = selected_function_ ? snapshot_->functions[*selected_function_].name : QStringLiteral("Endereço");
    function_heading_->setText(sample + QStringLiteral("  ›  ") + function + QStringLiteral("  ›  ") + hex(address.value));
    if (const auto offset = reworkbench::file_offset(snapshot_->metadata, address)) hex_->show_file(snapshot_->metadata.source_path, *offset);
}
void MainWindow::show_command_palette() { show_quick_picker(true); }
void MainWindow::show_quick_open() { show_quick_picker(false); }
void MainWindow::show_quick_picker(bool commands) {
    QVector<QuickPickerEntry> entries;
    QVector<QPointer<QAction>> registry;
    const auto picker_snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_);
    if (commands) {
        QMap<QAction*, QString> categories;
        if (menus_) for (auto* menu_action : menus_->actions()) {
            if (const auto* menu = menu_action->menu()) {
                const auto category = QString(menu->title()).remove('&');
                for (auto* action : menu->actions()) categories.insert(action, category);
            }
        }
        QSet<QAction*> seen;
        const auto add_command = [&entries, &registry, &categories, &seen](QAction* action, const QString& category = {}) {
            if (!action || action->isSeparator() || seen.contains(action)) return;
            const auto label = QString(action->text()).remove('&').trimmed();
            if (label.isEmpty()) return;
            seen.insert(action);
            const int index = registry.size();
            registry.append(action);
            const auto icon = action->icon();
            entries.append({icon, label, categories.value(action, category),
                action->shortcut().toString(QKeySequence::NativeText), index, action->isEnabled()});
        };
        for (auto* action : actions_) add_command(action);
        for (auto* dock : docks_) add_command(dock->toggleViewAction(), QStringLiteral("Exibir painel"));
    } else if (picker_snapshot) {
        entries.reserve(picker_snapshot->functions.size());
        const auto function_icon = workbench_icon("symbol-method", theme_color("symbolIcon.functionForeground"));
        for (const auto& function : picker_snapshot->functions) {
            entries.append({function_icon, function.name, function.address.text(), {}, function.address.text()});
        }
    }
    const auto chosen = choose_quick_picker(this, commands, entries,
        picker_snapshot ? QStringLiteral("Nenhuma função encontrada")
                        : QStringLiteral("Abra um projeto e importe um binário para navegar."));
    if (!chosen || closing_) return;
    if (commands) {
        const int index = chosen->toInt();
        if (index >= 0 && index < registry.size()) {
            const auto action = registry[index];
            if (action && action->isEnabled()) action->trigger();
        }
    } else if (!busy_ && picker_snapshot == snapshot_) {
        navigate(parse_address(chosen->toString()));
    }
}
}
