#include "hex_view.hpp"
#include "reworkbench/binary_image.hpp"

#include <QFont>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>

namespace reworkbench::ui {
namespace {
struct HexResult { QString text; QString error; quint64 size = 0; };
}
HexView::HexView(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(QStringLiteral("Offset no arquivo")));
    position_ = new QLineEdit(QStringLiteral("0x0"));
    position_->setMaximumWidth(170);
    bar->addWidget(position_);
    auto* previous = new QPushButton(QStringLiteral("Anterior"));
    auto* next = new QPushButton(QStringLiteral("Próximos 64 KiB"));
    bar->addWidget(previous);
    bar->addWidget(next);
    detail_ = new QLabel;
    detail_->setTextFormat(Qt::PlainText);
    bar->addWidget(detail_, 1);
    layout->addLayout(bar);
    text_ = new QPlainTextEdit;
    text_->setReadOnly(true);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setFont(QFont(QStringLiteral("Consolas"), 10));
    layout->addWidget(text_);
    connect(position_, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        auto text = position_->text().trimmed();
        const auto offset = text.toULongLong(&ok, text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 16 : 10);
        if (ok && !path_.isEmpty()) show_file(path_, offset);
        else detail_->setText(QStringLiteral("Offset inválido."));
    });
    connect(previous, &QPushButton::clicked, this, [this] { if (!path_.isEmpty()) show_file(path_, offset_ >= 65536 ? offset_ - 65536 : 0); });
    connect(next, &QPushButton::clicked, this, [this] { if (!path_.isEmpty() && offset_ <= std::numeric_limits<quint64>::max() - 65536) show_file(path_, offset_ + 65536); });
    clear();
}
void HexView::clear() {
    ++token_; path_.clear(); offset_ = 0;
    position_->setText(QStringLiteral("0x0"));
    detail_->setText(QStringLiteral("Nenhuma amostra selecionada"));
    detail_->setToolTip({});
    text_->clear();
}
void HexView::show_file(QString path, quint64 offset) {
    path_ = std::move(path); offset_ = offset;
    const auto token = ++token_;
    position_->setText(hex(offset));
    text_->clear();
    detail_->setToolTip(path_);
    detail_->setText(QFileInfo(path_).fileName() + QStringLiteral(" · lendo…"));
    auto* watcher = new QFutureWatcher<HexResult>(this);
    connect(watcher, &QFutureWatcher<HexResult>::finished, this, [this, watcher, token] {
        auto result = watcher->result(); watcher->deleteLater();
        if (token != token_) return;
        text_->setPlainText(result.text);
        detail_->setText(QFileInfo(path_).fileName() + QStringLiteral(" · ") + (result.error.isEmpty() ? QStringLiteral("%1 bytes · somente leitura").arg(result.size) : result.error));
    });
    watcher->setFuture(QtConcurrent::run([path = path_, offset] {
        HexResult result;
        try {
            BinaryFile file(path);
            if (offset > file.size()) throw std::runtime_error("Offset fora do arquivo.");
            const auto bytes = file.read(offset, std::min<quint64>(65536, file.size() - offset));
            result.size = static_cast<quint64>(bytes.size());
            QStringList lines;
            for (qsizetype index = 0; index < bytes.size(); index += 16) {
                const auto block = bytes.mid(index, 16);
                QString ascii;
                for (const auto character : block) {
                    const auto byte = static_cast<unsigned char>(character);
                    ascii += byte >= 32 && byte < 127 ? QChar::fromLatin1(static_cast<char>(byte)) : QChar::fromLatin1('.');
                }
                lines.push_back(QStringLiteral("%1  %2  │ %3").arg(offset + static_cast<quint64>(index), 16, 16, QLatin1Char('0'))
                    .arg(QString::fromLatin1(block.toHex(' ')).leftJustified(47, QLatin1Char(' ')), ascii));
            }
            result.text = lines.join(QLatin1Char('\n'));
        } catch (const std::exception& error) { result.error = QString::fromUtf8(error.what()); }
        catch (...) { result.error = QStringLiteral("Falha ao ler os bytes."); }
        return result;
    }));
}
} // namespace reworkbench::ui
