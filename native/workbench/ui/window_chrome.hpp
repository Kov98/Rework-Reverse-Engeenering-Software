#pragma once
#include <QWidget>
#include <QIcon>
#include <QColor>
class QAction;
class QMenuBar;
class QPushButton;
class QMainWindow;
namespace reworkbench::ui {
enum class Glyph { Close, Minimize, Maximize, Restore, Search, Back, Forward, Sidebar, Panel, Assistant, Split, More };
QIcon chrome_icon(Glyph glyph, const QColor& color = {});
class WindowChrome final : public QWidget {
public:
    WindowChrome(QMainWindow* window, QMenuBar* menus, QAction* back, QAction* forward,
                 QAction* quick_open, QAction* sidebar, QAction* panel, QAction* assistant);
    void set_workspace(const QString& title);
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
private:
    QMainWindow* window_;
    QPushButton* command_;
};
}
