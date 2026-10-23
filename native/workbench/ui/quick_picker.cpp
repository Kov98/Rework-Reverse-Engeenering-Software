#include "quick_picker.hpp"
#include "workspace_theme.hpp"

#include <QDialog>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QScreen>
#include <QShowEvent>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace reworkbench::ui {
namespace {

// Geometry reference: Microsoft's MIT-licensed quickInput.css and
// quickInputController.ts. This is an independent Qt implementation.
// https://github.com/microsoft/vscode/tree/main/src/vs/platform/quickinput/browser
constexpr int row_height = 22;
constexpr int description_role = Qt::UserRole + 1;
constexpr int shortcut_role = Qt::UserRole + 2;

QStringList shortcut_keys(const QString& shortcut) {
    // Native Windows shortcut strings use '+' between modifiers. Keep a final
    // '+' as a key (Ctrl++), rather than discarding it as an empty segment.
    QStringList keys = shortcut.split('+', Qt::SkipEmptyParts);
    if (shortcut.endsWith('+')) keys.append(QStringLiteral("+"));
    return keys;
}

class QuickPickerDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, row_height};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override {
        const bool enabled = index.flags().testFlag(Qt::ItemIsEnabled);
        const bool selected = enabled && option.state.testFlag(QStyle::State_Selected);
        const bool hovered = enabled && option.state.testFlag(QStyle::State_MouseOver);
        const auto foreground = theme_color(selected ? "quickInputList.focusForeground" : "quickInput.foreground");
        const auto muted = theme_color(enabled ? "descriptionForeground" : "disabledForeground");
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        if (selected || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(theme_color(selected ? "quickInputList.focusBackground" : "list.hoverBackground"));
            const auto radius = theme_metric("radius.item");
            painter->drawRoundedRect(QRectF(option.rect).adjusted(2, 0.5, -2, -0.5), radius, radius);
        }
        painter->setFont(option.font);
        QRect content = option.rect.adjusted(6, 0, -6, 0);
        const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (!icon.isNull()) {
            icon.paint(painter, QRect(content.left(), content.center().y() - 7, 16, 16),
                Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        }
        content.adjust(22, 0, 0, 0);

        const auto shortcut = index.data(shortcut_role).toString();
        if (!shortcut.isEmpty()) {
            QFont key_font = option.font;
            key_font.setPixelSize(theme_metric("typography.captionPixels"));
            const QFontMetrics key_metrics(key_font);
            const auto keys = shortcut_keys(shortcut);
            int keys_width = 0;
            for (const auto& key : keys) keys_width += key_metrics.horizontalAdvance(key) + 8;
            keys_width += std::max(0, int(keys.size()) - 1) * 2;
            // On very narrow windows, keep the result name readable first.
            if (keys_width < content.width() / 2) {
                painter->setFont(key_font);
                int x = content.right() - keys_width + 1;
                for (const auto& key : keys) {
                    const int width = key_metrics.horizontalAdvance(key) + 8;
                    const QRect key_rect(x, content.center().y() - 8, width, 17);
                    painter->setPen(theme_color("keybindingLabel.border"));
                    painter->setBrush(selected || hovered ? QBrush(Qt::NoBrush) : QBrush(theme_color("keybindingLabel.background")));
                    const auto radius = theme_metric("radius.control");
                    painter->drawRoundedRect(key_rect.adjusted(0, 0, -1, -1), radius, radius);
                    painter->setPen(theme_color("keybindingLabel.bottomBorder"));
                    painter->drawLine(key_rect.bottomLeft() + QPoint(2, -1), key_rect.bottomRight() + QPoint(-2, -1));
                    painter->setPen(enabled ? (selected ? foreground : theme_color("keybindingLabel.foreground")) : muted);
                    painter->drawText(key_rect.adjusted(3, 0, -3, -1), Qt::AlignCenter, key);
                    x += width + 2;
                }
                content.setRight(content.right() - keys_width - 12);
                painter->setFont(option.font);
            }
        }

        const QFontMetrics metrics(option.font);
        const auto label = index.data(Qt::DisplayRole).toString();
        const auto description = index.data(description_role).toString();
        int label_width = content.width();
        if (!description.isEmpty()) {
            const int desired_label = metrics.horizontalAdvance(label);
            const int desired_description = metrics.horizontalAdvance(description);
            label_width = std::min(desired_label, std::max(content.width() / 2,
                content.width() - std::min(desired_description, content.width() / 3) - 10));
        }
        label_width = std::max(0, label_width);
        painter->setPen(enabled ? foreground : muted);
        painter->drawText(QRect(content.left(), content.top(), label_width, content.height()),
            Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(label, Qt::ElideRight, label_width));
        if (!description.isEmpty()) {
            const QRect detail = content.adjusted(label_width + 10, 0, 0, 0);
            painter->setPen(selected ? foreground : muted);
            painter->drawText(detail, Qt::AlignVCenter | Qt::AlignLeft,
                metrics.elidedText(description, Qt::ElideRight, std::max(0, detail.width())));
        }
        painter->restore();
    }
};

class QuickPickerDialog final : public QDialog {
public:
    QuickPickerDialog(QWidget* parent, bool commands, const QVector<QuickPickerEntry>& entries,
        QString empty_message)
        : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), entries_(entries),
          commands_(commands), empty_message_(std::move(empty_message)) {
        setObjectName(commands ? "commandPalette" : "quickOpen");
        setWindowTitle(commands ? QStringLiteral("Paleta de comandos") : QStringLiteral("Ir para função"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setFont(interface_font());
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 7);
        layout->setSpacing(4);
        query_ = new QLineEdit(this);
        query_->setObjectName("quickPickerQuery");
        query_->setFixedHeight(32);
        query_->setPlaceholderText(commands ? QStringLiteral("> Digite um comando")
            : QStringLiteral("Buscar função por nome ou endereço…"));
        query_->setAccessibleName(commands ? QStringLiteral("Buscar comandos") : QStringLiteral("Buscar funções"));
        query_->installEventFilter(this);
        layout->addWidget(query_);
        list_ = new QListWidget(this);
        list_->setObjectName("quickPickerResults");
        list_->setAccessibleName(QStringLiteral("Resultados"));
        list_->setFrameShape(QFrame::NoFrame);
        list_->setItemDelegate(new QuickPickerDelegate(list_));
        list_->setUniformItemSizes(true);
        list_->setSpacing(0);
        list_->setFocusPolicy(Qt::NoFocus);
        list_->setMouseTracking(true);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list_->installEventFilter(this);
        list_->viewport()->installEventFilter(this);
        layout->addWidget(list_);
        // The delegate owns row geometry and drawing; neutralize generic list
        // item padding without relying on application-wide stylesheet order.
        setStyleSheet(themed_stylesheet(QStringLiteral(R"(
            QDialog#quickOpen, QDialog#commandPalette { background: transparent; border: none; }
            QLineEdit#quickPickerQuery { padding: 0 6px; border: 1px solid @{input.border}; border-radius: @{radius.input}; }
            QLineEdit#quickPickerQuery:focus { border-color: @{focusBorder}; }
            QListWidget#quickPickerResults { border: none; padding: 0; background: transparent; outline: none; }
            QListWidget#quickPickerResults::item { padding: 0; margin: 0; border: none; background: transparent; }
        )")));
        // The popup owns its alpha surface. A QSS-painted QDialog background
        // would otherwise leave opaque square corners outside the rounded path.
        setAttribute(Qt::WA_StyledBackground, false);
        connect(query_, &QLineEdit::textChanged, this, [this](const QString& text) { filter(text); });
        connect(query_, &QLineEdit::returnPressed, this, [this] { activate(); });
        const auto activate_item = [this](QListWidgetItem* item) {
            if (!item || !selectable(list_->row(item))) return;
            list_->setCurrentItem(item);
            activate();
        };
        connect(list_, &QListWidget::itemClicked, this, activate_item);
        connect(list_, &QListWidget::itemActivated, this, activate_item);
        if (parent) parent->installEventFilter(this);
        filter({});
    }

    const std::optional<QVariant>& choice() const { return choice_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(theme_color("widget.border"), 1));
        painter.setBrush(theme_color("quickInput.background"));
        const auto radius = theme_metric("radius.popup");
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        resize_to_results();
        query_->setFocus(Qt::PopupFocusReason);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == parentWidget() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
            resize_to_results();
        }
        if (event->type() != QEvent::KeyPress || (watched != query_ && watched != list_ && watched != list_->viewport()))
            return QDialog::eventFilter(watched, event);
        const auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Escape: reject(); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: activate(); return true;
        default: break;
        }
        if (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
            return QDialog::eventFilter(watched, event);
        switch (key->key()) {
        case Qt::Key_Up: step(-1, true); return true;
        case Qt::Key_Down: step(1, true); return true;
        case Qt::Key_Home:
            if (watched == query_) return QDialog::eventFilter(watched, event);
            select_edge(false); return true;
        case Qt::Key_End:
            if (watched == query_) return QDialog::eventFilter(watched, event);
            select_edge(true); return true;
        case Qt::Key_PageUp: step(-std::max(1, list_->viewport()->height() / row_height), false); return true;
        case Qt::Key_PageDown: step(std::max(1, list_->viewport()->height() / row_height), false); return true;
        default: return QDialog::eventFilter(watched, event);
        }
    }

private:
    bool selectable(int row) const {
        const auto* item = list_->item(row);
        return item && item->flags().testFlag(Qt::ItemIsEnabled) && item->flags().testFlag(Qt::ItemIsSelectable);
    }

    void select_row(int row) {
        list_->setCurrentRow(row);
        if (auto* item = list_->currentItem()) list_->scrollToItem(item);
        query_->setFocus(Qt::OtherFocusReason);
    }

    void select_edge(bool last) {
        for (int row = last ? list_->count() - 1 : 0; row >= 0 && row < list_->count(); row += last ? -1 : 1) {
            if (selectable(row)) { select_row(row); return; }
        }
    }

    void step(int distance, bool wrap) {
        const int count = list_->count();
        if (!count) return;
        const int direction = distance < 0 ? -1 : 1;
        int row = list_->currentRow();
        if (row < 0) { select_edge(direction < 0); return; }
        row += distance;
        if (wrap) row = (row % count + count) % count;
        else row = std::clamp(row, 0, count - 1);
        for (int attempt = 0; attempt < count; ++attempt) {
            if (selectable(row)) { select_row(row); return; }
            row += direction;
            if (wrap) row = (row % count + count) % count;
            else if (row < 0 || row >= count) {
                // A disabled edge should still reach the nearest enabled row.
                select_edge(direction > 0);
                return;
            }
        }
    }

    void activate() {
        if (!selectable(list_->currentRow())) return;
        choice_ = list_->currentItem()->data(Qt::UserRole);
        accept();
    }

    void filter(QString text) {
        text = text.trimmed();
        if (commands_ && text.startsWith('>')) text = text.mid(1).trimmed();
        const auto terms = text.split(' ', Qt::SkipEmptyParts);
        list_->clear();
        for (const auto& entry : entries_) {
            const QString searchable = entry.label + ' ' + entry.description;
            if (!std::all_of(terms.cbegin(), terms.cend(), [&searchable](const QString& term) {
                return searchable.contains(term, Qt::CaseInsensitive);
            })) continue;
            auto* item = new QListWidgetItem(entry.icon, entry.label, list_);
            item->setData(Qt::UserRole, entry.payload);
            item->setData(description_role, entry.description);
            item->setData(shortcut_role, entry.shortcut);
            item->setToolTip(entry.description.isEmpty() ? entry.label : entry.label + '\n' + entry.description);
            item->setData(Qt::AccessibleTextRole, entry.label + ' ' + entry.description + ' ' + entry.shortcut);
            if (!entry.enabled) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            if (!commands_ && list_->count() >= 200) break;
        }
        if (!list_->count()) {
            auto* empty = new QListWidgetItem(entries_.isEmpty() && !empty_message_.isEmpty()
                ? empty_message_ : commands_ ? QStringLiteral("Nenhum comando encontrado")
                : QStringLiteral("Nenhuma função encontrada"), list_);
            empty->setFlags(Qt::NoItemFlags);
        }
        select_edge(false);
        resize_to_results();
    }

    void resize_to_results() {
        const auto* owner = parentWidget();
        if (!owner) return;
        const auto* screen = owner->screen();
        const QRect available = screen ? screen->availableGeometry() : QRect(owner->mapToGlobal(QPoint(0, 0)), owner->size());
        const int width_limit = std::max(1, std::min(owner->width() - 24, available.width() - 24));
        const int popup_width = std::min(width_limit, std::min(600, qRound(owner->width() * 0.62)));
        const int list_limit = std::max(row_height, std::min(351, qRound(owner->height() * 0.4)));
        const int visible_rows = std::max(1, list_limit / row_height);
        const int list_height = std::min(std::max(1, list_->count()), visible_rows) * row_height;
        list_->setFixedHeight(list_height);
        setFixedSize(std::max(1, popup_width), 6 + 32 + 4 + list_height + 7);
        QPoint top_left = owner->mapToGlobal(QPoint((owner->width() - width()) / 2, 36));
        top_left.setX(std::clamp(top_left.x(), available.left() + 6, std::max(available.left() + 6, available.right() - width() - 5)));
        top_left.setY(std::clamp(top_left.y(), available.top() + 6, std::max(available.top() + 6, available.bottom() - height() - 5)));
        move(top_left);
    }

    const QVector<QuickPickerEntry>& entries_;
    bool commands_;
    QString empty_message_;
    QLineEdit* query_ = nullptr;
    QListWidget* list_ = nullptr;
    std::optional<QVariant> choice_;
};

} // namespace

std::optional<QVariant> choose_quick_picker(QWidget* parent, bool commands,
    const QVector<QuickPickerEntry>& entries, const QString& empty_message) {
    QuickPickerDialog dialog(parent, commands, entries, empty_message);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.choice();
}

} // namespace reworkbench::ui
