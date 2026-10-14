#pragma once
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QWidget>

namespace reworkbench::ui {
class HexView final : public QWidget {
public:
    explicit HexView(QWidget* parent = nullptr);
    void show_file(QString path, quint64 offset = 0);
    void clear();
private:
    QString path_;
    quint64 offset_ = 0;
    quint64 token_ = 0;
    QLineEdit* position_;
    QLabel* detail_;
    QPlainTextEdit* text_;
};
} // namespace reworkbench::ui
