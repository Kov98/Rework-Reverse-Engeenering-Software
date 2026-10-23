#include "import_dialog.hpp"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace reworkbench::ui {
Address parse_address(QString text) {
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("ram:"))) text.remove(0, 4);
    static const QRegularExpression expression(QStringLiteral("^(?:0[xX])?[0-9a-fA-F]{1,16}$"));
    if (!expression.match(text).hasMatch()) throw std::invalid_argument("Use um endereço hexadecimal de até64 bits no espaço ram.");
    bool valid = false;
    const auto value = text.toULongLong(&valid, 16);
    if (!valid) throw std::invalid_argument("Endereço fora da faixa de64 bits.");
    return {QStringLiteral("ram"), value};
}
ImportDialog::ImportDialog(const QString& path, const Probe& probe, QWidget* parent)
    : QDialog(parent), probe_(probe) {
    setWindowTitle(QStringLiteral("Importação nativa"));
    resize(660, 460);
    auto* layout = new QVBoxLayout(this);
    auto* source = new QLabel(path);
    source->setWordWrap(true);
    source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(source);
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Formato identificado"), new QLabel(format_name(probe.format)));
    slices_ = new QComboBox;
    slices_->addItem(QStringLiteral("Selecione uma imagem…"), -1);
    for (const auto& slice : probe.slices) slices_->addItem(slice.label, static_cast<int>(slice.index));
    slices_->setEnabled(!probe.slices.isEmpty());
    form->addRow(QStringLiteral("Imagem universal"), slices_);
    raw_ = new QCheckBox(QStringLiteral("Importar como firmware bruto"));
    raw_->setChecked(probe.raw_required);
    raw_->setEnabled(probe.slices.isEmpty());
    form->addRow(raw_);
    architecture_ = new QComboBox;
    architecture_->addItem(QStringLiteral("Selecione a arquitetura"), static_cast<int>(Architecture::Unknown));
    for (auto architecture : {Architecture::X86, Architecture::X64, Architecture::Arm, Architecture::Thumb, Architecture::Arm64, Architecture::Mips})
        architecture_->addItem(architecture_name(architecture), static_cast<int>(architecture));
    form->addRow(QStringLiteral("Arquitetura do firmware"), architecture_);
    endian_ = new QComboBox;
    endian_->addItem(QStringLiteral("Little-endian"), static_cast<int>(ByteOrder::Little));
    endian_->addItem(QStringLiteral("Big-endian"), static_cast<int>(ByteOrder::Big));
    form->addRow(QStringLiteral("Ordem dos bytes"), endian_);
    base_ = new QLineEdit(QStringLiteral("0x0"));
    form->addRow(QStringLiteral("Base do firmware"), base_);
    entries_ = new QPlainTextEdit;
    entries_->setPlaceholderText(QStringLiteral("Uma entrada hexadecimal por linha:\nram:0x8000\nram:0x8100\nPara ARM Thumb, selecione Thumb e informe endereços alinhados."));
    entries_->setMaximumHeight(120);
    form->addRow(QStringLiteral("Pontos de entrada"), entries_);
    layout->addLayout(form);
    auto* description = new QLabel(QStringLiteral("Carregadores e análise são próprios da bancada. Firmware bruto exige arquitetura, base e entrada. Decodificação de instruções depende do módulo Capstone disponível na compilação."));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ImportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ImportDialog::reject);
    layout->addWidget(buttons);
    const auto update = [this] {
        for (QWidget* widget : {static_cast<QWidget*>(architecture_), static_cast<QWidget*>(endian_), static_cast<QWidget*>(base_)}) widget->setEnabled(raw_->isChecked());
    };
    connect(raw_, &QCheckBox::toggled, this, [update] { update(); });
    update();
}
OpenOptions ImportDialog::options() const {
    OpenOptions options;
    options.raw = raw_->isChecked();
    if (options.raw) {
        options.raw_architecture = static_cast<Architecture>(architecture_->currentData().toInt());
        options.raw_byte_order = static_cast<ByteOrder>(endian_->currentData().toInt());
        options.raw_base = parse_address(base_->text()).value;
        if (options.raw_architecture == Architecture::Unknown) throw std::invalid_argument("Selecione a arquitetura do firmware.");
    }
    for (const auto& line : entries_->toPlainText().split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty()) options.entries.push_back(parse_address(line));
    if (options.raw && options.entries.isEmpty()) throw std::invalid_argument("Informe ao menos uma entrada para o firmware bruto.");
    if (!probe_.slices.isEmpty()) {
        const auto slice = slices_->currentData().toInt();
        if (slice < 0) throw std::invalid_argument("Escolha uma imagem do arquivo universal.");
        options.slice_index = static_cast<quint32>(slice);
    }
    return options;
}
void ImportDialog::accept() {
    try { (void)options(); QDialog::accept(); }
    catch (const std::exception& error) { QMessageBox::warning(this, QStringLiteral("Configuração inválida"), QString::fromUtf8(error.what())); }
}
} // namespace reworkbench::ui
