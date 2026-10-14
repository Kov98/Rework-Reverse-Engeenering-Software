#pragma once
#include "reworkbench/debugger.hpp"
#include <QDialog>
#include <QThread>
#include <QTimer>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <functional>

namespace reworkbench::ui {
class DebuggerDialog final : public QDialog {
public:
    explicit DebuggerDialog(QWidget* parent = nullptr);
    ~DebuggerDialog() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    struct State { std::unique_ptr<NativeDebugger> debugger; };
    void submit(std::function<QJsonObject(NativeDebugger&)> work);
    void refresh_buttons();
    void poll();
    std::shared_ptr<State> state_;
    QThread worker_;
    QObject* context_;
    QTimer timer_;
    bool pending_ = false;
    bool active_ = false;
    bool event_pending_ = false;
    bool awaiting_ = false;
    bool closing_ = false;
    bool allow_close_ = false;
    quint32 thread_id_ = 0;
    QLineEdit* executable_;
    QLineEdit* arguments_;
    QLineEdit* pid_;
    QLineEdit* address_;
    QPlainTextEdit* output_;
    QPushButton* launch_;
    QPushButton* attach_;
    QPushButton* resume_;
    QPushButton* step_;
    QPushButton* registers_;
    QPushButton* breakpoint_;
    QPushButton* read_;
    QPushButton* detach_;
};
}
