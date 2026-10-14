#include "code_highlighter.hpp"
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QColor>
namespace reworkbench::ui {
CodeHighlighter::CodeHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
void CodeHighlighter::highlightBlock(const QString& text) {
    static const QRegularExpression keywords(QStringLiteral("\\b(if|else|while|do|for|goto|return|phi|opaque|input|load|store|true|false|void|bool|u8|u16|u32|u64|uint8_t|uint16_t|uint32_t|uint64_t)\\b"));
    static const QRegularExpression numbers(QStringLiteral("\\b(0x[0-9a-fA-F]+|[0-9]+)\\b"));
    const auto apply = [&](const QRegularExpression& expression, const QColor& color) {
        auto matches = expression.globalMatch(text); while (matches.hasNext()) { const auto match = matches.next(); setFormat(int(match.capturedStart()), int(match.capturedLength()), color); }
    };
    apply(keywords, QColor(QStringLiteral("#c6a0f6"))); apply(numbers, QColor(QStringLiteral("#eed49f")));
    const auto comment = text.indexOf(QStringLiteral("//"));
    if (comment >= 0) setFormat(int(comment), int(text.size() - comment), QColor(QStringLiteral("#8195a9")));
}
}
