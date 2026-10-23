#pragma once
#include <QSyntaxHighlighter>
namespace reworkbench::ui {
class CodeHighlighter final : public QSyntaxHighlighter {
public:
    explicit CodeHighlighter(QTextDocument* document);
protected:
    void highlightBlock(const QString& text) override;
};
}
