#pragma once

#include <QIcon>
#include <QString>
#include <QVariant>
#include <QVector>
#include <optional>

class QWidget;

namespace reworkbench::ui {

struct QuickPickerEntry {
    QIcon icon;
    QString label;
    QString description;
    QString shortcut;
    QVariant payload;
    bool enabled = true;
};

// Entries are presentation values. Callers resolve the selected payload only
// after the popup closes, keeping command execution out of its event loop.
std::optional<QVariant> choose_quick_picker(QWidget* parent, bool commands,
    const QVector<QuickPickerEntry>& entries, const QString& empty_message = {});

} // namespace reworkbench::ui
