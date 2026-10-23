#pragma once
#include <QWidget>
#include <functional>
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class QToolButton;
namespace reworkbench::ui {
class AssistantPanel final : public QWidget {
public:
    explicit AssistantPanel(std::function<QString()> capture_context, QWidget* parent = nullptr);
    void set_selection(const QString& identity, const QString& label);
private:
    void invalidate_request();
    QString identity_, selection_label_, captured_context_;
    QPlainTextEdit* question_;
    QPlainTextEdit* context_;
    QPlainTextEdit* prepared_;
    QPushButton* capture_;
    QPushButton* prepare_;
    QPushButton* copy_;
    QLabel* selection_;
    QLabel* status_;
    QWidget* empty_state_;
    QTabWidget* previews_;
    QToolButton* preview_toggle_;
};
}
