#include "code_editor.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPlainTextDocumentLayout>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QStyle>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace reworkbench::ui {
namespace {
constexpr int maximum_find_highlights = 1000;

class CodeDocumentLayout final : public QPlainTextDocumentLayout {
public:
    explicit CodeDocumentLayout(QTextDocument* document) : QPlainTextDocumentLayout(document) {}
    QRectF blockBoundingRect(const QTextBlock& block) const override {
        auto bounds = QPlainTextDocumentLayout::blockBoundingRect(block);
        if (!block.isValid() || !block.isVisible()) return bounds;
        const auto font = document()->defaultFont();
        const qreal dpi = paintDevice() ? paintDevice()->logicalDpiY() : 96;
        const qreal pixels = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * dpi / 72;
        // VS Code's Windows editor uses round(fontSize * 1.35). Keep native glyph
        // metrics when a user-selected font needs more room. No document formats change.
        const qreal desired = qRound(pixels * 1.35);
        qreal top = 0;
        auto* layout = block.layout();
        for (int index = 0; index < layout->lineCount(); ++index) {
            auto line = layout->lineAt(index);
            const qreal height = std::max(desired, line.height());
            // Absolute positions make repeated bounds/paint calls idempotent.
            line.setPosition(QPointF(line.x(), top + (height - line.height()) / 2));
            top += height;
        }
        bounds.setHeight(top + (block.next().isValid() ? 0 : document()->documentMargin()));
        return bounds;
    }
};
class Gutter final : public QWidget {
    CodeEditor* owner_;
public:
    explicit Gutter(CodeEditor* owner) : QWidget(owner), owner_(owner) {}
    void paintEvent(QPaintEvent* event) override { owner_->paint_gutter(event); }
};
}
CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent), gutter_(new Gutter(this)) {
    auto* code_document = new QTextDocument(this);
    code_document->setDocumentLayout(new CodeDocumentLayout(code_document));
    setDocument(code_document);
    setReadOnly(true); setLineWrapMode(QPlainTextEdit::NoWrap);
    // Qt's read-only default enables mouse selection only. Keep caret navigation
    // and Shift+arrow selection available without granting text editing.
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFont(default_code_font());
    document()->setDocumentMargin(12);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    auto colors = palette();
    colors.setColor(QPalette::Base, theme_color("editor.background"));
    colors.setColor(QPalette::Text, theme_color("editor.foreground"));
    colors.setColor(QPalette::Highlight, theme_color("editor.selectionBackground"));
    setPalette(colors);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { update_margin(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& area, int dy) {
        if (dy) gutter_->scroll(0, dy);
        else gutter_->update(0, area.y(), gutter_->width(), area.height());
        if (area.contains(viewport()->rect())) update_margin();
    });
    create_find_overlay();
    observe_document();
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        observe_document();
        refresh_find(find_overlay_->isVisible());
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        update_current_line();
        update_find_count();
        if (!selecting_address_ && address_selected)
            if (const auto address = current_address()) address_selected(*address);
    });
    update_margin();
    update_current_line();
}
void CodeEditor::observe_document() {
    if (observed_document_ == document()) return;
    disconnect(source_document_connection_);
    observed_document_ = document();
    source_addresses_.clear();
    source_revision_ = -1;
    source_document_connection_ = connect(document(), &QTextDocument::contentsChange, this,
        [this](int, int removed, int added) {
            if (!removed && !added) return;
            source_addresses_.clear();
            source_revision_ = -1;
            find_matches_.clear();
        });
}
void CodeEditor::set_source_addresses(const QMap<int, reworkbench::Address>& addresses) {
    observe_document();
    source_addresses_.clear();
    for (auto entry = addresses.cbegin(); entry != addresses.cend(); ++entry)
        if (entry.key() >= 0 && entry.key() < blockCount()) source_addresses_.insert(entry.key(), entry.value());
    source_revision_ = document()->revision();
}
std::optional<reworkbench::Address> CodeEditor::current_address() const {
    if (observed_document_ != document() || source_revision_ != document()->revision()) return std::nullopt;
    const auto found = source_addresses_.constFind(textCursor().blockNumber());
    if (found == source_addresses_.cend()) return std::nullopt;
    return *found;
}
void CodeEditor::select_address(reworkbench::Address address) {
    if (observed_document_ != document() || source_revision_ != document()->revision()) return;
    for (auto entry = source_addresses_.cbegin(); entry != source_addresses_.cend(); ++entry) {
        if (entry.value() != address) continue;
        const auto block = document()->findBlockByNumber(entry.key());
        if (!block.isValid()) return;
        const QScopedValueRollback<bool> selecting(selecting_address_, true);
        setTextCursor(QTextCursor(block));
        ensureCursorVisible();
        return;
    }
}
void CodeEditor::create_find_overlay() {
    auto* overlay = new QFrame(viewport());
    find_overlay_ = overlay;
    overlay->setObjectName(QStringLiteral("editorFindOverlay"));
    overlay->setFont(interface_font());
    overlay->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(overlay);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(3);
    auto* query_row = new QHBoxLayout;
    query_row->setSpacing(3);
    find_query_ = new QLineEdit(overlay);
    find_query_->setObjectName(QStringLiteral("editorFindQuery"));
    find_query_->setPlaceholderText(QStringLiteral("Buscar no editor"));
    find_query_->setAccessibleName(QStringLiteral("Buscar no editor"));
    find_query_->setMinimumWidth(0);
    find_query_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    find_query_->setFixedHeight(26);
    query_row->addWidget(find_query_, 1);
    const auto button = [overlay](const QString& name, const QString& tooltip, const QString& icon) {
        auto* result = new QToolButton(overlay);
        result->setObjectName(name);
        result->setAccessibleName(tooltip);
        result->setToolTip(tooltip);
        result->setFixedSize(25, 25);
        result->setIconSize(QSize(16, 16));
        result->setFocusPolicy(Qt::StrongFocus);
        if (!icon.isEmpty()) result->setIcon(workbench_icon(icon));
        return result;
    };
    find_case_ = button(QStringLiteral("editorFindCaseSensitive"),
        QStringLiteral("Diferenciar maiúsculas de minúsculas"), {});
    find_case_->setText(QStringLiteral("Aa"));
    find_case_->setCheckable(true);
    query_row->addWidget(find_case_);
    auto* close = button(QStringLiteral("editorFindClose"), QStringLiteral("Fechar busca (Esc)"), QStringLiteral("close"));
    query_row->addWidget(close);
    layout->addLayout(query_row);
    auto* status_row = new QHBoxLayout;
    status_row->setSpacing(3);
    find_count_ = new QLabel(overlay);
    find_count_->setObjectName(QStringLiteral("editorFindCount"));
    find_count_->setAccessibleName(QStringLiteral("Resultados da busca"));
    find_count_->setMinimumWidth(0);
    find_count_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_row->addWidget(find_count_, 1);
    find_previous_ = button(QStringLiteral("editorFindPrevious"), QStringLiteral("Resultado anterior (Shift+F3)"), QStringLiteral("arrow-left"));
    find_next_ = button(QStringLiteral("editorFindNext"), QStringLiteral("Próximo resultado (F3)"), QStringLiteral("arrow-right"));
    status_row->addWidget(find_previous_);
    status_row->addWidget(find_next_);
    layout->addLayout(status_row);
    overlay->setStyleSheet(themed_stylesheet(QStringLiteral(R"(
        QFrame#editorFindOverlay { background: @{editorWidget.background}; border: 1px solid @{widget.border}; border-radius: @{radius.input}; }
        QLineEdit#editorFindQuery { background: @{input.background}; color: @{input.foreground}; border: 1px solid @{input.border}; border-radius: @{radius.input}; padding: 0 5px; }
        QLineEdit#editorFindQuery:focus { border-color: @{focusBorder}; }
        QLineEdit#editorFindQuery[noMatch="true"] { border-color: @{errorForeground}; }
        QLabel#editorFindCount { background: transparent; color: @{descriptionForeground}; border: none; }
        QLabel#editorFindCount[noMatch="true"] { color: @{errorForeground}; }
        QToolButton { background: transparent; color: @{icon.foreground}; border: 1px solid transparent; border-radius: @{radius.control}; padding: 0; }
        QToolButton:hover { background: @{toolbar.hoverBackground}; }
        QToolButton:pressed, QToolButton:checked { background: @{toolbar.pressedBackground}; }
        QToolButton:checked, QToolButton:focus { border-color: @{focusBorder}; }
    )")));
    connect(find_query_, &QLineEdit::textChanged, this, [this] { refresh_find(true); });
    connect(find_query_, &QLineEdit::returnPressed, this, [this] { find_next(false); });
    connect(find_case_, &QToolButton::toggled, this, [this] { refresh_find(true); });
    connect(close, &QToolButton::clicked, this, [this] { close_find(); });
    connect(find_previous_, &QToolButton::clicked, this, [this] { find_next(true); });
    connect(find_next_, &QToolButton::clicked, this, [this] { find_next(false); });
    const auto shortcut = [this](const QKeySequence& sequence, auto action) {
        auto* result = new QShortcut(sequence, this);
        result->setContext(Qt::WidgetWithChildrenShortcut);
        connect(result, &QShortcut::activated, this, action);
        return result;
    };
    shortcut(QKeySequence::Find, [this] { open_find(); });
    shortcut(QKeySequence(Qt::Key_F3), [this] { find_next(false); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { find_next(true); });
    close_find_shortcut_ = shortcut(QKeySequence(Qt::Key_Escape), [this] { close_find(); });
    close_find_shortcut_->setEnabled(false);
    overlay->hide();
    update_find_count();
}
void CodeEditor::position_find_overlay() {
    if (!find_overlay_) return;
    const int width = std::min(420, std::max(0, viewport()->width() - 12));
    const int height = std::min(66, std::max(0, viewport()->height() - 12));
    find_overlay_->setGeometry(std::max(0, viewport()->width() - width - 6),
        std::min(6, std::max(0, viewport()->height() - height)), width, height);
    find_overlay_->raise();
}
void CodeEditor::open_find() {
    position_find_overlay();
    find_overlay_->show();
    close_find_shortcut_->setEnabled(true);
    refresh_find(false);
    find_query_->setFocus(Qt::ShortcutFocusReason);
    find_query_->selectAll();
}
void CodeEditor::close_find() {
    find_overlay_->hide();
    close_find_shortcut_->setEnabled(false);
    update_current_line();
    setFocus(Qt::ShortcutFocusReason);
}
void CodeEditor::refresh_find(bool select_match) {
    if (!find_query_ || updating_find_) return;
    const QScopedValueRollback<bool> updating(updating_find_, true);
    find_matches_.clear();
    find_matches_capped_ = false;
    const auto query = find_query_->text();
    if (!query.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (find_case_->isChecked()) flags |= QTextDocument::FindCaseSensitively;
        QTextCursor cursor(document());
        while (true) {
            cursor = document()->find(query, cursor, flags);
            if (cursor.isNull()) break;
            if (find_matches_.size() == maximum_find_highlights) {
                // Probe one extra result so an exact 1,000 stays an exact count.
                find_matches_capped_ = true;
                break;
            }
            find_matches_.append(cursor);
        }
        if (select_match) {
            auto result = document()->find(query, textCursor().selectionStart(), flags);
            if (result.isNull()) result = document()->find(query, 0, flags);
            if (!result.isNull()) {
                setTextCursor(result);
                ensureCursorVisible();
            } else {
                auto cursor = textCursor();
                cursor.clearSelection();
                setTextCursor(cursor);
            }
        }
    }
    update_current_line();
    update_find_count();
}
void CodeEditor::find_next(bool backwards) {
    if (find_query_->text().isEmpty()) { open_find(); return; }
    QTextDocument::FindFlags flags;
    if (find_case_->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (backwards) flags |= QTextDocument::FindBackward;
    const auto cursor = textCursor();
    const int start = backwards ? cursor.selectionStart() : cursor.selectionEnd();
    auto result = document()->find(find_query_->text(), start, flags);
    if (result.isNull())
        result = document()->find(find_query_->text(), backwards ? document()->characterCount() - 1 : 0, flags);
    if (result.isNull()) return;
    setTextCursor(result);
    ensureCursorVisible();
}
void CodeEditor::update_find_count() {
    if (!find_query_) return;
    const bool no_match = !find_query_->text().isEmpty() && find_matches_.isEmpty();
    find_previous_->setEnabled(!find_matches_.isEmpty());
    find_next_->setEnabled(!find_matches_.isEmpty());
    if (find_query_->text().isEmpty()) find_count_->clear();
    else if (no_match) find_count_->setText(QStringLiteral("Nenhum resultado"));
    else if (find_matches_capped_) find_count_->setText(QStringLiteral("%1+ resultados").arg(maximum_find_highlights));
    else {
        int current = 0;
        const auto cursor = textCursor();
        for (int index = 0; index < find_matches_.size(); ++index)
            if (cursor.selectionStart() == find_matches_[index].selectionStart()
                && cursor.selectionEnd() == find_matches_[index].selectionEnd()) { current = index + 1; break; }
        find_count_->setText(current ? QStringLiteral("%1 / %2").arg(current).arg(find_matches_.size())
            : QStringLiteral("%1 resultados").arg(find_matches_.size()));
    }
    find_count_->setToolTip(find_matches_capped_
        ? QStringLiteral("Mais de %1 resultados. Realce limitado; F3 percorre todos.").arg(maximum_find_highlights)
        : find_count_->text());
    for (auto* widget : QList<QWidget*>{find_query_, find_count_}) {
        if (widget->property("noMatch").toBool() == no_match) continue;
        widget->setProperty("noMatch", no_match);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}
int CodeEditor::gutter_width() const {
    int digits = 1;
    for (int count = qMax(1, blockCount()); count >= 10; count /= 10) ++digits;
    return 28 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}
void CodeEditor::update_current_line() {
    QTextEdit::ExtraSelection line;
    line.format.setBackground(theme_color("editor.lineHighlightBackground"));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    QList<QTextEdit::ExtraSelection> selections{line};
    if (find_overlay_ && !find_overlay_->isHidden()) {
        for (const auto& match : find_matches_) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = match;
            selection.format.setBackground(theme_color("editor.inactiveSelectionBackground"));
            selections.append(selection);
        }
    }
    setExtraSelections(selections);
    gutter_->update();
}
void CodeEditor::update_margin() {
    setViewportMargins(gutter_width(), 0, 0, 0);
    const auto area = contentsRect();
    gutter_->setGeometry(area.left(), area.top(), gutter_width(), area.height());
    position_find_overlay();
}
void CodeEditor::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
        update_margin(); gutter_->update(); viewport()->update();
    }
}
void CodeEditor::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);
    QPainter painter(viewport());
    painter.setClipRect(event->rect());
    painter.setPen(theme_color("editorIndentGuide.background"));
    const qreal space_width = QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' '));
    auto block = firstVisibleBlock();
    while (block.isValid()) {
        const auto bounds = blockBoundingGeometry(block).translated(contentOffset());
        if (bounds.top() > event->rect().bottom()) break;
        if (block.isVisible() && bounds.bottom() >= event->rect().top()) {
            const auto cursor = textCursor();
            const auto* layout = block.layout();
            qreal row_top = bounds.top();
            for (int index = 0; index < layout->lineCount(); ++index) {
                const auto line = layout->lineAt(index);
                const qreal native_top = bounds.top() + line.y();
                const qreal padding = native_top - row_top;
                const qreal row_bottom = native_top + line.height() + padding;
                const auto fill_padding = [&](qreal left, qreal right, const QColor& color) {
                    painter.fillRect(QRectF(left, row_top, right - left, padding), color);
                    painter.fillRect(QRectF(left, row_bottom - padding, right - left, padding), color);
                };
                if (block.blockNumber() == cursor.blockNumber())
                    fill_padding(0, viewport()->width(), theme_color("editor.lineHighlightBackground"));
                if (cursor.hasSelection()) {
                    const int start = std::max(cursor.selectionStart() - block.position(), line.textStart());
                    const int end = std::min(cursor.selectionEnd() - block.position(), line.textStart() + line.textLength() + 1);
                    if (end > start) {
                        const qreal left = bounds.left() + line.cursorToX(start);
                        const qreal right = end > line.textStart() + line.textLength() ? viewport()->width() : bounds.left() + line.cursorToX(end);
                        fill_padding(left, right, palette().color(QPalette::Highlight));
                    }
                }
                row_top = row_bottom;
            }
            int indentation = 0;
            const auto text = block.text();
            for (QChar character : text) {
                if (character == QLatin1Char(' ')) ++indentation;
                else if (character == QLatin1Char('\t')) indentation += 4 - indentation % 4;
                else break;
            }
            for (int column = 0; column < indentation; column += 4) {
                const qreal x = bounds.left() + document()->documentMargin() + column * space_width;
                painter.drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
            }
        }
        block = block.next();
    }
}
void CodeEditor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const auto area = contentsRect(); gutter_->setGeometry(area.left(), area.top(), gutter_width(), area.height());
    position_find_overlay();
}
void CodeEditor::paint_gutter(QPaintEvent* event) {
    QPainter painter(gutter_); painter.fillRect(event->rect(), theme_color("editor.background")); painter.setFont(font());
    auto block = firstVisibleBlock(); int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            const bool current = block.blockNumber() == textCursor().blockNumber();
            if (current) painter.fillRect(0, top, gutter_->width(), height, theme_color("editor.lineHighlightBackground"));
            painter.setPen(theme_color(current ? "editorLineNumber.activeForeground" : "editorLineNumber.foreground"));
            const qreal line_top = block.layout()->lineCount() ? block.layout()->lineAt(0).y() : 0;
            painter.drawText(QRectF(0, top + line_top, gutter_->width() - 13, fontMetrics().height()), Qt::AlignRight, QString::number(block.blockNumber() + 1));
        }
        top += height; block = block.next();
    }
}
}
