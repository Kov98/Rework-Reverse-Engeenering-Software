#pragma once
#include "reworkbench/types.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>

namespace reworkbench::ui {
class ImportDialog final : public QDialog {
public:
    ImportDialog(const QString& path, const Probe& probe, QWidget* parent = nullptr);
    [[nodiscard]] OpenOptions options() const;
    void accept() override;
private:
    QCheckBox* raw_;
    QComboBox* architecture_;
    QComboBox* endian_;
    QComboBox* slices_;
    QLineEdit* base_;
    QPlainTextEdit* entries_;
    Probe probe_;
};
[[nodiscard]] Address parse_address(QString text);
} // namespace reworkbench::ui
