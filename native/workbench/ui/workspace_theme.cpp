#include "workspace_theme.hpp"
#include "window_chrome.hpp"
#include <QApplication>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QStyleFactory>
#include <QStyleOption>
#include <QFontInfo>
#include <QDebug>
#include <QMenu>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace reworkbench::ui {
namespace {
// Rework Dark surfaces and identity, with inherited Dark+ syntax and RE mappings.
// See docs/theme.md for sources, mappings and typography.
const QHash<QString, QString>& tokens() {
    static const QHash<QString, QString> values{
        {"brand.primary", "#6018F8"}, {"brand.focus", "#A074FB"},
        {"foreground", "#D4D4D8"}, {"descriptionForeground", "#ABABB4"},
        {"disabledForeground", "#777780"}, {"focusBorder", "#A074FB"},
        {"icon.foreground", "#C9C9D0"}, {"widget.border", "#414149"},
        {"widget.shadow", "#00000066"}, {"errorForeground", "#F85149"},
        {"editor.background", "#232327"}, {"editor.foreground", "#D4D4D8"},
        {"editor.selectionBackground", "#49365F"}, {"editor.inactiveSelectionBackground", "#3D3D45"},
        {"editor.lineHighlightBackground", "#2B2B31"},
        {"editorLineNumber.foreground", "#6E7681"}, {"editorLineNumber.activeForeground", "#CCCCCC"},
        {"editorIndentGuide.background", "#404040"}, {"editorIndentGuide.activeBackground", "#707070"},
        {"editorWidget.background", "#29292F"}, {"editorGroup.border", "#34343C"},
        {"editorGroupHeader.tabsBackground", "#1E1E22"},
        {"sideBar.background", "#1E1E22"}, {"sideBar.foreground", "#D4D4D8"},
        {"sideBar.border", "#34343C"}, {"sideBarSectionHeader.background", "#1E1E22"},
        {"sideBarSectionHeader.foreground", "#D4D4D8"},
        {"activityBar.background", "#1B1B1F"}, {"activityBar.foreground", "#E1E1E6"},
        {"activityBar.inactiveForeground", "#909098"}, {"activityBar.border", "#34343C"},
        {"activityBar.activeBorder", "#A074FB"},
        {"titleBar.activeBackground", "#202024"}, {"titleBar.activeForeground", "#D4D4D8"},
        {"titleBar.border", "#34343C"}, {"titleBar.inactiveForeground", "#ABABB4"},
        {"commandCenter.background", "#2A2A30"}, {"commandCenter.border", "#414149"},
        {"panel.background", "#1E1E22"}, {"panel.border", "#34343C"},
        {"panelTitle.activeBorder", "#A074FB"}, {"panelTitle.activeForeground", "#D4D4D8"},
        {"panelTitle.inactiveForeground", "#ABABB4"},
        {"tab.activeBackground", "#232327"}, {"tab.activeForeground", "#F2F2F4"},
        {"tab.activeBorderTop", "#A074FB"}, {"tab.inactiveBackground", "#1E1E22"},
        {"tab.inactiveForeground", "#ABABB4"}, {"tab.border", "#34343C"}, {"tab.hoverBackground", "#2B2B31"},
        {"input.background", "#2E2E35"}, {"input.foreground", "#D4D4D8"},
        {"input.border", "#414149"}, {"input.placeholderForeground", "#A0A0AA"},
        {"dropdown.background", "#29292F"}, {"dropdown.border", "#414149"},
        {"button.background", "#6018F8"}, {"button.foreground", "#FFFFFF"},
        {"button.hoverBackground", "#762FF9"}, {"button.pressedBackground", "#5013CF"},
        {"button.secondaryBackground", "#303037"},
        {"button.secondaryForeground", "#D4D4D8"}, {"button.secondaryHoverBackground", "#3A3A43"},
        {"toolbar.hoverBackground", "#34343C"}, {"toolbar.pressedBackground", "#414149"},
        {"list.activeSelectionBackground", "#3B344B"}, {"list.activeSelectionForeground", "#F2F2F4"},
        {"list.inactiveSelectionBackground", "#37373F"}, {"list.hoverBackground", "#2E2E35"},
        {"menu.background", "#29292F"}, {"menu.border", "#484850"}, {"menu.selectionBackground", "#3A3A43"},
        {"quickInput.background", "#29292F"}, {"quickInput.foreground", "#D4D4D8"},
        {"statusBar.background", "#1E1E22"}, {"statusBar.foreground", "#C9C9D0"}, {"statusBar.border", "#34343C"},
        {"progressBar.background", "#A074FB"}, {"scrollbarSlider.background", "#79797966"},
        {"scrollbarSlider.hoverBackground", "#646464B3"}, {"scrollbarSlider.activeBackground", "#BFBFBF66"},
        {"textLink.foreground", "#B89BFC"}, {"window.closeHoverBackground", "#C42B1C"},
        {"syntax.keyword", "#569CD6"}, {"syntax.control", "#C586C0"}, {"syntax.type", "#4EC9B0"},
        {"syntax.function", "#DCDCAA"}, {"syntax.variable", "#9CDCFE"}, {"syntax.parameter", "#9CDCFE"},
        {"syntax.string", "#CE9178"}, {"syntax.number", "#B5CEA8"}, {"syntax.comment", "#6A9955"},
        {"syntax.operator", "#D4D4D4"}, {"syntax.register", "#9CDCFE"}, {"syntax.mnemonic", "#CCCCCC"},
        {"syntax.address", "#858585"}, {"syntax.label", "#DCDCAA"}, {"syntax.constant", "#4FC1FF"},
        {"symbolIcon.functionForeground", "#DCDCAA"}, {"symbolIcon.variableForeground", "#75BEFF"},
        {"symbolIcon.stringForeground", "#CE9178"}, {"symbolIcon.namespaceForeground", "#CCCCCC"},
        {"symbolIcon.folderForeground", "#CCCCCC"}, {"symbolIcon.classForeground", "#EE9D28"},
        {"charts.green", "#89D185"}, {"charts.red", "#F14C4C"}, {"charts.blue", "#75BEFF"},
        {"debugIcon.breakpointForeground", "#E51400"},
        {"quickInputList.focusBackground", "#3B344B"}, {"quickInputList.focusForeground", "#F2F2F4"},
        {"keybindingLabel.background", "#2B2B2B"}, {"keybindingLabel.foreground", "#CCCCCC"},
        {"keybindingLabel.border", "#3C3C3C"}, {"keybindingLabel.bottomBorder", "#454545"},
        {"graph.nodeBackground", "#29292F"}, {"graph.selectedBackground", "#3B344B"},
        {"graph.border", "#414149"}, {"graph.selectedBorder", "#A074FB"},
        {"fileIcon.blue", "#519ABA"}, {"fileIcon.purple", "#A074C4"}, {"fileIcon.yellow", "#CBCB41"},
        {"fileIcon.orange", "#E37933"}, {"fileIcon.red", "#CC3E44"}, {"fileIcon.grey", "#6D8086"},
        {"fileIcon.default", "#D4D7D6"}, {"symbolIcon.structForeground", "#B5CEA8"}
    };
    return values;
}
const QHash<QString, int>& metrics() {
    static const QHash<QString, int> values{
        {"radius.control", 5}, {"radius.input", 6}, {"radius.item", 5},
        {"radius.popup", 10}, {"radius.composer", 8}, {"spacing.unit", 4},
        {"typography.uiPixels", 13}, {"typography.captionPixels", 12},
        {"typography.codePixels", 14}, {"density.row", 22}, {"icon.size", 16}
    };
    return values;
}
class WorkbenchStyle final : public QProxyStyle {
public:
    WorkbenchStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    void polish(QWidget* widget) override {
        QProxyStyle::polish(widget);
        if (qobject_cast<QMenu*>(widget) || widget->windowType() == Qt::ToolTip) {
            // QSS radii alone leave an opaque native rectangular popup behind.
            widget->setWindowFlag(Qt::FramelessWindowHint);
            widget->setAttribute(Qt::WA_TranslucentBackground);
            widget->setAutoFillBackground(false);
        }
    }
    QIcon standardIcon(StandardPixmap icon, const QStyleOption* option, const QWidget* widget) const override {
        if (icon == SP_TitleBarCloseButton || icon == SP_DockWidgetCloseButton) return chrome_icon(Glyph::Close);
        return QProxyStyle::standardIcon(icon, option, widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override {
        if (element == PE_IndicatorTabClose) {
            painter->save();
            if (option->state.testFlag(State_MouseOver)) {
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(Qt::NoPen); painter->setBrush(theme_color("toolbar.hoverBackground"));
                painter->drawRoundedRect(option->rect, theme_metric("radius.control"), theme_metric("radius.control"));
            }
            chrome_icon(Glyph::Close).paint(painter, option->rect); painter->restore(); return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};
}
QColor theme_color(const char* token) {
    const auto value = tokens().value(QLatin1String(token), tokens().value("foreground"));
    // VS Code encodes alpha last (RRGGBBAA); QColor's eight-digit form is ARGB.
    if (value.size() == 9) { QColor color(value.left(7)); color.setAlpha(value.right(2).toInt(nullptr, 16)); return color; }
    return QColor(value);
}
QString theme_hex(const char* token) {
    const auto color = theme_color(token);
    if (color.alpha() == 255) return color.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}
QString theme_name() { return QStringLiteral("Rework Dark"); }
int theme_metric(const char* token) { return metrics().value(QLatin1String(token), 0); }
QString themed_stylesheet(QString source) {
    static const QRegularExpression reference(QStringLiteral("@\\{([A-Za-z0-9_.]+)\\}"));
    auto matches = reference.globalMatch(source); QVector<QRegularExpressionMatch> replacements;
    while (matches.hasNext()) replacements << matches.next();
    for (auto match = replacements.crbegin(); match != replacements.crend(); ++match) {
        const auto key = match->captured(1);
        const auto replacement = metrics().contains(key) ? QString::number(metrics().value(key)) + QStringLiteral("px")
                                                       : theme_hex(key.toLatin1().constData());
        source.replace(match->capturedStart(), match->capturedLength(), replacement);
    }
    return source;
}
QFont interface_font() {
    QFont font = QFontDatabase::families().contains("Segoe UI", Qt::CaseInsensitive)
        ? QFont(QStringLiteral("Segoe UI")) : QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(theme_metric("typography.uiPixels") * 72.0 / 96.0);
    font.setWeight(QFont::Normal); font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    font.setHintingPreference(QFont::PreferDefaultHinting);
    return font; // Keep platform text rasterization and native DPI scaling.
}
QFont default_code_font() {
    const auto families = QFontDatabase::families();
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const auto* family : {"Cascadia Code", "Consolas"})
        if (families.contains(QLatin1String(family), Qt::CaseInsensitive)) { font.setFamily(QLatin1String(family)); break; }
    font.setPointSizeF(theme_metric("typography.codePixels") * 72.0 / 96.0);
    font.setStyleHint(QFont::Monospace); font.setFixedPitch(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0); return font;
}
QFont resolve_code_font(QFont requested) {
    if (QFontDatabase::families().contains(requested.family(), Qt::CaseInsensitive)) return requested;
    const auto fallback = default_code_font().family();
    qWarning() << "Code font unavailable:" << requested.family() << "using" << fallback;
    requested.setFamily(fallback); return requested;
}
bool system_reduces_motion() {
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) return !enabled;
#endif
    return !QApplication::isEffectEnabled(Qt::UI_AnimateMenu);
}
void apply_workspace_theme(QApplication& application) {
    application.setStyle(new WorkbenchStyle);
    QPalette palette;
    const std::pair<QPalette::ColorRole, const char*> roles[]{
        {QPalette::Window, "sideBar.background"}, {QPalette::WindowText, "foreground"},
        {QPalette::Base, "editor.background"}, {QPalette::AlternateBase, "editor.background"},
        {QPalette::Text, "editor.foreground"}, {QPalette::Button, "button.secondaryBackground"},
        {QPalette::ButtonText, "button.secondaryForeground"}, {QPalette::Highlight, "editor.selectionBackground"},
        {QPalette::HighlightedText, "list.activeSelectionForeground"}, {QPalette::PlaceholderText, "input.placeholderForeground"},
        {QPalette::ToolTipBase, "editorWidget.background"}, {QPalette::ToolTipText, "foreground"},
        {QPalette::Light, "widget.border"}, {QPalette::Mid, "panel.border"}, {QPalette::Dark, "sideBar.background"}
    };
    for (const auto& [role, token] : roles) palette.setColor(role, theme_color(token));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) palette.setColor(QPalette::Disabled, role, theme_color("disabledForeground"));
    application.setPalette(palette);
    application.setStyleSheet(themed_stylesheet(QStringLiteral(R"(
        QMainWindow, QDialog { background: @{sideBar.background}; color: @{foreground}; }
        QWidget#workspaceChrome { background: @{titleBar.activeBackground}; border-bottom: 1px solid @{titleBar.border}; }
        QLabel#appMark { color: @{focusBorder}; font-size: 18px; font-weight: 600; }
        QMenuBar { background: @{titleBar.activeBackground}; color: @{titleBar.activeForeground}; padding: 0; }
        QMenuBar::item { padding: 5px 7px; background: transparent; border-radius: @{radius.control}; }
        QMenuBar::item:selected { background: @{toolbar.hoverBackground}; }
        QMenu { background: @{menu.background}; color: @{foreground}; border: 1px solid @{menu.border}; border-radius: @{radius.popup}; padding: 6px; }
        QMenu::item { padding: 5px 28px 5px 9px; border-radius: @{radius.item}; }
        QMenu::item:selected { background: @{menu.selectionBackground}; color: @{button.foreground}; }
        QMenu::item:disabled { color: @{disabledForeground}; }
        QMenu::separator { height: 1px; background: @{panel.border}; margin: 4px 2px; }
        QToolButton { color: @{icon.foreground}; border: 1px solid transparent; border-radius: @{radius.control}; padding: 2px; background: transparent; }
        QToolButton:hover { background: @{toolbar.hoverBackground}; }
        QToolButton:pressed { background: @{toolbar.pressedBackground}; }
        QToolButton:focus { border-color: @{focusBorder}; }
        QToolButton:disabled { color: @{disabledForeground}; }
        QFrame#graphZoomControls { background: @{editorWidget.background}; border: 1px solid @{widget.border}; border-radius: @{radius.input}; }
        QFrame#graphZoomControls QToolButton { padding: 3px; border-radius: @{radius.control}; }
        QWidget#workspaceChrome QToolButton { padding: 0; border-radius: @{radius.control}; }
        QWidget#workspaceChrome QToolButton#windowClose, QWidget#workspaceChrome QToolButton#windowMinimize, QWidget#workspaceChrome QToolButton#windowMaximize { border-radius: 0; }
        QToolButton#windowClose:hover { background: @{window.closeHoverBackground}; }
        QPushButton#commandCenter { color: @{foreground}; background: @{commandCenter.background}; border: 1px solid @{commandCenter.border}; border-radius: @{radius.input}; padding: 0 10px; text-align: center; }
        QPushButton#commandCenter:hover { border-color: @{descriptionForeground}; }
        QPushButton#commandCenter:focus { border-color: @{focusBorder}; }
        QMainWindow::separator { width: 3px; height: 3px; background: @{panel.border}; }
        QMainWindow::separator:hover { background: @{focusBorder}; }
        QDockWidget { border: none; color: @{foreground}; }
        QDockWidget::title { background: @{sideBar.background}; padding: 6px; }
        QWidget#dockHeader { background: @{sideBar.background}; border-bottom: 1px solid @{sideBar.border}; }
        QWidget#dockHeader QLabel { color: @{sideBarSectionHeader.foreground}; }
        QWidget#dockHeader QToolButton { padding: 2px; }
        QToolBar { border: none; padding: 0; background: @{activityBar.background}; }
        QToolBar#activityBar { spacing: 0; border-right: 1px solid @{activityBar.border}; }
        QToolBar#activityBar QToolButton { width: 45px; height: 46px; padding: 0; border-radius: 0; border-left: 2px solid transparent; }
        QToolBar#activityBar QToolButton:checked { border-left: 2px solid @{activityBar.activeBorder}; background: @{activityBar.background}; }
        QToolBar#activityBar QToolButton:hover { background: @{toolbar.hoverBackground}; }
        QComboBox { color: @{foreground}; background: @{sideBar.background}; border: none; border-bottom: 1px solid @{sideBar.border}; padding: 5px 9px; min-height: 20px; }
        QComboBox::drop-down { border: none; width: 20px; }
        QComboBox QAbstractItemView { background: @{dropdown.background}; border: 1px solid @{dropdown.border}; selection-background-color: @{list.activeSelectionBackground}; }
        QTreeWidget { color: @{sideBar.foreground}; background: @{sideBar.background}; border: none; outline: none; }
        QTreeWidget::item { height: 23px; padding: 0; border: none; }
        QTreeWidget::item:hover { background: @{list.hoverBackground}; }
        QTreeWidget::item:selected { background: @{list.activeSelectionBackground}; color: @{list.activeSelectionForeground}; }
        QTreeWidget::item:selected:!active { background: @{list.inactiveSelectionBackground}; color: @{foreground}; }
        QLineEdit { color: @{input.foreground}; background: @{input.background}; border: 1px solid @{input.border}; border-radius: @{radius.input}; padding: 4px 6px; selection-background-color: @{editor.selectionBackground}; }
        QLineEdit:hover { border-color: @{descriptionForeground}; }
        QLineEdit:focus { border-color: @{focusBorder}; }
        QLineEdit#binaryTreeFilter { margin: 0 8px; padding: 4px 7px; }
        QPlainTextEdit, QListWidget { color: @{editor.foreground}; background: @{editor.background}; border: none; selection-background-color: @{editor.selectionBackground}; }
        QTableView { color: @{editor.foreground}; background: @{editor.background}; border: none; outline: none; gridline-color: transparent; selection-background-color: @{list.inactiveSelectionBackground}; }
        QTableView::item { padding: 0 7px; border: none; }
        QHeaderView::section { color: @{descriptionForeground}; background: @{editor.background}; padding: 4px 7px; border: none; border-bottom: 1px solid @{panel.border}; font-size: @{typography.captionPixels}; }
        QTabWidget::pane { border: none; background: @{editor.background}; }
        QTabBar { background: @{editorGroupHeader.tabsBackground}; }
        QTabBar::tab { color: @{tab.inactiveForeground}; background: @{tab.inactiveBackground}; padding: 6px 9px; border-top: 1px solid transparent; border-right: 1px solid @{tab.border}; }
        QTabBar::tab:selected { color: @{tab.activeForeground}; background: @{tab.activeBackground}; border-top-color: @{tab.activeBorderTop}; }
        QTabBar::tab:hover { background: @{tab.hoverBackground}; }
        QTabBar#functionTabs::tab { min-width: 92px; padding: 8px 12px; }
        QTabBar::close-button { width: 16px; height: 16px; }
        QTabWidget#bottomPanel { background: @{panel.background}; }
        QTabWidget#bottomPanel QTabBar::tab { padding: 6px 10px; border: none; border-bottom: 1px solid transparent; background: @{panel.background}; }
        QTabWidget#bottomPanel QTabBar::tab:selected { color: @{panelTitle.activeForeground}; border-bottom-color: @{panelTitle.activeBorder}; }
        QLabel#functionHeading { color: @{descriptionForeground}; background: @{editor.background}; padding: 0 12px; font-size: @{typography.captionPixels}; border-bottom: 1px solid @{panel.border}; }
        QLabel#panelTitle { color: @{foreground}; background: @{editorGroupHeader.tabsBackground}; padding: 0 10px; min-height: 32px; max-height: 32px; font-size: @{typography.captionPixels}; border-bottom: 1px solid @{panel.border}; }
        QLabel#decompilerStatus { color: @{descriptionForeground}; font-size: @{typography.captionPixels}; padding: 0 9px; }
        QSplitter::handle { background: @{panel.border}; }
        QSplitter::handle:hover { background: @{focusBorder}; }
        QPushButton { color: @{button.secondaryForeground}; background: @{button.secondaryBackground}; border: 1px solid @{input.border}; border-radius: @{radius.control}; padding: 4px 9px; }
        QPushButton:hover { background: @{button.secondaryHoverBackground}; }
        QPushButton:pressed { background: @{toolbar.pressedBackground}; }
        QPushButton:focus { border-color: @{focusBorder}; }
        QPushButton:disabled { color: @{disabledForeground}; border-color: @{panel.border}; }
        QPushButton#primaryButton { color: @{button.foreground}; background: @{button.background}; border: 1px solid transparent; }
        QPushButton#primaryButton:hover { background: @{button.hoverBackground}; }
        QPushButton#primaryButton:pressed { background: @{button.pressedBackground}; }
        QPushButton#primaryButton:focus { border-color: @{focusBorder}; }
        QProgressBar { max-height: 5px; border: none; background: @{input.background}; }
        QProgressBar::chunk { background: @{progressBar.background}; }
        QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
        QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
        QScrollBar::handle { background: @{scrollbarSlider.background}; min-height: 24px; min-width: 24px; border-radius: @{radius.control}; }
        QScrollBar::handle:hover { background: @{scrollbarSlider.hoverBackground}; }
        QScrollBar::handle:pressed { background: @{scrollbarSlider.activeBackground}; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
        QStatusBar { color: @{statusBar.foreground}; background: @{statusBar.background}; border-top: 1px solid @{statusBar.border}; font-size: @{typography.captionPixels}; }
        QStatusBar::item { border: none; }
        QToolTip { color: @{foreground}; background: @{editorWidget.background}; border: 1px solid @{widget.border}; border-radius: @{radius.input}; padding: 6px 8px; }
        QDialog#quickOpen, QDialog#commandPalette { background: transparent; border: none; }
        QLineEdit#quickPickerQuery { padding: 6px; border-color: @{focusBorder}; }
        QListWidget#quickPickerResults { background: @{quickInput.background}; outline: none; }
        QListWidget#quickPickerResults::item { padding: 6px 9px; }
        QListWidget#quickPickerResults::item:selected { background: @{list.activeSelectionBackground}; }
        QLabel#pickerHint { color: @{descriptionForeground}; font-size: @{typography.captionPixels}; padding: 4px; }
        QLabel#overline { color: @{textLink.foreground}; font-size: @{typography.captionPixels}; }
        QLabel#welcomeTitle { color: @{tab.activeForeground}; font-size: 30px; font-weight: 600; padding: 12px 0; }
        QLabel#muted { color: @{descriptionForeground}; }
    )")));
    // Installing the first stylesheet wraps QStyle and reloads Windows class
    // fonts (menus/item views). Set the shared font afterward, otherwise those
    // controls can silently return to the platform's 9pt/12px default.
    application.setFont(interface_font());
}
}
