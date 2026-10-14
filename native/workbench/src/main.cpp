#include "main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>

namespace {
void apply_theme(QApplication& application) {
    application.setStyle(QStringLiteral("Fusion"));
    application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#181e28")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#dce4ef")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#111722")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#192230")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#dce4ef")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#253244")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e4edf7")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#286d75")));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#253244")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e4edf7")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#748195")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#748195")));
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(R"(
        QMainWindow::separator { background: #2b394d; width: 3px; height: 3px; }
        QDockWidget::title { background: #202b3b; padding: 7px; font-weight: 600; }
        QToolBar { padding: 6px; spacing: 5px; border: 0; border-bottom: 1px solid #304055; }
        QToolButton { padding: 7px; }
        QHeaderView::section { background: #243145; padding: 6px; border: 0; }
        QTableView { border: 0; gridline-color: #28364a; }
        QLineEdit, QComboBox { padding: 5px; border: 1px solid #35465e; border-radius: 3px; }
        QPushButton { padding: 7px 12px; border-radius: 4px; border: 1px solid #3a506a; background: #26384d; }
        QPushButton:hover { background: #33516a; border-color: #6babb5; }
        QPushButton:disabled { color: #748195; }
        QTabBar::tab { padding: 7px 12px; }
        QTabBar::tab:selected { color: #8bd5ca; }
        QProgressBar { max-height: 10px; border: 0; background: #263449; }
        QProgressBar::chunk { background: #73c8bd; }
        QLabel#overline { color: #8bd5ca; font-weight: 700; }
        QLabel#welcomeTitle { font-size: 34px; font-weight: 700; padding: 12px 0; }
        QLabel#muted { color: #91a2b8; padding-top: 18px; }
        QLabel#codeHeading { background: #243145; color: #eaf2fc; font-weight: 600; padding: 10px; }
        QLabel#panelTitle { color: #8bd5ca; font-size: 10px; font-weight: 700; padding: 5px; }
        QStatusBar { border-top: 1px solid #304055; }
    )"));
}
} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("REWorkbench"));
    QCoreApplication::setApplicationName(QStringLiteral("REWorkbench Native"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    apply_theme(application);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Bancada independente de engenharia reversa em C++"));
    parser.addHelpOption(); parser.addVersionOption();
    const QCommandLineOption project(QStringLiteral("project"), QStringLiteral("Abrir pasta de projeto nativo"), QStringLiteral("directory"));
    const QCommandLineOption sample(QStringLiteral("sample"), QStringLiteral("Importar amostra no projeto ou examinar seus bytes"), QStringLiteral("file"));
    parser.addOption(project); parser.addOption(sample);
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Pasta de projeto nativo existente"), QStringLiteral("[project]"));
    parser.process(application);
    if (parser.positionalArguments().size() > 1 || (parser.isSet(project) && !parser.positionalArguments().isEmpty())) parser.showHelp(1);
    try {
        reworkbench::ui::MainWindow window;
        window.show();
        const auto project_path = parser.isSet(project) ? parser.value(project) : parser.positionalArguments().value(0);
        const auto sample_path = parser.value(sample);
        if (!project_path.isEmpty() || !sample_path.isEmpty())
            QTimer::singleShot(0, &window, [&window, project_path, sample_path] {
                if (!project_path.isEmpty()) window.open_project_path(project_path, sample_path);
                else window.examine_path(sample_path);
            });
        return application.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("Não foi possível iniciar"), QString::fromUtf8(error.what()));
        return 1;
    }
}
