#include "debugger_dialog.hpp"
#include <QCloseEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPointer>
#include <QProcess>
#include <QVBoxLayout>

namespace reworkbench::ui {
namespace {
quint64 address_value(QString text) {
    if (text.startsWith(QStringLiteral("ram:"))) text.remove(0, 4);
    bool valid = false; const auto result = text.toULongLong(&valid, 16);
    if (!valid || text.size() > 18) throw std::runtime_error("Endereço hexadecimal inválido."); return result;
}
}
DebuggerDialog::DebuggerDialog(QWidget* parent) : QDialog(parent), state_(std::make_shared<State>()) {
    setWindowTitle(QStringLiteral("Depurador nativo · Windows")); resize(1050, 740);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(QStringLiteral("Iniciar executa o programa escolhido sob depuração. Anexar conecta a um processo existente.\nBreakpoints são de uso único; ao fechar, a sessão tenta restaurar os bytes e se desconectar."));
    description->setWordWrap(true); layout->addWidget(description);
    auto* form = new QFormLayout;
    executable_ = new QLineEdit; arguments_ = new QLineEdit; pid_ = new QLineEdit; address_ = new QLineEdit(QStringLiteral("0x0"));
    auto* path_row = new QWidget; auto* path_layout = new QHBoxLayout(path_row); path_layout->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton(QStringLiteral("Escolher…")); path_layout->addWidget(executable_, 1); path_layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] { const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Programa para depurar")); if (!path.isEmpty()) executable_->setText(path); });
    form->addRow(QStringLiteral("Programa"), path_row); form->addRow(QStringLiteral("Argumentos"), arguments_); form->addRow(QStringLiteral("PID decimal"), pid_); form->addRow(QStringLiteral("Endereço hexadecimal"), address_); layout->addLayout(form);
    auto* controls = new QHBoxLayout;
    const auto button = [&](const QString& text) { auto* result = new QPushButton(text); controls->addWidget(result); return result; };
    launch_ = button(QStringLiteral("Iniciar")); attach_ = button(QStringLiteral("Anexar")); resume_ = button(QStringLiteral("Continuar")); step_ = button(QStringLiteral("Passo"));
    registers_ = button(QStringLiteral("Registradores")); breakpoint_ = button(QStringLiteral("Breakpoint")); read_ = button(QStringLiteral("Ler 256 bytes")); detach_ = button(QStringLiteral("Desconectar"));
    layout->addLayout(controls);
    output_ = new QPlainTextEdit; output_->setReadOnly(true); output_->setMaximumBlockCount(5000); layout->addWidget(output_);
    context_ = new QObject; context_->moveToThread(&worker_);
    connect(&worker_, &QThread::finished, context_, &QObject::deleteLater); worker_.start();
    connect(launch_, &QPushButton::clicked, this, [this] {
        const auto path = executable_->text(); const auto arguments = QProcess::splitCommand(arguments_->text());
        submit([path, arguments](NativeDebugger& debugger) { debugger.launch(path, arguments); return QJsonObject{{"kind", "launched"}}; });
    });
    connect(attach_, &QPushButton::clicked, this, [this] {
        bool valid = false; const auto pid = pid_->text().toUInt(&valid);
        if (!valid || !pid) { output_->appendPlainText(QStringLiteral("PID inválido.")); return; }
        submit([pid](NativeDebugger& debugger) { debugger.attach(pid); return QJsonObject{{"kind", "attached"}}; });
    });
    connect(resume_, &QPushButton::clicked, this, [this] { submit([](NativeDebugger& debugger) { debugger.continue_event(false); return QJsonObject{{"kind", "continued"}}; }); });
    connect(step_, &QPushButton::clicked, this, [this] { const auto thread = thread_id_; submit([thread](NativeDebugger& debugger) { debugger.single_step(thread); return QJsonObject{{"kind", "stepping"}}; }); });
    connect(registers_, &QPushButton::clicked, this, [this] { const auto thread = thread_id_; submit([thread](NativeDebugger& debugger) { return QJsonObject{{"kind", "registers"}, {"registers", debugger.registers(thread)}}; }); });
    connect(breakpoint_, &QPushButton::clicked, this, [this] {
        quint64 address = 0; try { address = address_value(address_->text()); } catch (const std::exception& error) { output_->appendPlainText(QString::fromUtf8(error.what())); return; }
        submit([address](NativeDebugger& debugger) { debugger.set_breakpoint(address); return QJsonObject{{"kind", "breakpoint-installed"}, {"address", hex(address)}}; });
    });
    connect(read_, &QPushButton::clicked, this, [this] {
        quint64 address = 0; try { address = address_value(address_->text()); } catch (const std::exception& error) { output_->appendPlainText(QString::fromUtf8(error.what())); return; }
        submit([address](NativeDebugger& debugger) { return QJsonObject{{"kind", "memory"}, {"address", hex(address)}, {"bytes", QString::fromLatin1(debugger.read_memory(address, 256).toHex(' '))}}; });
    });
    connect(detach_, &QPushButton::clicked, this, [this] { submit([](NativeDebugger& debugger) { debugger.detach(); return QJsonObject{{"kind", "detached"}}; }); });
    timer_.setInterval(150); connect(&timer_, &QTimer::timeout, this, [this] { poll(); }); timer_.start(); refresh_buttons();
}
DebuggerDialog::~DebuggerDialog() {
    timer_.stop();
    if (worker_.isRunning()) {
        QMetaObject::invokeMethod(context_, [state = state_] { state->debugger.reset(); }, Qt::BlockingQueuedConnection);
        worker_.quit(); worker_.wait();
    }
}
void DebuggerDialog::refresh_buttons() {
    launch_->setEnabled(!pending_ && !active_ && !closing_); attach_->setEnabled(launch_->isEnabled());
    for (auto* button : {resume_, step_, registers_, breakpoint_, read_}) button->setEnabled(!pending_ && active_ && event_pending_ && !closing_);
    step_->setEnabled(step_->isEnabled() && thread_id_ != 0); registers_->setEnabled(registers_->isEnabled() && thread_id_ != 0);
    detach_->setEnabled(!pending_ && active_ && !closing_);
}
void DebuggerDialog::submit(std::function<QJsonObject(NativeDebugger&)> work) {
    if (pending_) return; pending_ = true; refresh_buttons(); QPointer<DebuggerDialog> reference(this);
    QMetaObject::invokeMethod(context_, [state = state_, reference, work = std::move(work)] {
        QJsonObject result;
        try { if (!state->debugger) state->debugger = std::make_unique<NativeDebugger>(); result = work(*state->debugger); }
        catch (const std::exception& error) { result = {{"kind", "error"}, {"error", QString::fromUtf8(error.what())}}; }
        catch (...) { result = {{"kind", "error"}, {"error", "Falha inesperada no depurador."}}; }
        result.insert("active", state->debugger && state->debugger->active());
        if (!reference) return;
        QMetaObject::invokeMethod(reference.data(), [reference, result] {
            if (!reference) return; auto& dialog = *reference;
            dialog.pending_ = false; dialog.active_ = result.value("active").toBool();
            const auto kind = result.value("kind").toString();
            if (kind != QStringLiteral("timeout")) dialog.output_->appendPlainText(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
            if (kind == QStringLiteral("launched") || kind == QStringLiteral("attached") || kind == QStringLiteral("continued") || kind == QStringLiteral("stepping")) { dialog.awaiting_ = true; dialog.event_pending_ = false; }
            else if (kind == QStringLiteral("detached")) { dialog.awaiting_ = false; dialog.event_pending_ = false; }
            else if (result.contains("thread_id")) { dialog.thread_id_ = result.value("thread_id").toString().toUInt(); dialog.event_pending_ = true; dialog.awaiting_ = false; }
            if (!dialog.active_) { dialog.event_pending_ = false; dialog.awaiting_ = false; }
            if (kind == QStringLiteral("error")) { dialog.closing_ = false; dialog.awaiting_ = false; }
            dialog.refresh_buttons();
            if (dialog.closing_) {
                if (!dialog.active_) { dialog.allow_close_ = true; dialog.close(); }
                else dialog.submit([](NativeDebugger& debugger) { debugger.detach(); return QJsonObject{{"kind", "detached"}}; });
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
void DebuggerDialog::poll() { if (active_ && awaiting_ && !pending_ && !closing_) submit([](NativeDebugger& debugger) { return debugger.wait_event(100); }); }
void DebuggerDialog::closeEvent(QCloseEvent* event) {
    if (allow_close_ || (!active_ && !pending_)) { event->accept(); return; }
    event->ignore(); closing_ = true; refresh_buttons();
    if (!pending_) submit([](NativeDebugger& debugger) { debugger.detach(); return QJsonObject{{"kind", "detached"}}; });
}
}
