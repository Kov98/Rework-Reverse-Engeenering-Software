#pragma once
#include "reworkbench/types.hpp"

#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    std::function<void(const Record&, int)> activated;
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
    QPushButton* previous_;
    QPushButton* next_;
    QTimer* timer_;
    std::stop_source stop_;
    quint64 token_ = 0;
};

} // namespace reworkbench::ui
