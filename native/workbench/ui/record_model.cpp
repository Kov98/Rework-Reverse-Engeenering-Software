#include "record_model.hpp"
#include <QScopedValueRollback>
#include "reworkbench/binary_image.hpp"
#include "workspace_theme.hpp"
#include "workbench_icons.hpp"

#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QAction>
#include <QBrush>
#include <QMenu>
#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace reworkbench::ui {
namespace {
constexpr quint64 page_size = 200;

class AssemblyDelegate final : public QStyledItemDelegate {
public:
    AssemblyDelegate(const RecordModel* model, QObject* parent) : QStyledItemDelegate(parent), model_(model) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const auto* record = model_->record(index.row());
        if (!record || !record->assembly || index.column() != 3) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const auto text = item.text;
        item.text.clear();
        const auto* widget = item.widget;
        auto* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, widget);
        const auto bounds = style->subElementRect(QStyle::SE_ItemViewItemText, &item, widget);

        // Prefer decoder register metadata; the fallback recognizes register spellings only.
        static const QRegularExpression registers(QStringLiteral(
            "^(?:(?:[re]?(?:ax|bx|cx|dx|si|di|bp|sp|ip))|[abcd][lh]|[sd]il|[bs]pl|"
            "r(?:[0-9]|1[0-5])(?:[bwd])?|[cdefgs]s|(?:xmm|ymm|zmm|mm|st|cr|dr)[0-9]+|"
            "[xw](?:[0-9]|[12][0-9]|30)|[vqdsbh](?:[0-9]|[12][0-9]|3[01])|"
            "sp|wsp|pc|lr|fp|ip|[xw]zr|cpsr|nzcv|"
            "\\$(?:[0-9]|[12][0-9]|3[01]|zero|at|[va][0-3]|[ts][0-9]|[kg][01]|sp|fp|ra))$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression tokens(QStringLiteral(
            "(?:\\$[0-9]+)|(?:0[xX][0-9a-fA-F]+|[0-9]+(?:\\.[0-9]+)?)|"
            "(?:[$%]?[A-Za-z_?.][A-Za-z_0-9?.@$]*)|"
            "(?:\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*')|[^\\s]"));
        static const QRegularExpression memory_qualifiers(QStringLiteral(
            "^(?:byte|word|dword|qword|tbyte|oword|xmmword|ymmword|zmmword|ptr|offset|short|near|far)$"),
            QRegularExpression::CaseInsensitiveOption);
        QVector<QTextLayout::FormatRange> formats;
        auto matches = tokens.globalMatch(text);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const auto word = match.captured();
            auto register_name = word;
            if (register_name.startsWith(QLatin1Char('%'))) register_name.remove(0, 1);
            const char* token = "syntax.operator";
            const auto register_base = register_name.section(QLatin1Char('.'), 0, 0);
            if (record->operand_registers.contains(register_base, Qt::CaseInsensitive) || registers.match(register_base).hasMatch())
                token = "syntax.register";
            else if (word.front().isDigit()) {
                bool valid = false;
                const auto number = word.toULongLong(&valid, 0);
                token = record->flow == FlowKind::Call && record->other_address && valid && number == record->other_address->value
                    ? "syntax.function" : "syntax.number";
            }
            else if (word.front() == QLatin1Char('"') || word.front() == QLatin1Char('\'')) token = "syntax.string";
            else if (memory_qualifiers.match(word).hasMatch()) token = "syntax.keyword";
            else if (word.front().isLetter() || word.front() == QLatin1Char('_') || word.front() == QLatin1Char('$') || word.front() == QLatin1Char('.'))
                token = "syntax.label";
            QTextCharFormat format;
            format.setForeground(theme_color(token));
            formats.push_back({static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength()), format});
        }
        QTextLayout layout(text, item.font);
        layout.setFormats(formats);
        QTextOption text_option;
        text_option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(text_option);
        layout.beginLayout();
        auto line = layout.createLine();
        if (line.isValid()) line.setLineWidth(bounds.width());
        layout.endLayout();
        painter->save();
        painter->setClipRect(bounds);
        painter->setPen(item.palette.color(QPalette::Text));
        const qreal height = line.isValid() ? line.height() : item.fontMetrics.height();
        layout.draw(painter, QPointF(bounds.left(), bounds.top() + (bounds.height() - height) / 2));
        painter->restore();
    }
private:
    const RecordModel* model_;
};

Page build_page(const AnalysisSnapshot& snapshot, Dataset dataset,
                std::optional<qsizetype> selected, quint64 offset,
                const QString& query, std::stop_token stop) {
    Page page;
    page.offset = offset;
    const auto accept = [&](Record row) {
        check_cancelled(stop);
        if (!query.isEmpty() && !row.cells.join(QLatin1Char(' ')).contains(query, Qt::CaseInsensitive)) return;
        if (page.total >= offset && page.rows.size() < static_cast<qsizetype>(page_size))
            page.rows.push_back(std::move(row));
        ++page.total;
    };
    switch (dataset) {
    case Dataset::Functions:
        for (qsizetype index = 0; index < snapshot.functions.size(); ++index) {
            const auto& function = snapshot.functions[index];
            accept({{function.address.text(), function.name, QString::number(function.instructions.size()), function.partial ? QStringLiteral("Parcial") : QStringLiteral("Descoberta")}, function.address, {}, {}, index});
        }
        break;
    case Dataset::Symbols:
        for (const auto& symbol : snapshot.metadata.symbols)
            accept({{symbol.address.text(), symbol.name, symbol.kind}, symbol.address, {}, {}, -1});
        break;
    case Dataset::Strings:
        for (const auto& string : snapshot.strings)
            accept({{hex(string.file_offset), string.value}, {}, {}, string.file_offset, -1});
        break;
    case Dataset::Imports:
        for (const auto& imported : snapshot.metadata.imports)
            accept({{imported.slot ? imported.slot->text() : QStringLiteral("—"), imported.name, imported.library}, imported.slot, {}, {}, -1});
        break;
    case Dataset::Regions:
        for (const auto& region : snapshot.metadata.regions) {
            const QString permissions = QString(region.readable ? QLatin1Char('r') : QLatin1Char('-')) +
                (region.writable ? QLatin1Char('w') : QLatin1Char('-')) +
                (region.executable ? QLatin1Char('x') : QLatin1Char('-'));
            accept({{region.start.text(), region.name, hex(region.virtual_size), permissions,
                     region.file_offset ? hex(*region.file_offset) : QStringLiteral("Sem backing")},
                    region.start, {}, region.file_offset, -1});
        }
        break;
    case Dataset::References:
        for (const auto& reference : snapshot.references) {
            if (selected && *selected >= 0 && *selected < snapshot.functions.size()) {
                const auto address = snapshot.functions[*selected].address;
                if (reference.source_function != std::optional<Address>(address) && reference.target != address) continue;
            }
            accept({{reference.source.text(), reference.target.text(), reference.type},
                    reference.source, reference.target, {}, -1});
        }
        break;
    case Dataset::Assembly:
        if (selected && *selected >= 0 && *selected < snapshot.functions.size())
            for (const auto& instruction : snapshot.functions[*selected].instructions) {
                Record row{{instruction.address.text(), QString::fromLatin1(instruction.bytes.toHex(' ')), instruction.mnemonic,
                         instruction.operands_text}, instruction.address, instruction.direct_target ? instruction.direct_target : instruction.memory_target,
                        reworkbench::file_offset(snapshot.metadata, instruction.address, static_cast<quint64>(instruction.bytes.size())), *selected};
                row.assembly = true;
                row.flow = instruction.flow;
                for (const auto& operand : instruction.operands) {
                    if (!operand.reg.isEmpty()) row.operand_registers.push_back(operand.reg);
                    if (!operand.memory_base.isEmpty()) row.operand_registers.push_back(operand.memory_base);
                    if (!operand.memory_index.isEmpty()) row.operand_registers.push_back(operand.memory_index);
                }
                row.operand_registers.removeDuplicates();
                accept(std::move(row));
            }
        break;
    }
    return page;
}
} // namespace

RecordModel::RecordModel(QStringList headers, QObject* parent)
    : QAbstractTableModel(parent), headers_(std::move(headers)) {}
int RecordModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(page_.rows.size()); }
int RecordModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(headers_.size()); }
QVariant RecordModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= page_.rows.size()) return {};
    const auto& cells = page_.rows[index.row()].cells;
    if (role == Qt::ForegroundRole) {
        if (index.column() == 0 || (page_.rows[index.row()].assembly && index.column() == 1)) return QBrush(theme_color("syntax.address"));
        if (page_.rows[index.row()].assembly && index.column() == 2)
            return QBrush(theme_color(page_.rows[index.row()].flow == FlowKind::Normal ? "syntax.mnemonic" : "syntax.control"));
    }
    if (role == Qt::DisplayRole && index.column() == 0 && page_.rows[index.row()].address)
        return QString::number(page_.rows[index.row()].address->value, 16).rightJustified(8, '0');
    if ((role == Qt::DisplayRole || role == Qt::ToolTipRole) && index.column() >= 0 && index.column() < cells.size()) return cells[index.column()];
    return {};
}
QVariant RecordModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal && section >= 0 && section < headers_.size()) return headers_[section];
    return QString::number(page_.offset + static_cast<quint64>(section) + 1);
}
void RecordModel::set_page(Page page) { beginResetModel(); page_ = std::move(page); endResetModel(); }
const Record* RecordModel::record(int row) const { return row >= 0 && row < page_.rows.size() ? &page_.rows[row] : nullptr; }

Explorer::Explorer(Dataset dataset, QStringList headers, QWidget* parent)
    : QWidget(parent), dataset_(dataset) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    query_ = new QLineEdit;
    query_->setPlaceholderText(QStringLiteral("Filtrar resultados…"));
    layout->addWidget(query_);
    model_ = new RecordModel(std::move(headers), this);
    view_ = new QTableView;
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setAlternatingRowColors(false); view_->setShowGrid(false);
    view_->setWordWrap(false);
    view_->setFont(default_code_font());
    view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->verticalHeader()->hide();
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view_->verticalHeader()->setDefaultSectionSize(23);
    view_->setColumnWidth(0, 146); view_->setColumnWidth(1, 105);
    if (dataset == Dataset::Assembly) {
        view_->setItemDelegate(new AssemblyDelegate(model_, view_));
        view_->setColumnWidth(0, 108); view_->setColumnWidth(2, 74);
        view_->horizontalHeader()->setStretchLastSection(false);
        view_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        view_->setColumnHidden(1, true);
    }
    layout->addWidget(view_);
    navigation_ = new QWidget;
    auto* navigation = new QHBoxLayout(navigation_); navigation->setContentsMargins(5, 1, 5, 1);
    previous_ = new QToolButton; previous_->setObjectName(QStringLiteral("recordPreviousPage"));
    previous_->setIcon(workbench_icon(QStringLiteral("arrow-left"))); previous_->setToolTip(QStringLiteral("Página anterior"));
    next_ = new QToolButton; next_->setObjectName(QStringLiteral("recordNextPage"));
    next_->setIcon(workbench_icon(QStringLiteral("arrow-right"))); next_->setToolTip(QStringLiteral("Próxima página"));
    for (auto* button : {previous_, next_}) {
        button->setFixedSize(26, 24); button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly); button->setAccessibleName(button->toolTip());
    }
    count_ = new QLabel(QStringLiteral("0 registros"));
    count_->setAlignment(Qt::AlignCenter);
    navigation->addWidget(previous_);
    navigation->addWidget(count_, 1);
    navigation->addWidget(next_);
    layout->addWidget(navigation_);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(220);
    connect(query_, &QLineEdit::textChanged, timer_, [this] { timer_->start(); });
    connect(timer_, &QTimer::timeout, this, [this] { request(0); });
    connect(previous_, &QToolButton::clicked, this, [this] {
        request(model_->page().offset >= page_size ? model_->page().offset - page_size : 0);
    });
    connect(next_, &QToolButton::clicked, this, [this] { request(model_->page().offset + page_size); });
    connect(view_, &QTableView::doubleClicked, this, [this](QModelIndex index) {
        if (const auto* row = model_->record(index.row()); row && activated) activated(*row, index.column());
    });
    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& index) {
        if (const auto* row = model_->record(index.row()); row && selected && !restoring_selection_) selected(*row);
    });
    auto* follow = new QAction(this); follow->setShortcut(QKeySequence(Qt::Key_Return)); follow->setShortcutContext(Qt::WidgetShortcut);
    view_->addAction(follow); connect(follow, &QAction::triggered, this, [this] { activate_current(); });
    auto* find = new QAction(this); find->setShortcut(QKeySequence::Find); find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(find); connect(find, &QAction::triggered, this, [this] { query_->show(); query_->setFocus(); query_->selectAll(); });
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view_, &QTableView::customContextMenuRequested, this, [this](const QPoint& point) {
        const auto index = view_->indexAt(point); const auto* row = model_->record(index.row()); if (!row) return;
        QMenu menu(this);
        if (row->other_address) menu.addAction(QStringLiteral("Seguir destino · Enter"), this, [this] { activate_current(); });
        const auto text = row->cells.join("  ");
        menu.addAction(QStringLiteral("Copiar linha"), this, [text] { QApplication::clipboard()->setText(text); });
        if (dataset_ == Dataset::Assembly) {
            auto* bytes = menu.addAction(QStringLiteral("Mostrar bytes")); bytes->setCheckable(true); bytes->setChecked(!view_->isColumnHidden(1));
            connect(bytes, &QAction::toggled, this, [this](bool checked) { view_->setColumnHidden(1, !checked); });
        }
        menu.exec(view_->viewport()->mapToGlobal(point));
    });
}
void Explorer::set_compact(bool compact) {
    compact_ = compact;
    if (dataset_ == Dataset::Assembly) query_->setVisible(!compact);
    navigation_->setVisible(!compact || model_->page().total > page_size);
}
void Explorer::activate_current() {
    const auto index = view_->currentIndex();
    if (const auto* row = model_->record(index.row()); row && activated) activated(*row, index.column());
}
void Explorer::select_address(Address address) {
    for (int row = 0; row < model_->rowCount(); ++row) if (model_->record(row)->address == address) {
        const QScopedValueRollback restoring(restoring_selection_, true);
        view_->setCurrentIndex(model_->index(row, 0)); view_->scrollTo(model_->index(row, 0)); pending_address_.reset(); return;
    }
    pending_address_ = address;
    if (dataset_ == Dataset::Assembly && snapshot_ && function_ && *function_ < snapshot_->functions.size()) {
        const auto& instructions = snapshot_->functions[*function_].instructions;
        for (qsizetype index = 0; index < instructions.size(); ++index) if (instructions[index].address == address) {
            if (!query_->text().isEmpty()) { query_->clear(); timer_->stop(); }
            request(static_cast<quint64>(index) / page_size * page_size); return;
        }
    }
}
Explorer::~Explorer() { stop_.request_stop(); }
void Explorer::set_snapshot(std::shared_ptr<const AnalysisSnapshot> snapshot, std::optional<qsizetype> function) {
    snapshot_ = std::move(snapshot);
    function_ = function;
    pending_address_.reset();
    if (dataset_ == Dataset::Assembly) { query_->clear(); timer_->stop(); }
    request(0);
}
void Explorer::refresh() { request(model_->page().offset); }
void Explorer::request(quint64 offset) {
    stop_.request_stop();
    stop_ = std::stop_source{};
    const auto token = ++token_;
    previous_->setEnabled(false);
    next_->setEnabled(false);
    // Rows from the previous snapshot must not remain clickable while a new
    // request is pending, even though its eventual result has a token guard.
    model_->set_page({});
    if (!snapshot_) { model_->set_page({}); count_->setText(QStringLiteral("0 registros")); return; }
    count_->setText(QStringLiteral("Carregando…"));
    auto* watcher = new QFutureWatcher<Page>(this);
    connect(watcher, &QFutureWatcher<Page>::finished, this, [this, watcher, token] {
        const auto page = watcher->result();
        watcher->deleteLater();
        if (token != token_) return;
        model_->set_page(page);
        navigation_->setVisible(!compact_ || page.total > page_size);
        if (pending_address_) {
            const auto address = *pending_address_; pending_address_.reset();
            for (int row = 0; row < model_->rowCount(); ++row) if (model_->record(row)->address == address) {
                const QScopedValueRollback restoring(restoring_selection_, true);
                view_->setCurrentIndex(model_->index(row, 0)); view_->scrollTo(model_->index(row, 0)); break;
            }
        }
        count_->setText(page.error.isEmpty() ? (page.rows.isEmpty() ? QStringLiteral("0 registros") :
            QStringLiteral("%1–%2 / %3").arg(page.offset + 1).arg(page.offset + static_cast<quint64>(page.rows.size())).arg(page.total)) : page.error);
        previous_->setEnabled(page.offset > 0);
        next_->setEnabled(page.offset + static_cast<quint64>(page.rows.size()) < page.total && !page.rows.isEmpty());
    });
    watcher->setFuture(QtConcurrent::run([snapshot = snapshot_, dataset = dataset_, selected = function_, offset,
                                          query = query_->text(), stop = stop_.get_token()] {
        try { return build_page(*snapshot, dataset, selected, offset, query, stop); }
        catch (const std::exception& error) { Page result; result.error = QString::fromUtf8(error.what()); return result; }
        catch (...) { Page result; result.error = QStringLiteral("Não foi possível ler os registros."); return result; }
    }));
}
} // namespace reworkbench::ui
