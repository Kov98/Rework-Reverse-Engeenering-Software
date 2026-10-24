#pragma once
#include <QColor>
#include <QFont>
#include <QString>
class QApplication;
namespace reworkbench::ui {
QColor theme_color(const char* token);
QString theme_hex(const char* token);
QString themed_stylesheet(QString source);
QString theme_name();
int theme_metric(const char* token);
QFont interface_font();
QFont default_code_font();
QFont resolve_code_font(QFont requested);
bool system_reduces_motion();
void apply_workspace_theme(QApplication& application);
}
