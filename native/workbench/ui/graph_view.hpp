#pragma once
#include "reworkbench/types.hpp"
#include <QGraphicsView>
#include <QHash>
#include <functional>

class QGraphicsRectItem;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;

namespace reworkbench::ui {
class GraphView final : public QGraphicsView {
public:
    explicit GraphView(QWidget* parent = nullptr);
    void show_function(const Function* function);
    void select_address(Address address);
    void fit_graph();
    std::function<void(Address)> activated;
    std::function<void(Address)> selected;
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    QGraphicsRectItem* block_at(const QPoint& position) const;
    void select_block(QGraphicsRectItem* block, bool center);
    void zoom_by(double factor, bool under_mouse = false);
    void position_controls();
    QHash<QString, QGraphicsRectItem*> blocks_;
    QHash<QString, QString> instruction_blocks_;
    QHash<QString, Address> block_addresses_;
    QGraphicsRectItem* selected_block_ = nullptr;
    QWidget* zoom_controls_ = nullptr;
    bool fit_on_show_ = true;
};
} // namespace reworkbench::ui
