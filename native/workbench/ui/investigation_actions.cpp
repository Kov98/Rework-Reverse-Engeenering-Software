#include "main_window.hpp"
#include "workspace_theme.hpp"
#include "debugger_dialog.hpp"
#include "reworkbench/decompilation_cache.hpp"
#include "reworkbench/enrichment.hpp"
#include "reworkbench/investigation.hpp"
#include "reworkbench/plugin.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QVBoxLayout>

namespace reworkbench::ui {
namespace {
QJsonDocument json_input(const QString& text, bool object) {
    if (text.size() > 1024 * 1024) throw std::runtime_error("Configuração excede 1 MiB.");
    QJsonParseError error; const auto document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || (object ? !document.isObject() : !document.isArray())) throw std::runtime_error("Configuração JSON inválida.");
    return document;
}
}
void MainWindow::show_tool_result(const QString& title, const QJsonObject& result) {
    auto* dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(title); dialog->resize(1000, 720);
    auto* layout = new QVBoxLayout(dialog); auto* text = new QPlainTextEdit;
    text->setReadOnly(true); text->setFont(default_code_font()); text->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto output = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
    if (output.size() > 2 * 1024 * 1024) { output.truncate(2 * 1024 * 1024); output += QStringLiteral("\n… Visualização limitada a 2 MiB. Use a CLI para o resultado completo."); }
    text->setPlainText(output); layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close); layout->addWidget(buttons);
    dialog->show();
}
void MainWindow::export_pseudocode() {
    if (!project_ || !snapshot_ || !selected_function_ || busy_) return;
    const auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar pseudocódigo"), QStringLiteral("funcao-%1.c.txt").arg(hex(snapshot_->functions[*selected_function_].address.value)), QStringLiteral("Pseudocódigo (*.txt)"));
    if (destination.isEmpty()) return;
    const auto project = project_; const auto id = analysis_id_; const auto snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_); const auto index = *selected_function_;
    start_task(QStringLiteral("Exportando pseudocódigo próprio…"), [project, id, snapshot, index, destination](std::stop_token stop, ProgressCallback) {
        const auto result = cached_decompile(project, *snapshot, index, stop);
        const auto header = QStringLiteral("// REWorkbench pseudocode: partial machine-state model, not original source.\n// analysis=%1 function=%2 source_sha256=%3\n\n").arg(id, snapshot->functions[index].address.text(), snapshot->metadata.sha256);
        project->export_artifact(id, (header + result.pseudocode).toUtf8(), destination, stop);
        TaskResult task; task.message = QStringLiteral("Pseudocódigo exportado: ") + destination; return task;
    });
}
void MainWindow::compare_binary() {
    QString left = snapshot_ ? snapshot_->metadata.source_path : byte_path_;
    if (left.isEmpty()) left = QFileDialog::getOpenFileName(this, QStringLiteral("Primeiro binário"));
    if (left.isEmpty()) return;
    const auto right = QFileDialog::getOpenFileName(this, QStringLiteral("Comparar com outro binário")); if (right.isEmpty()) return;
    start_task(QStringLiteral("Comparando bytes e hashes…"), [left, right](std::stop_token stop, ProgressCallback) {
        TaskResult result; result.data = compare_binaries(left, right, stop); return result;
    }, [this](const TaskResult& result) { show_tool_result(QStringLiteral("Comparação binária"), result.data); });
}
void MainWindow::export_patch() {
    if (!snapshot_ || !project_ || busy_) return;
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, QStringLiteral("Patch em uma cópia"), QStringLiteral("Liste offsets de arquivo, bytes esperados e bytes novos, em hexadecimal.\nA cópia original é preservada; os tamanhos de cada substituição devem coincidir."), QStringLiteral("[\n  {\"offset\":\"0x0\", \"expected\":\"4d5a\", \"replacement\":\"4d5a\"}\n]"), &accepted);
    if (!accepted) return;
    QJsonArray patches; try { patches = json_input(text, false).array(); } catch (const std::exception& exception) { error(QString::fromUtf8(exception.what())); return; }
    const auto destination = QFileDialog::getSaveFileName(this, QStringLiteral("Salvar cópia com patch"), project_->root() + QStringLiteral("/exports/patched.bin")); if (destination.isEmpty()) return;
    const auto project = project_; const auto id = analysis_id_; const auto source = snapshot_->metadata.source_path;
    start_task(QStringLiteral("Verificando e exportando a cópia…"), [project, id, source, destination, patches](std::stop_token stop, ProgressCallback) {
        (void)project->analysis_sample(id, stop);
        TaskResult result; result.data = export_patched_copy(source, destination, patches, project->root(), project->protected_sources(stop), stop); result.message = QStringLiteral("Cópia com patch exportada."); return result;
    }, [this](const TaskResult& result) { show_tool_result(QStringLiteral("Patch exportado"), result.data); });
}
void MainWindow::emulate_selection() {
    if (!snapshot_ || !selected_function_ || busy_) return;
    bool accepted = false;
    const auto text = QInputDialog::getMultiLineText(this, QStringLiteral("Emular função"), QStringLiteral("Defina os valores iniciais conhecidos. Memória e registradores omitidos permanecem desconhecidos.\nA emulação interrompe em instruções ou efeitos não suportados."), QStringLiteral("{\n  \"registers\": {},\n  \"memory\": [],\n  \"max_steps\": 10000\n}"), &accepted);
    if (!accepted) return;
    QJsonObject initial; try { initial = json_input(text, true).object(); } catch (const std::exception& exception) { error(QString::fromUtf8(exception.what())); return; }
    const auto snapshot = std::shared_ptr<const AnalysisSnapshot>(snapshot_); const auto index = *selected_function_;
    start_task(QStringLiteral("Emulando o modelo da função…"), [snapshot, index, initial](std::stop_token stop, ProgressCallback) {
        TaskResult result; result.data = emulate_function(*snapshot, index, initial, stop); return result;
    }, [this](const TaskResult& result) { show_tool_result(QStringLiteral("Emulação"), result.data); });
}
void MainWindow::import_symbols() {
    if (!project_ || !snapshot_ || busy_) return;
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Importar mapa de símbolos local"), {}, QStringLiteral("Mapas JSON ou texto (*.json *.map *.txt);;Todos os arquivos (*)")); if (path.isEmpty()) return;
    if (QFileInfo(path).size() > 16 * 1024 * 1024) { error(QStringLiteral("Mapa de símbolos excede 16 MiB.")); return; }
    const auto project = project_; const auto previous = analysis_id_; const auto options = snapshot_->options; const auto drafts = drafts_;
    start_task(QStringLiteral("Importando símbolos e criando nova análise…"), [project, previous, options, drafts, path](std::stop_token stop, ProgressCallback progress) {
        for (const auto& note : drafts) { check_cancelled(stop); project->set_note(previous, note); }
        const auto sample = project->analysis_sample(previous, stop); const auto map = project->import_sample(path, stop);
        auto updated = options; updated.symbol_map_path = project->root() + QLatin1Char('/') + map.relative_path; updated.symbol_map_sha256 = map.sha256;
        auto snapshot = std::make_shared<AnalysisSnapshot>(analyze_binary(project->root() + QLatin1Char('/') + sample.relative_path, updated, {}, stop, progress));
        if (!snapshot->metadata.sha256.isEmpty() && snapshot->metadata.sha256 != sample.sha256) throw std::runtime_error("A cópia mudou antes da análise; resultado recusado.");
        snapshot->metadata.sha256 = sample.sha256; snapshot->metadata.original_name = sample.original_name;
        TaskResult result; result.project = project; result.analysis_id = project->save_new_analysis(sample, *snapshot, {});
        project->inherit_annotations(previous, result.analysis_id);
        result.snapshot = std::make_shared<AnalysisSnapshot>(project->load_analysis(result.analysis_id)); result.summaries = project->analyses(); result.notes = project->notes(result.analysis_id);
        result.message = QStringLiteral("Nova análise com símbolos importados; análise anterior preservada."); return result;
    }, [this](const TaskResult& result) {
        drafts_.clear(); analyses_->blockSignals(true); analyses_->clear();
        for (const auto& summary : result.summaries) analyses_->addItem(summary.name + QStringLiteral(" · ") + summary.id.left(8), summary.id);
        analyses_->blockSignals(false); install_snapshot(result);
    });
}
void MainWindow::show_enrichment() {
    if (snapshot_) show_tool_result(QStringLiteral("Símbolos, relocações e fontes"), snapshot_->enrichment);
}
void MainWindow::show_extensions() {
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Pasta de extensões nativas")); if (directory.isEmpty()) return;
    start_task(QStringLiteral("Lendo metadados das extensões…"), [directory](std::stop_token stop, ProgressCallback) {
        TaskResult result; result.data = {{"extensions", discover_extensions(directory, stop)}}; return result;
    }, [this](const TaskResult& result) { show_tool_result(QStringLiteral("Extensões · metadados"), result.data); });
}
void MainWindow::show_debugger() { auto* dialog = new DebuggerDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->show(); }
}
