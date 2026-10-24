#include "main_window.hpp"
#include "application_brand.hpp"
#include "workspace_theme.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QMessageBox>
#include <QPalette>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("REWorkbench"));
    QCoreApplication::setApplicationName(QStringLiteral("REWorkbench Native"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Rework"));
    QApplication::setWindowIcon(reworkbench::ui::application_icon());
    reworkbench::ui::apply_workspace_theme(application);
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
