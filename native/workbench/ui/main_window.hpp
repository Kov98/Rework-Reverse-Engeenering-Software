#pragma once
#include "reworkbench/analysis.hpp"
#include "reworkbench/project.hpp"
#include "record_model.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFutureWatcher>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QMap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QSplitter>
#include <QMouseEvent>
#include <functional>
#include <memory>

namespace reworkbench::ui {
class HexView;
class GraphView;
class AssistantPanel;
class BinaryExplorer;
class WindowChrome;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void open_project_path(const QString& path, const QString& sample = {});
    void examine_path(const QString& path);
protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
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
        QJsonObject data;
        bool cancelled = false;
    };
    using Work = std::function<TaskResult(std::stop_token, ProgressCallback)>;
    using Completion = std::function<void(const TaskResult&)>;

    void build_actions();
    void build_workspace();
    void build_menus();
    void build_workspace_shell();
    void restore_workspace();
    void save_workspace();
    void reset_workspace();
    void apply_code_font();
    void show_command_palette();
    void show_quick_open();
    void show_quick_picker(bool commands);
    void show_bottom_tab(QWidget* tab);
    void select_instruction(Address address, QPlainTextEdit* origin = nullptr);
    void set_view_visible(const QString& key, bool visible);
    void open_function_tab(Address address, const QString& name);
    void close_function_tab(int index);
    QString assistant_context() const;
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
    void install_snapshot(const TaskResult& result, std::optional<Address> navigation = {});
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
    void export_pseudocode();
    void compare_binary();
    void export_patch();
    void emulate_selection();
    void import_symbols();
    void show_enrichment();
    void show_extensions();
    void show_debugger();
    void show_tool_result(const QString& title, const QJsonObject& result);

    QSettings settings_;
    std::shared_ptr<Project> project_;
    std::shared_ptr<AnalysisSnapshot> snapshot_;
    std::weak_ptr<const AnalysisSnapshot> document_snapshot_;
    QString analysis_id_;
    QString byte_path_;
    std::optional<qsizetype> selected_function_;
    std::optional<Address> selected_address_;
    bool synchronizing_selection_ = false;
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
    QPlainTextEdit* pseudocode_;
    QPlainTextEdit* ssa_;
    QLabel* decompiler_status_;
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
    QTabWidget* representation_ = nullptr;
    QTabBar* documents_ = nullptr;
    QSplitter* editor_split_ = nullptr;
    AssistantPanel* assistant_ = nullptr;
    BinaryExplorer* binary_explorer_ = nullptr;
    WindowChrome* chrome_ = nullptr;
    QMenuBar* menus_ = nullptr;
    QTabWidget* bottom_panel_ = nullptr;
    QWidget* listing_panel_ = nullptr;
    QLabel* sample_status_ = nullptr;
    QByteArray default_layout_;
    QFont code_font_;
    QMap<QString, std::pair<QWidget*, QString>> views_;
};
} // namespace reworkbench::ui
