#include "binary_explorer.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"

#include <QCursor>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace reworkbench::ui {
namespace {
constexpr qsizetype batch_size = 1000;
constexpr int address_role = Qt::UserRole;
constexpr int offset_role = Qt::UserRole + 1;
constexpr int kind_role = Qt::UserRole + 2;
constexpr int category_role = Qt::UserRole + 3;
enum ItemKind { Header, Leaf, More, Loading };
enum CategoryIndex { Functions, Imports, Regions, Symbols, Strings };

class ExplorerItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto* tree = qobject_cast<const QTreeWidget*>(option.widget);
        if (!tree) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        const bool enabled = option.state.testFlag(QStyle::State_Enabled);
        const bool selected = enabled && option.state.testFlag(QStyle::State_Selected);
        const bool hovered = enabled && (option.state.testFlag(QStyle::State_MouseOver) ||
            (tree->viewport()->underMouse() && tree->indexAt(tree->viewport()->mapFromGlobal(QCursor::pos())) == index));
        const bool focused = tree->hasFocus() && tree->currentIndex() == index;
        const auto radius = theme_metric("radius.item");
        const QRect row(0, option.rect.y(), tree->viewport()->width(), option.rect.height());
        QRectF highlight(row);
        highlight.setLeft(theme_metric("spacing.unit"));
        highlight.setRight(tree->viewport()->width() - theme_metric("spacing.unit"));
        highlight.adjust(0, 0.5, 0, -0.5);

        // Qt adds selection/hover inside drawRow and paints its branch background
        // before calling the delegate. Replace that square background here, where
        // the final state is available, before any text or icons are painted.
        painter->save();
        painter->fillRect(row, theme_color("sideBar.background"));
        painter->setRenderHint(QPainter::Antialiasing);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme_color(selected ? (tree->hasFocus() ? "list.activeSelectionBackground" : "list.inactiveSelectionBackground") : "list.hoverBackground"));
            painter->drawRoundedRect(highlight, radius, radius);
        }
        if (enabled && focused) {
            painter->setBrush(Qt::NoBrush);
            painter->setPen(QPen(theme_color("focusBorder"), 1));
            painter->drawRoundedRect(highlight, radius, radius);
        }
        painter->restore();

        // Keep Qt's text/icon layout, with transparent item QSS and no native
        // interaction state so it cannot cover the rounded background again.
        auto content = option;
        content.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        content.backgroundBrush = Qt::NoBrush;
        if (selected) content.palette.setColor(QPalette::Text, theme_color("list.activeSelectionForeground"));
        QStyledItemDelegate::paint(painter, content, index);
    }
};

// VS Code advances each hierarchy level by 8px while its disclosure control
// remains 16px wide. Preserve the Qt tree model and interactions, reserving the
// other 8px in the item padding and painting the original Codicon over that slot.
class ExplorerTree final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
protected:
    void drawBranches(QPainter*, const QRect&, const QModelIndex&) const override {}
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QTreeWidget::drawRow(painter, option, index);
        if (model()->hasChildren(index)) {
            workbench_icon(isExpanded(index) ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"))
                .paint(painter, disclosure_rect(index), Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        const auto index = indexAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && index.isValid() && model()->hasChildren(index) &&
            disclosure_rect(index).contains(event->position().toPoint())) {
            setExpanded(index, !isExpanded(index));
            event->accept();
            return;
        }
        QTreeWidget::mousePressEvent(event);
    }
private:
    QRect disclosure_rect(const QModelIndex& index) const {
        const auto row = visualRect(index);
        return QRect(row.left() - indentation(), row.center().y() - 7, 16, 16);
    }
};

struct Entry {
    QString name;
    QString detail;
    QString address;
    std::optional<quint64> offset;
};
struct Batch {
    QVector<Entry> entries;
    qsizetype cursor = 0;
    bool exhausted = false;
    QString error;
};

qsizetype category_size(const AnalysisSnapshot& snapshot, int category) {
    switch (category) {
    case Functions: return snapshot.functions.size();
    case Imports: return snapshot.metadata.imports.size();
    case Regions: return snapshot.metadata.regions.size();
    case Symbols: return snapshot.metadata.symbols.size();
    case Strings: return snapshot.strings.size();
    default: return 0;
    }
}

Entry entry_at(const AnalysisSnapshot& snapshot, int category, qsizetype index) {
    switch (category) {
    case Functions: {
        const auto& function = snapshot.functions[index];
        return {function.name, QStringLiteral("%1 instruções%2").arg(function.instructions.size())
            .arg(function.partial ? QStringLiteral(" · análise parcial") : QString{}), function.address.text(), {}};
    }
    case Imports: {
        const auto& imported = snapshot.metadata.imports[index];
        return {imported.name, imported.library, imported.slot ? imported.slot->text() : QString{}, {}};
    }
    case Regions: {
        const auto& region = snapshot.metadata.regions[index];
        const QString permissions = QString(region.readable ? QLatin1Char('r') : QLatin1Char('-')) +
            (region.writable ? QLatin1Char('w') : QLatin1Char('-')) +
            (region.executable ? QLatin1Char('x') : QLatin1Char('-'));
        return {region.name, QStringLiteral("%1 · %2 bytes").arg(permissions).arg(region.virtual_size),
            region.start.text(), region.file_offset};
    }
    case Symbols: {
        const auto& symbol = snapshot.metadata.symbols[index];
        return {symbol.name, symbol.kind, symbol.address.text(), {}};
    }
    case Strings: {
        const auto& string = snapshot.strings[index];
        return {string.value, QStringLiteral("Offset %1").arg(hex(string.file_offset)), {}, string.file_offset};
    }
    default: return {};
    }
}

Batch build_batch(const AnalysisSnapshot& snapshot, int category, qsizetype cursor,
                  const QString& query, std::stop_token stop) {
    Batch result;
    result.cursor = cursor;
    const auto total = category_size(snapshot, category);
    result.entries.reserve(std::min(batch_size, total - std::min(cursor, total)));
    while (result.cursor < total && result.entries.size() < batch_size) {
        check_cancelled(stop);
        auto entry = entry_at(snapshot, category, result.cursor++);
        if (!query.isEmpty() && !entry.name.contains(query, Qt::CaseInsensitive) &&
            !entry.address.contains(query, Qt::CaseInsensitive) &&
            !entry.detail.contains(query, Qt::CaseInsensitive) &&
            !(entry.offset && hex(*entry.offset).contains(query, Qt::CaseInsensitive))) continue;
        result.entries.push_back(std::move(entry));
    }
    result.exhausted = result.cursor >= total;
    return result;
}

QIcon tree_icon(int category, const QString& kind = {}) {
    switch (category) {
    case Functions: return symbol_icon(QStringLiteral("function"));
    case Imports: return symbol_icon(QStringLiteral("import"));
    case Regions: return symbol_icon(QStringLiteral("memory"));
    case Symbols: return symbol_icon(kind.isEmpty() ? QStringLiteral("symbol") : kind);
    case Strings: return symbol_icon(QStringLiteral("string"));
    default: return symbol_icon(QStringLiteral("symbol"));
    }
}

QString tooltip(const Entry& entry) {
    QStringList lines{entry.name.toHtmlEscaped()};
    if (!entry.address.isEmpty()) lines << entry.address.toHtmlEscaped();
    if (!entry.detail.isEmpty()) lines << entry.detail.toHtmlEscaped();
    return QStringLiteral("<qt>%1</qt>").arg(lines.join(QStringLiteral("<br>")));
}
} // namespace

BinaryExplorer::BinaryExplorer(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("binaryExplorer"));
    setMinimumWidth(160);
    setFont(interface_font());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(theme_metric("spacing.unit"));
    filter_ = new QLineEdit;
    filter_->setObjectName(QStringLiteral("binaryTreeFilter"));
    filter_->setPlaceholderText(QStringLiteral("Filtrar nomes ou endereços…"));
    filter_->setClearButtonEnabled(true); filter_->setFixedHeight(27);
    filter_->setStyleSheet(themed_stylesheet(QStringLiteral("QLineEdit#binaryTreeFilter { border-radius: @{radius.input}; }")));
    layout->addWidget(filter_);
    tree_ = new ExplorerTree;
    tree_->setObjectName(QStringLiteral("binaryTree"));
    tree_->setFont(interface_font());
    tree_->setHeaderHidden(true); tree_->setColumnCount(1); tree_->setIndentation(8);
    tree_->setUniformRowHeights(true); tree_->setIconSize(QSize(16, 16));
    tree_->setMouseTracking(true); tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setItemDelegate(new ExplorerItemDelegate(tree_));
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setStyleSheet(themed_stylesheet(QStringLiteral(R"(
        QTreeWidget#binaryTree { border: none; outline: none; }
        QTreeWidget#binaryTree::item { height: 22px; padding: 0 0 0 8px; border: none; background: transparent; }
        QTreeWidget#binaryTree::item:hover, QTreeWidget#binaryTree::item:selected { background: transparent; }
    )")));
    tree_->installEventFilter(this);
    layout->addWidget(tree_, 1);
    filter_timer_ = new QTimer(this); filter_timer_->setSingleShot(true); filter_timer_->setInterval(160);
    connect(filter_, &QLineEdit::textChanged, this, [this] { filter_timer_->start(); });
    connect(filter_timer_, &QTimer::timeout, this, [this] { rebuild(); });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int) { activate_item(item); });
    rebuild();
}

BinaryExplorer::~BinaryExplorer() { stop_.request_stop(); }

void BinaryExplorer::set_snapshot(std::shared_ptr<const AnalysisSnapshot> snapshot) {
    filter_timer_->stop();
    {
        const QSignalBlocker blocked(filter_);
        filter_->clear();
    }
    snapshot_ = std::move(snapshot); selected_address_.clear(); rebuild();
}

void BinaryExplorer::set_busy(bool busy) {
    busy_ = busy; tree_->setEnabled(!busy); filter_->setEnabled(!busy);
}

void BinaryExplorer::rebuild() {
    stop_.request_stop(); stop_ = std::stop_source{}; ++generation_;
    const QSignalBlocker blocked(tree_);
    tree_->clear(); categories_ = {};
    if (!snapshot_) return;
    auto name = snapshot_->metadata.original_name;
    if (name.isEmpty()) name = QFileInfo(snapshot_->metadata.source_path).fileName();
    if (name.isEmpty()) name = QStringLiteral("Amostra");
    auto* root = new QTreeWidgetItem(tree_, {name});
    root->setData(0, kind_role, Header); root->setExpanded(true);
    root->setIcon(0, file_icon(name));
    root->setToolTip(0, QStringLiteral("<qt>%1<br>%2</qt>").arg(name.toHtmlEscaped(), snapshot_->metadata.source_path.toHtmlEscaped()));
    const QStringList names{QStringLiteral("Funções"), QStringLiteral("Imports"), QStringLiteral("Regiões"), QStringLiteral("Símbolos"), QStringLiteral("Strings")};
    for (int index = 0; index < static_cast<int>(categories_.size()); ++index) {
        const auto total = category_size(*snapshot_, index);
        auto* item = new QTreeWidgetItem(root, {QStringLiteral("%1  %2").arg(names[index]).arg(total)});
        item->setData(0, kind_role, Header); item->setData(0, category_role, index);
        item->setIcon(0, tree_icon(index));
        item->setExpanded(index == Functions || !filter_->text().isEmpty());
        categories_[index].item = item;
        request(index);
    }
}

void BinaryExplorer::request(int category) {
    if (!snapshot_ || category < 0 || category >= static_cast<int>(categories_.size())) return;
    auto& state = categories_[category];
    if (state.loading || state.exhausted || !state.item) return;
    state.loading = true;
    delete state.more;
    state.more = new QTreeWidgetItem(state.item, {QStringLiteral("Carregando…")});
    state.more->setData(0, kind_role, Loading);
    state.more->setForeground(0, theme_color("descriptionForeground"));
    const auto generation = generation_;
    auto* watcher = new QFutureWatcher<Batch>(this);
    connect(watcher, &QFutureWatcher<Batch>::finished, this, [this, watcher, category, generation] {
        const auto batch = watcher->result(); watcher->deleteLater();
        if (generation != generation_) return;
        auto& current = categories_[category];
        current.loading = false; current.cursor = batch.cursor; current.exhausted = batch.exhausted;
        const QSignalBlocker blocked(tree_);
        tree_->setUpdatesEnabled(false);
        delete current.more; current.more = nullptr;
        for (const auto& entry : batch.entries) {
            auto label = entry.name;
            label.replace(QLatin1Char('\n'), QLatin1Char(' ')); label.replace(QLatin1Char('\r'), QLatin1Char(' '));
            if (label.isEmpty()) label = entry.address.isEmpty() ? QStringLiteral("Sem nome") : entry.address;
            auto* item = new QTreeWidgetItem(current.item, {label.left(512)});
            item->setData(0, kind_role, Leaf); item->setIcon(0, tree_icon(category, category == Symbols ? entry.detail : QString{}));
            if (!entry.address.isEmpty()) item->setData(0, address_role, entry.address);
            if (entry.offset) item->setData(0, offset_role, QVariant::fromValue(*entry.offset));
            item->setToolTip(0, tooltip(entry));
        }
        if (!batch.error.isEmpty()) {
            current.more = new QTreeWidgetItem(current.item, {batch.error});
            current.more->setData(0, kind_role, Loading);
        } else if (!current.exhausted) {
            current.more = new QTreeWidgetItem(current.item, {QStringLiteral("Carregar mais…")});
            current.more->setData(0, kind_role, More); current.more->setData(0, category_role, category);
            current.more->setForeground(0, theme_color("textLink.foreground"));
            current.more->setToolTip(0, QStringLiteral("Exibir até 1000 resultados adicionais desta categoria."));
        }
        if (!filter_->text().isEmpty()) current.item->setHidden(current.item->childCount() == 0 && current.exhausted);
        restore_selection(); tree_->setUpdatesEnabled(true);
    });
    watcher->setFuture(QtConcurrent::run([snapshot = snapshot_, category, cursor = state.cursor,
                                          query = filter_->text().trimmed(), stop = stop_.get_token()] {
        try { return build_batch(*snapshot, category, cursor, query, stop); }
        catch (const Cancelled&) { return Batch{}; }
        catch (const std::exception& error) { Batch result; result.exhausted = true; result.error = QString::fromUtf8(error.what()); return result; }
        catch (...) { Batch result; result.exhausted = true; result.error = QStringLiteral("Não foi possível carregar a categoria."); return result; }
    }));
}

void BinaryExplorer::activate_item(QTreeWidgetItem* item) {
    if (!item || busy_) return;
    const auto kind = item->data(0, kind_role).toInt();
    if (kind == More) { request(item->data(0, category_role).toInt()); return; }
    if (kind != Leaf) return;
    const auto text = item->data(0, address_role).toString();
    if (!text.isEmpty()) {
        // Address data originates in the snapshot; split here to preserve the
        // address space without accepting or interpreting sample text as code.
        const auto separator = text.lastIndexOf(QStringLiteral(":0x"));
        bool valid = false;
        const auto value = text.mid(separator + 3).toULongLong(&valid, 16);
        if (separator >= 0 && valid && activated) activated(Address{text.left(separator), value});
    } else if (item->data(0, offset_role).isValid() && offset_activated) {
        offset_activated(item->data(0, offset_role).toULongLong());
    }
}

bool BinaryExplorer::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tree_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            auto* item = tree_->currentItem();
            if (item && item->data(0, kind_role).toInt() == Header) item->setExpanded(!item->isExpanded());
            else activate_item(item);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BinaryExplorer::select_address(Address address) {
    selected_address_ = address.text(); restore_selection();
}

void BinaryExplorer::restore_selection() {
    const QSignalBlocker blocked(tree_);
    tree_->clearSelection();
    tree_->setCurrentItem(nullptr);
    if (selected_address_.isEmpty()) return;
    // Prefer the function leaf when an entry point is also a symbol/region.
    for (const auto& category : categories_) {
        if (!category.item) continue;
        for (int index = 0; index < category.item->childCount(); ++index) {
            auto* item = category.item->child(index);
            if (item->data(0, address_role).toString() != selected_address_) continue;
            category.item->setExpanded(true); tree_->setCurrentItem(item); tree_->scrollToItem(item);
            return;
        }
    }
}

} // namespace reworkbench::ui
