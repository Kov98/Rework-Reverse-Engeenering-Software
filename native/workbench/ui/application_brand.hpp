#pragma once

#include <QIcon>

namespace reworkbench::ui {

// The official logo is embedded in the static UI library and is available
// whenever a window is constructed, including outside the GUI entry point.
QIcon application_icon();

} // namespace reworkbench::ui
