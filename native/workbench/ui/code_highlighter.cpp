#include "code_highlighter.hpp"
#include "workspace_theme.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QTextCharFormat>
#include <QVector>

namespace reworkbench::ui {
namespace {
enum BlockState { Normal, BlockComment, ContinuedString, ContinuedCharacter };
enum class TokenKind { Identifier, Type, Keyword, Control, Number, Operator };
struct Token {
    int start;
    int length;
    QString text;
    TokenKind kind;
};
const QSet<QString> control_words = {
    QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("switch"), QStringLiteral("case"),
    QStringLiteral("default"), QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("for"),
    QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("goto"), QStringLiteral("return"),
    QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("throw"), QStringLiteral("co_return")
};
const QSet<QString> keywords = {
    QStringLiteral("const"), QStringLiteral("volatile"), QStringLiteral("static"), QStringLiteral("extern"),
    QStringLiteral("inline"), QStringLiteral("constexpr"), QStringLiteral("consteval"), QStringLiteral("constinit"),
    QStringLiteral("struct"), QStringLiteral("class"), QStringLiteral("union"), QStringLiteral("enum"),
    QStringLiteral("typedef"), QStringLiteral("using"), QStringLiteral("namespace"), QStringLiteral("template"),
    QStringLiteral("typename"), QStringLiteral("public"), QStringLiteral("protected"), QStringLiteral("private"),
    QStringLiteral("virtual"), QStringLiteral("override"), QStringLiteral("final"), QStringLiteral("noexcept"),
    QStringLiteral("sizeof"), QStringLiteral("alignof"), QStringLiteral("decltype"), QStringLiteral("new"),
    QStringLiteral("delete"), QStringLiteral("nullptr"), QStringLiteral("NULL"), QStringLiteral("true"),
    QStringLiteral("false"), QStringLiteral("this"), QStringLiteral("thread_local"), QStringLiteral("restrict"),
    QStringLiteral("__restrict"), QStringLiteral("__stdcall"), QStringLiteral("__fastcall"), QStringLiteral("__cdecl"),
    QStringLiteral("include"), QStringLiteral("define"), QStringLiteral("undef"), QStringLiteral("ifdef"),
    QStringLiteral("ifndef"), QStringLiteral("endif"), QStringLiteral("elif"), QStringLiteral("pragma")
};
const QSet<QString> primitive_types = {
    QStringLiteral("void"), QStringLiteral("bool"), QStringLiteral("char"), QStringLiteral("wchar_t"),
    QStringLiteral("char8_t"), QStringLiteral("char16_t"), QStringLiteral("char32_t"), QStringLiteral("short"),
    QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("float"), QStringLiteral("double"),
    QStringLiteral("signed"), QStringLiteral("unsigned"), QStringLiteral("auto")
};
const QSet<QString> types = {
    QStringLiteral("size_t"),
    QStringLiteral("ssize_t"), QStringLiteral("ptrdiff_t"), QStringLiteral("intptr_t"), QStringLiteral("uintptr_t"),
    QStringLiteral("machine_state"), QStringLiteral("memory_state"), QStringLiteral("architecture_state"),
    QStringLiteral("opaque_state"), QStringLiteral("state"), QStringLiteral("undefined"), QStringLiteral("undefined1"),
    QStringLiteral("undefined2"), QStringLiteral("undefined4"), QStringLiteral("undefined8")
};
bool is_type(const QString& word) {
    static const QRegularExpression sized_type(QStringLiteral("^(?:u?int(?:8|16|32|64|128)_t|[ui](?:1|8|16|32|64|128)|__int(?:8|16|32|64|128))$"));
    return primitive_types.contains(word) || types.contains(word) || sized_type.match(word).hasMatch();
}
bool identifier_start(QChar character) { return character.isLetter() || character == QLatin1Char('_'); }
bool identifier_part(QChar character) { return character.isLetterOrNumber() || character == QLatin1Char('_'); }
}

CodeHighlighter::CodeHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

void CodeHighlighter::highlightBlock(const QString& text) {
    // Lex literals and comments first. Their contents never reach the code classifier.
    const auto color = [this](int start, int length, const char* token) { setFormat(start, length, theme_color(token)); };
    const int length = static_cast<int>(text.size());
    QVector<Token> tokens;
    int position = 0;
    setCurrentBlockState(Normal);
    const auto quoted = [&](int start, int content, QChar quote) {
        int end = content;
        while (end < length) {
            if (text[end] == QLatin1Char('\\')) {
                if (end + 1 == length) {
                    setCurrentBlockState(quote == QLatin1Char('"') ? ContinuedString : ContinuedCharacter);
                    ++end;
                    break;
                }
                end += 2;
            } else if (text[end++] == quote) {
                break;
            }
        }
        color(start, end - start, "syntax.string");
        return end;
    };
    if (previousBlockState() == BlockComment) {
        const int end = static_cast<int>(text.indexOf(QStringLiteral("*/")));
        if (end < 0) {
            color(0, length, "syntax.comment");
            setCurrentBlockState(BlockComment);
            return;
        }
        color(0, end + 2, "syntax.comment");
        position = end + 2;
    } else if (previousBlockState() == ContinuedString || previousBlockState() == ContinuedCharacter) {
        position = quoted(0, 0, previousBlockState() == ContinuedString ? QLatin1Char('"') : QLatin1Char('\''));
    }
    static const QRegularExpression number(QStringLiteral(
        "(?:0[xX][0-9a-fA-F']+(?:\\.[0-9a-fA-F']*)?(?:[pP][+-]?[0-9']+)?|"
        "0[bB][01']+|(?:[0-9][0-9']*(?:\\.[0-9']*)?|\\.[0-9][0-9']*)(?:[eE][+-]?[0-9']+)?)[uUlLfF]*"));
    while (position < length) {
        const int start = position;
        const QChar character = text[position];
        if (character.isSpace()) { ++position; continue; }
        if (character == QLatin1Char('/') && position + 1 < length) {
            if (text[position + 1] == QLatin1Char('/')) {
                color(position, length - position, "syntax.comment");
                break;
            }
            if (text[position + 1] == QLatin1Char('*')) {
                const int end = static_cast<int>(text.indexOf(QStringLiteral("*/"), position + 2));
                if (end < 0) {
                    color(position, length - position, "syntax.comment");
                    setCurrentBlockState(BlockComment);
                    break;
                }
                position = end + 2;
                color(start, position - start, "syntax.comment");
                continue;
            }
        }
        if (character == QLatin1Char('"') || character == QLatin1Char('\'')) {
            position = quoted(start, start + 1, character);
            continue;
        }
        if (identifier_start(character)) {
            while (position < length && identifier_part(text[position])) ++position;
            const auto word = text.mid(start, position - start);
            if (position < length && (text[position] == QLatin1Char('"') || text[position] == QLatin1Char('\'')) &&
                (word == QStringLiteral("L") || word == QStringLiteral("u") || word == QStringLiteral("U") || word == QStringLiteral("u8"))) {
                position = quoted(start, position + 1, text[position]);
                continue;
            }
            TokenKind kind = TokenKind::Identifier;
            const char* token = "syntax.variable";
            if (control_words.contains(word)) { kind = TokenKind::Control; token = "syntax.control"; }
            else if (is_type(word)) {
                kind = TokenKind::Type;
                token = primitive_types.contains(word) ? "syntax.keyword" : "syntax.type";
            }
            else if (keywords.contains(word)) { kind = TokenKind::Keyword; token = "syntax.keyword"; }
            color(start, position - start, token);
            tokens.push_back({start, position - start, word, kind});
            continue;
        }
        if (character.isDigit() || (character == QLatin1Char('.') && position + 1 < length && text[position + 1].isDigit())) {
            const auto match = number.match(text, position, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            if (match.hasMatch()) {
                position += static_cast<int>(match.capturedLength());
                color(start, position - start, "syntax.number");
                tokens.push_back({start, position - start, text.mid(start, position - start), TokenKind::Number});
                continue;
            }
        }
        ++position;
        color(start, 1, "syntax.operator");
        tokens.push_back({start, 1, QString(character), TokenKind::Operator});
    }

    // Only recognizable declarations introduce type/parameter colors. This does not
    // claim recovered C/C++ semantics; unresolved names retain the identifier color.
    for (qsizetype index = 0; index < tokens.size(); ++index) {
        auto& token = tokens[index];
        if (token.kind != TokenKind::Identifier) continue;
        bool declared_namespace = false;
        for (qsizetype before = index - 1; before >= 0; --before) {
            if (tokens[before].text == QStringLiteral("namespace")) { declared_namespace = true; break; }
            if (tokens[before].text != QStringLiteral(":") && tokens[before].kind != TokenKind::Identifier && tokens[before].kind != TokenKind::Type) break;
        }
        if (declared_namespace || (index > 0 && (tokens[index - 1].text == QStringLiteral("struct") || tokens[index - 1].text == QStringLiteral("class") ||
                          tokens[index - 1].text == QStringLiteral("union") || tokens[index - 1].text == QStringLiteral("enum") ||
                          tokens[index - 1].text == QStringLiteral("using")))) {
            token.kind = TokenKind::Type;
            color(token.start, token.length, "syntax.type");
            continue;
        }
        if (index > 0 && tokens[index - 1].text == QStringLiteral("goto")) {
            color(token.start, token.length, "syntax.label");
            continue;
        }
        if (index + 1 >= tokens.size()) continue;
        if (tokens[index + 1].text == QStringLiteral("=")) {
            for (qsizetype before = index - 1; before >= 0; --before) {
                if (tokens[before].text == QStringLiteral("constexpr")) {
                    color(token.start, token.length, "syntax.constant");
                    break;
                }
                if (tokens[before].kind == TokenKind::Operator) break;
            }
        }
        if (index == 0 && tokens[index + 1].text == QStringLiteral(":") &&
            (index + 2 == tokens.size() || tokens[index + 2].text != QStringLiteral(":")))
            color(token.start, token.length, "syntax.label");
        if (tokens[index + 1].text != QStringLiteral("(")) continue;
        color(token.start, token.length, "syntax.function");

        qsizetype before = index - 1;
        while (before >= 0 && (tokens[before].text == QStringLiteral("*") || tokens[before].text == QStringLiteral("&"))) --before;
        if (before < 0 || tokens[before].kind != TokenKind::Type) continue;
        int depth = 1;
        bool parameter_type = false;
        qsizetype candidate = -1;
        for (qsizetype parameter = index + 2; parameter < tokens.size() && depth > 0; ++parameter) {
            const auto& part = tokens[parameter];
            if (part.text == QStringLiteral("(")) { ++depth; continue; }
            if (part.text == QStringLiteral(")")) --depth;
            if (depth > 1) continue;
            if (part.text == QStringLiteral(",") || depth == 0) {
                if (parameter_type && candidate >= 0) color(tokens[candidate].start, tokens[candidate].length, "syntax.parameter");
                parameter_type = false;
                candidate = -1;
            } else if (part.kind == TokenKind::Type) parameter_type = true;
            else if (part.kind == TokenKind::Identifier) candidate = parameter;
            else if (part.text == QStringLiteral("=")) {
                if (parameter_type && candidate >= 0) color(tokens[candidate].start, tokens[candidate].length, "syntax.parameter");
                parameter_type = false;
                candidate = -1;
            }
        }
    }
}
}
