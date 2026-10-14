#pragma once
#include "reworkbench/analysis.hpp"
#include "reworkbench/project.hpp"
#include "record_model.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <functional>
#include <memory>

namespace reworkbench::ui {
class HexView;
class GraphView;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void open_project_path(const QString& path, const QString& sample = {});
    void examine_path(const QString& path);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    struct TaskResult {
        std::shared_ptr<Project> project;
        std::shared_ptr<AnalysisSnapshot> snapshot;
        QString analysis_id;
        QVector<AnalysisSummary> summaries;
        QVector<Note> notes;
        Probe probe;
        QString message;
        QString error;
        bool cancelled = false;
    };
    using Work = std::function<TaskResult(std::stop_token, ProgressCallback)>;
    using Completion = std::function<void(const TaskResult&)>;

    void build_actions();
    void build_workspace();
    void build_menus();
    QDockWidget* add_dock(const QString& key, const QString& title, QWidget* widget, Qt::DockWidgetArea area);
    void start_task(const QString& label, Work work, Completion completion = {});
    void finish_task();
    void update_actions();
    void set_progress(const Progress& progress);
    void log(const QString& message);
    void error(const QString& message);
    void new_project();
    void open_project();
    void request_project(const QString& path, bool create, const QString& sample = {});
    void import_sample(const QString& path = {});
    void analyze_sample(const QString& path, const OpenOptions& options);
    void activate_analysis(const QString& id, std::optional<Address> navigation = {});
    void install_project(const TaskResult& result);
    void install_snapshot(const TaskResult& result);
    void set_note_address(std::optional<Address> address);
    void record_draft();
    void save_project();
    void export_report();
    void export_ir();
    void rename_function();
    void navigate(Address address, bool record_history = true);
    void show_offset(quint64 offset);
    void history_step(int direction);
    void jump();
    void update_bookmarks();
    void update_audit();
    void about();

    QSettings settings_;
    std::shared_ptr<Project> project_;
    std::shared_ptr<AnalysisSnapshot> snapshot_;
    QString analysis_id_;
    QString byte_path_;
    std::optional<qsizetype> selected_function_;
    std::optional<Address> note_address_;
    QMap<QString, Note> notes_;
    QMap<QString, Note> drafts_;
    bool loading_note_ = false;
    bool busy_ = false;
    bool closing_ = false;
    bool allow_close_ = false;
    quint64 selection_token_ = 0;
    quint64 task_token_ = 0;
    quint64 audit_token_ = 0;
    std::stop_source stop_;
    std::stop_source selection_stop_;
    std::stop_source audit_stop_;
    QFutureWatcher<TaskResult>* task_;
    Completion completion_;
    QVector<std::pair<QString, Address>> history_;
    qsizetype history_index_ = -1;
    QMap<QString, QAction*> actions_;
    QMap<QString, QDockWidget*> docks_;
    QMap<Dataset, Explorer*> explorers_;
    QComboBox* analyses_;
    QStackedWidget* central_;
    QLabel* function_heading_;
    QLabel* ir_status_;
    QPlainTextEdit* ir_;
    Explorer* assembly_;
    Explorer* references_;
    GraphView* graph_;
    HexView* hex_;
    QPlainTextEdit* metadata_;
    QPlainTextEdit* console_;
    QLabel* note_heading_;
    QPlainTextEdit* note_editor_;
    QCheckBox* bookmark_;
    QPushButton* save_note_;
    QListWidget* bookmarks_;
    QListWidget* audit_;
    QLabel* status_;
    QProgressBar* progress_;
};
} // namespace reworkbench::ui
