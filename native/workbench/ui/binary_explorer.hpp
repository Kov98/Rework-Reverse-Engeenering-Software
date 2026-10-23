#pragma once
#include "reworkbench/types.hpp"

#include <QWidget>
#include <array>
#include <functional>
#include <memory>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace reworkbench::ui {

class BinaryExplorer final : public QWidget {
public:
    explicit BinaryExplorer(QWidget* parent = nullptr);
    ~BinaryExplorer() override;
    void set_snapshot(std::shared_ptr<const AnalysisSnapshot> snapshot);
    void select_address(Address address);
    void set_busy(bool busy);
    std::function<void(Address)> activated;
    std::function<void(quint64)> offset_activated;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Category {
        QTreeWidgetItem* item = nullptr;
        QTreeWidgetItem* more = nullptr;
        qsizetype cursor = 0;
        bool loading = false;
        bool exhausted = false;
    };
    void rebuild();
    void request(int category);
    void activate_item(QTreeWidgetItem* item);
    void restore_selection();

    QLineEdit* filter_;
    QTreeWidget* tree_;
    QTimer* filter_timer_;
    std::shared_ptr<const AnalysisSnapshot> snapshot_;
    std::array<Category, 5> categories_{};
    std::stop_source stop_;
    quint64 generation_ = 0;
    QString selected_address_;
    bool busy_ = false;
};

} // namespace reworkbench::ui
