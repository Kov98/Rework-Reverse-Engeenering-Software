#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace reworkbench::ui {

// Codicons are bundled with the application; no installed icon font is required.
// An invalid tint uses the workbench's icon.foreground token.
QIcon workbench_icon(const QString& name, const QColor& tint = {}, int size = 16);

// Files use the embedded Seti family; native binaries/libraries use Codicons.
// Selection is lexical and never implies a source file exists in a snapshot.
QIcon file_icon(const QString& filename, int size = 16);
QIcon folder_icon(bool project = false, int size = 16);

// Symbol kinds are independent of file extensions: function, class, struct,
// namespace, variable, import, export, memory, reference, instruction, string,
// graph, breakpoint, or symbol.
QIcon symbol_icon(const QString& kind, int size = 16);

} // namespace reworkbench::ui
