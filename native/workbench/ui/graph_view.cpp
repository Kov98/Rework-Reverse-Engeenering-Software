#include "graph_view.hpp"
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QMap>
#include <QWheelEvent>
#include <algorithm>

namespace reworkbench::ui {
GraphView::GraphView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setBackgroundBrush(QColor(QStringLiteral("#111722")));
}
void GraphView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const auto current = transform().m11();
        if (current * factor >= 0.15 && current * factor <= 4.0) scale(factor, factor);
        event->accept();
    } else QGraphicsView::wheelEvent(event);
}
void GraphView::show_function(const Function* function) {
    scene()->clear(); resetTransform();
    if (!function || function->blocks.isEmpty()) {
        auto* text = scene()->addText(QStringLiteral("Nenhum grafo disponível para a seleção."));
        text->setDefaultTextColor(QColor(QStringLiteral("#a6b3c5"))); return;
    }
    QMap<QString, QPointF> positions;
    const auto count = std::min<qsizetype>(200, function->blocks.size());
    for (qsizetype index = 0; index < count; ++index) {
        const auto& block = function->blocks[index];
        const QPointF position((index % 3) * 370.0, (index / 3) * 185.0);
        positions.insert(block.address.text(), position);
    }
    for (qsizetype index = 0; index < count; ++index) {
        const auto& block = function->blocks[index];
        const auto from = positions.value(block.address.text()) + QPointF(165, 135);
        for (const auto& successor : block.successors) {
            if (!positions.contains(successor.text())) continue;
            const auto to = positions.value(successor.text()) + QPointF(165, 0);
            QPainterPath path(from);
            path.cubicTo(from + QPointF(0, 45), to - QPointF(0, 45), to);
            scene()->addPath(path, QPen(QColor(QStringLiteral("#6cb5ba")), 2));
            scene()->addEllipse(to.x() - 3, to.y() - 3, 6, 6, QPen(Qt::NoPen), QBrush(QColor(QStringLiteral("#8bd5ca"))));
        }
    }
    for (qsizetype index = 0; index < count; ++index) {
        const auto& block = function->blocks[index];
        const auto position = positions.value(block.address.text());
        scene()->addRect(QRectF(position, QSizeF(330, 135)), QPen(QColor(QStringLiteral("#56718d"))), QBrush(QColor(QStringLiteral("#202d40"))));
        QStringList lines{block.address.text()};
        for (qsizetype row = 0; row < std::min<qsizetype>(4, block.instruction_indices.size()); ++row) {
            const auto instruction_index = block.instruction_indices[row];
            if (instruction_index < static_cast<quint32>(function->instructions.size())) {
                const auto& instruction = function->instructions[static_cast<qsizetype>(instruction_index)];
                lines.push_back((instruction.mnemonic + QLatin1Char(' ') + instruction.operands_text).left(45));
            }
        }
        if (block.instruction_indices.size() > 4) lines.push_back(QStringLiteral("… %1 instruções").arg(block.instruction_indices.size()));
        auto* text = scene()->addText(lines.join(QLatin1Char('\n')), QFont(QStringLiteral("Consolas"), 10));
        text->setDefaultTextColor(QColor(QStringLiteral("#dce4ef")));
        text->setPos(position + QPointF(8, 4));
        text->setTextWidth(310);
    }
    if (function->blocks.size() > count) {
        auto* warning = scene()->addText(QStringLiteral("Grafo visual limitado aos primeiros 200 blocos; os demais continuam no índice."));
        warning->setDefaultTextColor(QColor(QStringLiteral("#eed49f")));
        warning->setPos(0, (count / 3 + 1) * 185.0);
    }
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-25, -25, 25, 25));
}
} // namespace reworkbench::ui
