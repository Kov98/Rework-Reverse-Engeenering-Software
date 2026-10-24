#include "window_chrome.hpp"
#include "application_brand.hpp"
#include "workbench_icons.hpp"
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QWindow>

namespace reworkbench::ui {
namespace {
class ApplicationMark final : public QLabel {
public:
    explicit ApplicationMark(QWidget* parent = nullptr) : QLabel(parent), icon_(application_icon()) {}
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QSize size(20, 20);
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        icon_.paint(&painter, target, Qt::AlignCenter);
    }
private:
    QIcon icon_;
};
}
QIcon chrome_icon(Glyph glyph, const QColor& color) {
    switch (glyph) {
    case Glyph::Close: return workbench_icon(QStringLiteral("close"), color);
    case Glyph::Minimize: return workbench_icon(QStringLiteral("chrome-minimize"), color);
    case Glyph::Maximize: return workbench_icon(QStringLiteral("chrome-maximize"), color);
    case Glyph::Restore: return workbench_icon(QStringLiteral("chrome-restore"), color);
    case Glyph::Search: return workbench_icon(QStringLiteral("search"), color);
    case Glyph::Back: return workbench_icon(QStringLiteral("arrow-left"), color);
    case Glyph::Forward: return workbench_icon(QStringLiteral("arrow-right"), color);
    case Glyph::Sidebar: return workbench_icon(QStringLiteral("layout-sidebar-left"), color);
    case Glyph::Panel: return workbench_icon(QStringLiteral("layout-panel"), color);
    case Glyph::Assistant: return workbench_icon(QStringLiteral("layout-sidebar-right"), color);
    case Glyph::Split: return workbench_icon(QStringLiteral("split-horizontal"), color);
    case Glyph::More: return workbench_icon(QStringLiteral("ellipsis"), color);
    }
    return {};
}
WindowChrome::WindowChrome(QMainWindow* window, QMenuBar* menus, QAction* back, QAction* forward,
                           QAction* quick_open, QAction* sidebar, QAction* panel, QAction* assistant)
    : QWidget(window), window_(window) {
    setObjectName("workspaceChrome"); setFixedHeight(34); setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(9, 0, 0, 0); layout->setSpacing(3);
    auto* mark = new ApplicationMark; mark->setObjectName("appMark"); mark->setFixedSize(22, 24);
    mark->setAlignment(Qt::AlignCenter);
    mark->setToolTip(QStringLiteral("Rework"));
    mark->setAccessibleName(QStringLiteral("Rework"));
    layout->addWidget(mark);
    menus->setNativeMenuBar(false); menus->setObjectName("workspaceMenus"); layout->addWidget(menus);
    layout->addStretch(1);
    const auto tool = [this, layout](QAction* action, Glyph glyph) {
        action->setIcon(chrome_icon(glyph));
        auto* button = new QToolButton(this); button->setDefaultAction(action);
        button->setObjectName("chrome_" + action->objectName());
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFixedSize(28, 28); button->setAutoRaise(true); layout->addWidget(button); return button;
    };
    tool(back, Glyph::Back); tool(forward, Glyph::Forward);
    command_ = new QPushButton(chrome_icon(Glyph::Search), QStringLiteral("Buscar funções ou endereços"));
    command_->setObjectName("commandCenter"); command_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    command_->setMinimumWidth(180); command_->setMaximumWidth(460); command_->setFixedHeight(24);
    command_->setToolTip(QStringLiteral("Abrir função · Ctrl+P\nComandos · Ctrl+Shift+P"));
    connect(command_, &QPushButton::clicked, quick_open, &QAction::trigger); layout->addWidget(command_, 4);
    layout->addStretch(1);
    tool(sidebar, Glyph::Sidebar); tool(panel, Glyph::Panel); tool(assistant, Glyph::Assistant);
    const auto control = [this, layout](Glyph glyph, const QString& name, auto callback) {
        auto* button = new QToolButton(this); button->setIcon(chrome_icon(glyph)); button->setFixedSize(42, 34);
        button->setObjectName(name); button->setToolTip(name == "windowClose" ? QStringLiteral("Fechar") : name == "windowMinimize" ? QStringLiteral("Minimizar") : QStringLiteral("Maximizar / restaurar"));
        connect(button, &QToolButton::clicked, this, callback); layout->addWidget(button);
    };
    control(Glyph::Minimize, "windowMinimize", [window] { window->showMinimized(); });
    control(Glyph::Maximize, "windowMaximize", [window] { window->isMaximized() ? window->showNormal() : window->showMaximized(); });
    control(Glyph::Close, "windowClose", [window] { window->close(); });
}
void WindowChrome::set_workspace(const QString& title) {
    command_->setText(title.isEmpty() ? QStringLiteral("Buscar funções ou endereços") : title.left(52));
}
void WindowChrome::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window_->windowHandle()) {
        window_->windowHandle()->startSystemMove(); event->accept(); return;
    }
    QWidget::mousePressEvent(event);
}
void WindowChrome::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) { window_->isMaximized() ? window_->showNormal() : window_->showMaximized(); event->accept(); return; }
    QWidget::mouseDoubleClickEvent(event);
}
}
