#include "graph_view.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"
#include <QFont>
#include <QFontMetricsF>
#include <QFrame>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlockFormat>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace reworkbench::ui {
namespace {
constexpr int block_address_role = 0;
constexpr int visible_instructions = 6;

QPen block_pen(bool selected) {
    QPen pen(theme_color(selected ? "graph.selectedBorder" : "graph.border"), 1.0);
    pen.setCosmetic(true);
    return pen;
}
QString escaped_code(const QString& text) {
    auto escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char(' '), QStringLiteral("&nbsp;"));
    return escaped;
}
struct Edge { int target; bool feedback = false; };
} // namespace

GraphView::GraphView(QWidget* parent) : QGraphicsView(parent) {
    setObjectName(QStringLiteral("functionGraph"));
    setScene(new QGraphicsScene(this));
    setFont(default_code_font());
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(theme_color("editor.background"));
    setFrameShape(QFrame::NoFrame);

    auto* controls = new QFrame(viewport());
    controls->setObjectName(QStringLiteral("graphZoomControls"));
    auto* layout = new QHBoxLayout(controls);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);
    const auto add_button = [controls, layout](const QString& icon, const QString& name, const QString& tooltip) {
        auto* button = new QToolButton(controls);
        button->setIcon(workbench_icon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setObjectName(name);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(27, 25);
        layout->addWidget(button);
        return button;
    };
    auto* out = add_button(QStringLiteral("zoom-out"), QStringLiteral("graphZoomOut"), QStringLiteral("Diminuir zoom"));
    auto* in = add_button(QStringLiteral("add"), QStringLiteral("graphZoomIn"), QStringLiteral("Aumentar zoom"));
    auto* fit = add_button(QStringLiteral("screen-full"), QStringLiteral("graphFit"), QStringLiteral("Ajustar grafo \u00e0 janela"));
    connect(out, &QToolButton::clicked, this, [this] { zoom_by(1.0 / 1.2); });
    connect(in, &QToolButton::clicked, this, [this] { zoom_by(1.2); });
    connect(fit, &QToolButton::clicked, this, [this] { fit_graph(); });
    zoom_controls_ = controls;
    controls->adjustSize();
    position_controls();
}
void GraphView::zoom_by(double factor, bool under_mouse) {
    const auto current = transform().m11();
    const auto target = std::clamp(current * factor, 0.15, 4.0);
    const auto anchor = transformationAnchor();
    setTransformationAnchor(under_mouse ? QGraphicsView::AnchorUnderMouse : QGraphicsView::AnchorViewCenter);
    if (current > 0) scale(target / current, target / current);
    setTransformationAnchor(anchor);
}
void GraphView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->angleDelta().y() != 0) zoom_by(std::pow(1.15, event->angleDelta().y() / 120.0), true);
        event->accept();
    } else QGraphicsView::wheelEvent(event);
}
QGraphicsRectItem* GraphView::block_at(const QPoint& position) const {
    for (auto* item = itemAt(position); item; item = item->parentItem()) {
        const auto address = item->data(block_address_role);
        if (address.isValid()) return blocks_.value(address.toString(), nullptr);
    }
    return nullptr;
}
void GraphView::select_block(QGraphicsRectItem* block, bool center) {
    if (selected_block_ && selected_block_ != block) {
        selected_block_->setPen(block_pen(false));
        selected_block_->setBrush(theme_color("graph.nodeBackground"));
    }
    selected_block_ = block;
    if (!block) return;
    block->setPen(block_pen(true));
    block->setBrush(theme_color("graph.selectedBackground"));
    if (center) centerOn(block);
}
void GraphView::select_address(Address address) {
    const auto key = address.text();
    const auto block_key = instruction_blocks_.value(key, key);
    if (auto* block = blocks_.value(block_key, nullptr)) select_block(block, true);
}
void GraphView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* block = block_at(event->position().toPoint())) {
            select_block(block, false);
            if (selected) selected(block_addresses_.value(block->data(block_address_role).toString()));
        }
    }
    QGraphicsView::mousePressEvent(event);
}
void GraphView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* block = block_at(event->position().toPoint())) {
            select_block(block, false);
            const auto address = block_addresses_.value(block->data(block_address_role).toString());
            event->accept();
            if (activated) activated(address);
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}
void GraphView::position_controls() {
    if (zoom_controls_) {
        zoom_controls_->move(std::max(4, viewport()->width() - zoom_controls_->width() - 10), 10);
        zoom_controls_->raise();
    }
}
void GraphView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    position_controls();
}
void GraphView::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    if (fit_on_show_) {
        fit_on_show_ = false;
        if (selected_block_) ensureVisible(selected_block_, 24, 24);
        else fit_graph();
    }
    position_controls();
}
void GraphView::fit_graph() {
    const auto bounds = scene()->itemsBoundingRect().adjusted(-24, -24, 24, 24);
    if (bounds.isEmpty() || viewport()->width() <= 1 || viewport()->height() <= 1) return;
    const double horizontal = std::max(1, viewport()->width() - 24) / bounds.width();
    const double vertical = std::max(1, viewport()->height() - 24) / bounds.height();
    const auto factor = std::clamp(std::min(horizontal, vertical), 0.45, 1.0);
    setTransform(QTransform::fromScale(factor, factor));
    if (selected_block_ && (horizontal < 0.45 || vertical < 0.45)) centerOn(selected_block_);
    else centerOn(bounds.center());
}
void GraphView::show_function(const Function* function) {
    selected_block_ = nullptr;
    blocks_.clear();
    instruction_blocks_.clear();
    block_addresses_.clear();
    scene()->clear();
    resetTransform();
    fit_on_show_ = !isVisible();
    if (!function || function->blocks.isEmpty()) {
        auto* text = scene()->addText(QStringLiteral("Nenhum grafo dispon\u00edvel para a sele\u00e7\u00e3o."));
        text->setDefaultTextColor(theme_color("descriptionForeground"));
        scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-25, -25, 25, 25));
        fit_graph();
        return;
    }
    const auto count = static_cast<int>(std::min<qsizetype>(200, function->blocks.size()));
    const auto graph_font = font();
    const QFontMetricsF metrics(graph_font, viewport());
    const qreal node_width = std::max<qreal>(310, metrics.horizontalAdvance(QString(40, QLatin1Char('0'))) + 20);
    const qreal column_spacing = node_width + 64;
    const qreal font_pixels = graph_font.pixelSize() > 0 ? graph_font.pixelSize() : graph_font.pointSizeF() * logicalDpiY() / 72;
    const qreal row_height = std::max<qreal>(metrics.lineSpacing(), qRound(font_pixels * 1.35));
    // Header and body offsets must grow with the same user-selected font as
    // instruction rows. Fixed 28px/37px offsets overlapped at larger sizes.
    const qreal header_height = std::max<qreal>(28, std::ceil(metrics.lineSpacing()) + 12);
    const qreal body_top = header_height + 9;
    QHash<QString, int> indices;
    for (int index = 0; index < count; ++index) indices.insert(function->blocks[index].address.text(), index);
    QVector<QVector<Edge>> successors(count);
    QVector<QVector<int>> predecessors(count);
    for (int index = 0; index < count; ++index) {
        for (const auto& successor : function->blocks[index].successors) {
            const auto target = indices.constFind(successor.text());
            if (target == indices.cend()) continue;
            successors[index].push_back({target.value(), false});
            predecessors[target.value()].push_back(index);
        }
    }

    // Rank the graph after identifying feedback edges. Forward paths descend;
    // the remaining loop edges are routed around the layers.
    QVector<int> visited(count, 0), order;
    const std::function<void(int)> visit = [&](int source) {
        visited[source] = 1;
        for (auto& edge : successors[source]) {
            if (visited[edge.target] == 1) edge.feedback = true;
            else if (visited[edge.target] == 0) visit(edge.target);
        }
        visited[source] = 2;
        order.push_back(source);
    };
    const auto entry = indices.value(function->address.text(), 0);
    visit(entry);
    for (int index = 0; index < count; ++index) if (visited[index] == 0) visit(index);
    std::reverse(order.begin(), order.end());
    QVector<int> rank(count, 0);
    for (const auto source : order)
        for (const auto& edge : successors[source])
            if (!edge.feedback) rank[edge.target] = std::max(rank[edge.target], rank[source] + 1);
    const auto last_rank = *std::max_element(rank.cbegin(), rank.cend());
    QVector<QVector<int>> layers(last_rank + 1);
    for (int index = 0; index < count; ++index) layers[rank[index]].push_back(index);

    // Barycenter sweeps place branches near their parents and merges near the
    // incoming paths without an additional graph-layout dependency.
    QVector<double> horizontal_order(count, 0);
    const auto update_order = [&] {
        for (const auto& layer : layers)
            for (qsizetype slot = 0; slot < layer.size(); ++slot)
                horizontal_order[layer[slot]] = slot - (layer.size() - 1) / 2.0;
    };
    update_order();
    for (int sweep = 0; sweep < 4; ++sweep) {
        const bool downward = sweep % 2 == 0;
        for (int step = 0; step <= last_rank; ++step) {
            const int level = downward ? step : last_rank - step;
            QHash<int, double> scores;
            for (const auto node : layers[level]) {
                double sum = 0;
                int neighbors = 0;
                if (downward) {
                    for (const auto source : predecessors[node]) if (rank[source] < level) {
                        sum += horizontal_order[source];
                        ++neighbors;
                    }
                } else {
                    for (const auto& edge : successors[node]) if (rank[edge.target] > level) {
                        sum += horizontal_order[edge.target];
                        ++neighbors;
                    }
                }
                scores.insert(node, neighbors ? sum / neighbors : horizontal_order[node]);
            }
            std::stable_sort(layers[level].begin(), layers[level].end(), [&](int left, int right) {
                return scores.value(left) < scores.value(right);
            });
            update_order();
        }
    }

    QVector<QRectF> rectangles(count);
    qreal next_y = 0;
    for (const auto& layer : layers) {
        qreal layer_height = 0;
        for (qsizetype slot = 0; slot < layer.size(); ++slot) {
            const int index = layer[slot];
            const auto& block = function->blocks[index];
            const auto rows = std::min<qsizetype>(visible_instructions, block.instruction_indices.size());
            const qreal height = body_top + 7 + std::max<qsizetype>(1, rows) * row_height + (block.instruction_indices.size() > rows ? row_height : 0);
            rectangles[index] = QRectF((slot - (layer.size() - 1) / 2.0) * column_spacing, next_y, node_width, height);
            layer_height = std::max(layer_height, height);
        }
        next_y += layer_height + 86;
    }
    qreal right_boundary = 0;
    for (const auto& rectangle : rectangles) right_boundary = std::max(right_boundary, rectangle.right());
    int routed_edge = 0;
    for (int source = 0; source < count; ++source) {
        for (qsizetype branch = 0; branch < successors[source].size(); ++branch) {
            const auto& edge = successors[source][branch];
            const auto& from_rect = rectangles[source];
            const auto& to_rect = rectangles[edge.target];
            const auto branches = successors[source].size();
            const qreal spread = std::min<qreal>(140, (branches - 1) * 18);
            const qreal offset = branches > 1 ? (branch / static_cast<qreal>(branches - 1) - 0.5) * spread : 0;
            const auto from = QPointF(from_rect.center().x() + offset, from_rect.bottom());
            const auto to = QPointF(to_rect.center().x(), to_rect.top());
            QColor color = theme_color("charts.blue");
            if (edge.feedback) color = theme_color("syntax.control");
            else if (successors[source].size() > 1) color = theme_color(branch == 0 ? "charts.green" : "charts.red");
            QPainterPath path(from);
            if (edge.feedback || rank[edge.target] > rank[source] + 1) {
                const qreal lane = right_boundary + 35 + (routed_edge++ % 8) * 14;
                path.lineTo(from.x(), from.y() + 24);
                path.lineTo(lane, from.y() + 24);
                path.lineTo(lane, to.y() - 24);
                path.lineTo(to.x(), to.y() - 24);
                path.lineTo(to);
            } else {
                const auto bend = std::max<qreal>(30, (to.y() - from.y()) / 2);
                path.cubicTo(from + QPointF(0, bend), to - QPointF(0, bend), to);
            }
            QPen pen(color, 1.6);
            pen.setCosmetic(true);
            auto* line = scene()->addPath(path, pen);
            line->setZValue(-2);
            line->setAcceptedMouseButtons(Qt::NoButton);
            const QPolygonF arrow{to, to + QPointF(-4, -8), to + QPointF(4, -8)};
            auto* tip = scene()->addPolygon(arrow, QPen(Qt::NoPen), QBrush(color));
            tip->setZValue(-1);
            tip->setAcceptedMouseButtons(Qt::NoButton);
        }
    }

    for (int index = 0; index < count; ++index) {
        const auto& block = function->blocks[index];
        const auto key = block.address.text();
        const auto& rectangle = rectangles[index];
        auto* panel = scene()->addRect(QRectF(QPointF(0, 0), rectangle.size()), block_pen(false), QBrush(theme_color("graph.nodeBackground")));
        panel->setPos(rectangle.topLeft());
        panel->setZValue(1);
        panel->setData(block_address_role, key);
        panel->setAcceptedMouseButtons(Qt::NoButton);
        blocks_.insert(key, panel);
        block_addresses_.insert(key, block.address);
        auto* strip = new QGraphicsRectItem(QRectF(1, 1, node_width - 2, header_height), panel);
        strip->setPen(QPen(Qt::NoPen));
        strip->setBrush(theme_color("sideBar.background"));
        strip->setAcceptedMouseButtons(Qt::NoButton);
        auto* title = new QGraphicsTextItem(panel);
        title->setFont(graph_font);
        title->document()->setDocumentMargin(0);
        title->setHtml(QStringLiteral("<span style='color:%1'>%2</span><span style='color:%3'>%4</span>")
            .arg(theme_hex("editor.foreground"), key.toHtmlEscaped(), theme_hex("descriptionForeground"),
                 index == entry ? QStringLiteral(" &nbsp; ENTRY") : QString()));
        title->setPos(10, (header_height - metrics.lineSpacing()) / 2);
        title->setTextWidth(node_width - 20);
        title->setAcceptedMouseButtons(Qt::NoButton);

        QStringList lines;
        QStringList tooltip{key};
        for (qsizetype row = 0; row < block.instruction_indices.size(); ++row) {
            const auto instruction_index = block.instruction_indices[row];
            if (instruction_index >= static_cast<quint32>(function->instructions.size())) continue;
            const auto& instruction = function->instructions[static_cast<qsizetype>(instruction_index)];
            instruction_blocks_.insert(instruction.address.text(), key);
            if (row < 16) tooltip.push_back(instruction.address.text() + QLatin1Char(' ') + instruction.mnemonic + QLatin1Char(' ') + instruction.operands_text);
            if (row >= visible_instructions) continue;
            const auto address = QString::number(instruction.address.value, 16).right(8).rightJustified(8, QLatin1Char('0'));
            const auto mnemonic = instruction.mnemonic.left(9).leftJustified(9, QLatin1Char(' '));
            auto operands = instruction.operands_text;
            if (operands.size() > 19) operands = operands.left(18) + QChar(0x2026);
            const auto mnemonic_color = theme_hex(instruction.flow == FlowKind::Normal ? "syntax.mnemonic" : "syntax.control");
            lines.push_back(QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;<span style='color:%3'>%4</span><span style='color:%5'>%6</span>")
                .arg(theme_hex("editorLineNumber.foreground"), address, mnemonic_color, escaped_code(mnemonic),
                     theme_hex("editor.foreground"), escaped_code(operands)));
        }
        if (block.instruction_indices.size() > visible_instructions)
            lines.push_back(QStringLiteral("<span style='color:%1'>\u2026 %2 instru\u00e7\u00f5es</span>")
                .arg(theme_hex("descriptionForeground")).arg(block.instruction_indices.size()));
        if (lines.isEmpty()) lines.push_back(QStringLiteral("<span style='color:%1'>Bloco vazio</span>").arg(theme_hex("descriptionForeground")));
        auto* body = new QGraphicsTextItem(panel);
        body->setFont(graph_font);
        body->document()->setDocumentMargin(0);
        body->setHtml(QStringLiteral("<div style='white-space:pre'>%1</div>").arg(lines.join(QStringLiteral("<br>"))));
        QTextCursor layout_cursor(body->document()); layout_cursor.select(QTextCursor::Document);
        QTextBlockFormat block_format; block_format.setLineHeight(row_height, QTextBlockFormat::FixedHeight);
        layout_cursor.mergeBlockFormat(block_format);
        body->setPos(10, body_top);
        body->setTextWidth(node_width - 20);
        body->setAcceptedMouseButtons(Qt::NoButton);
        panel->setToolTip(tooltip.join(QLatin1Char('\n')));
    }
    if (function->blocks.size() > count) {
        auto* warning = scene()->addText(QStringLiteral("Grafo limitado a 200 blocos; os demais continuam no \u00edndice."));
        warning->setDefaultTextColor(theme_color("descriptionForeground"));
        warning->setPos(0, next_y);
    }
    select_block(blocks_.value(function->blocks[entry].address.text()), false);
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-35, -35, 35, 35));
    if (selected_block_) ensureVisible(selected_block_, 24, 24);
}
} // namespace reworkbench::ui
