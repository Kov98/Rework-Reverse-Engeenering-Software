#pragma once
#include "reworkbench/types.hpp"
#include <QGraphicsView>

namespace reworkbench::ui {
class GraphView final : public QGraphicsView {
public:
    explicit GraphView(QWidget* parent = nullptr);
    void show_function(const Function* function);
protected:
    void wheelEvent(QWheelEvent* event) override;
};
} // namespace reworkbench::ui
