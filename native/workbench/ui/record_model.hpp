#pragma once
#include "reworkbench/types.hpp"

#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QTableView>
#include <QTimer>
#include <QWidget>
#include <memory>

namespace reworkbench::ui {

enum class Dataset { Functions, Symbols, Strings, Imports, Regions, References, Assembly };
struct Record {
    QStringList cells;
    std::optional<Address> address;
    std::optional<Address> other_address;
    std::optional<quint64> file_offset;
    qsizetype function_index = -1;
    bool assembly = false;
    QStringList operand_registers{};
    FlowKind flow = FlowKind::Normal;
};
struct Page {
    QVector<Record> rows;
    quint64 offset = 0;
    quint64 total = 0;
    QString error;
};

class RecordModel final : public QAbstractTableModel {
public:
    explicit RecordModel(QStringList headers, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void set_page(Page page);
    [[nodiscard]] const Page& page() const { return page_; }
    [[nodiscard]] const Record* record(int row) const;
private:
    QStringList headers_;
    Page page_;
};

class Explorer final : public QWidget {
public:
    Explorer(Dataset dataset, QStringList headers, QWidget* parent = nullptr);
    ~Explorer() override;
    void set_snapshot(std::shared_ptr<const AnalysisSnapshot> snapshot,
                      std::optional<qsizetype> function = {});
    void refresh();
    void set_compact(bool compact);
    void select_address(Address address);
    void activate_current();
    std::function<void(const Record&, int)> activated;
    std::function<void(const Record&)> selected;
    QTableView* view() const { return view_; }
private:
    void request(quint64 offset);
    Dataset dataset_;
    std::shared_ptr<const AnalysisSnapshot> snapshot_;
    std::optional<qsizetype> function_;
    RecordModel* model_;
    QTableView* view_;
    QLineEdit* query_;
    QLabel* count_;
    QToolButton* previous_;
    QToolButton* next_;
    QTimer* timer_;
    QWidget* navigation_;
    bool compact_ = false;
    bool restoring_selection_ = false;
    std::optional<Address> pending_address_;
    std::stop_source stop_;
    quint64 token_ = 0;
};

} // namespace reworkbench::ui
