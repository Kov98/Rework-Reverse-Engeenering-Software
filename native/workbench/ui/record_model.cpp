#include "record_model.hpp"
#include "reworkbench/binary_image.hpp"

#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace reworkbench::ui {
namespace {
constexpr quint64 page_size = 200;

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
                    reference.source_function.value_or(reference.source), reference.target, {}, -1});
        }
        break;
    case Dataset::Assembly:
        if (selected && *selected >= 0 && *selected < snapshot.functions.size())
            for (const auto& instruction : snapshot.functions[*selected].instructions)
                accept({{instruction.address.text(), QString::fromLatin1(instruction.bytes.toHex(' ')), instruction.mnemonic,
                         instruction.operands_text}, instruction.address, instruction.direct_target,
                        reworkbench::file_offset(snapshot.metadata, instruction.address, static_cast<quint64>(instruction.bytes.size())), *selected});
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
    layout->setContentsMargins(5, 5, 5, 5);
    query_ = new QLineEdit;
    query_->setPlaceholderText(QStringLiteral("Filtrar resultados…"));
    layout->addWidget(query_);
    model_ = new RecordModel(std::move(headers), this);
    view_ = new QTableView;
    view_->setModel(model_);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setAlternatingRowColors(true);
    view_->setWordWrap(false);
    view_->setFont(QFont(QStringLiteral("Consolas"), 10));
    view_->verticalHeader()->hide();
    view_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(view_);
    auto* navigation = new QHBoxLayout;
    previous_ = new QPushButton(QStringLiteral("Anterior"));
    next_ = new QPushButton(QStringLiteral("Próximos"));
    count_ = new QLabel(QStringLiteral("0 registros"));
    count_->setAlignment(Qt::AlignCenter);
    navigation->addWidget(previous_);
    navigation->addWidget(count_, 1);
    navigation->addWidget(next_);
    layout->addLayout(navigation);
    previous_->setEnabled(false);
    next_->setEnabled(false);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(220);
    connect(query_, &QLineEdit::textChanged, timer_, [this] { timer_->start(); });
    connect(timer_, &QTimer::timeout, this, [this] { request(0); });
    connect(previous_, &QPushButton::clicked, this, [this] {
        request(model_->page().offset >= page_size ? model_->page().offset - page_size : 0);
    });
    connect(next_, &QPushButton::clicked, this, [this] { request(model_->page().offset + page_size); });
    connect(view_, &QTableView::doubleClicked, this, [this](QModelIndex index) {
        if (const auto* row = model_->record(index.row()); row && activated) activated(*row, index.column());
    });
}
Explorer::~Explorer() { stop_.request_stop(); }
void Explorer::set_snapshot(std::shared_ptr<const AnalysisSnapshot> snapshot, std::optional<qsizetype> function) {
    snapshot_ = std::move(snapshot);
    function_ = function;
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
