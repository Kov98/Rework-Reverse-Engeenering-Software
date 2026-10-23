#include "assistant_panel.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStyle>

namespace reworkbench::ui {
AssistantPanel::AssistantPanel(std::function<QString()> capture_context, QWidget* parent) : QWidget(parent) {
    setObjectName("assistantPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);
    setFont(interface_font());
    setStyleSheet(themed_stylesheet(QStringLiteral(R"(
        QWidget#assistantPanel { background: @{sideBar.background}; color: @{foreground}; }
        QFrame#assistantChatHeader { background: @{sideBar.background}; border-bottom: 1px solid @{panel.border}; }
        QLabel#assistantChatTab { color: @{foreground}; font-weight: 600; padding: 8px 0; }
        QLabel#assistantTitle, QLabel#assistantBadge { color: @{descriptionForeground}; font-size: @{typography.captionPixels}; }
        QLabel#assistantEmptyTitle { color: @{foreground}; font-weight: 600; }
        QLabel#assistantEmptyHint { color: @{descriptionForeground}; padding: 2px 0; }
        QPushButton[assistantSuggestion="true"] { background: transparent; color: @{foreground}; text-align: left; padding: 3px 6px; border: 1px solid transparent; border-radius: @{radius.control}; }
        QPushButton[assistantSuggestion="true"]:hover { background: @{toolbar.hoverBackground}; }
        QPushButton[assistantSuggestion="true"]:pressed { background: @{toolbar.pressedBackground}; }
        QPushButton[assistantSuggestion="true"]:focus { border-color: @{focusBorder}; }
        QToolButton#assistantPreviewToggle { color: @{descriptionForeground}; background: transparent; padding: 3px 0; text-align: left; }
        QToolButton#assistantPreviewToggle:hover { color: @{foreground}; background: @{toolbar.hoverBackground}; }
        QTabWidget#assistantPreviews::pane { border: 1px solid @{panel.border}; background: @{editor.background}; }
        QTabWidget#assistantPreviews QTabBar::tab { color: @{descriptionForeground}; padding: 5px 8px; border: none; border-bottom: 1px solid transparent; background: @{sideBar.background}; }
        QTabWidget#assistantPreviews QTabBar::tab:selected { color: @{foreground}; border-bottom: 1px solid @{focusBorder}; }
        QTabWidget#assistantPreviews QTabBar::tab:hover { background: @{toolbar.hoverBackground}; }
        QPlainTextEdit#assistantContextPreview, QPlainTextEdit#assistantPrepared { background: @{editor.background}; color: @{foreground}; border: none; padding: 4px; selection-background-color: @{editor.selectionBackground}; }
        QFrame#assistantComposer { background: @{input.background}; border: 1px solid @{input.border}; border-radius: @{radius.composer}; }
        QFrame#assistantComposer[promptFocused="true"] { border-color: @{focusBorder}; }
        QPlainTextEdit#assistantQuestion { background: transparent; color: @{input.foreground}; border: none; padding: 1px; selection-background-color: @{editor.selectionBackground}; }
        QPushButton#assistantContext { background: transparent; color: @{foreground}; border: 1px solid transparent; border-radius: @{radius.control}; padding: 2px 3px; font-size: @{typography.captionPixels}; }
        QPushButton#assistantContext:hover { background: @{toolbar.hoverBackground}; }
        QPushButton#assistantContext:disabled { color: @{disabledForeground}; }
        QLabel#assistantSelection, QLabel#assistantComposerMode { color: @{descriptionForeground}; font-size: @{typography.captionPixels}; }
        QPushButton#assistantPrepare { background: @{button.background}; color: @{button.foreground}; border: 1px solid transparent; border-radius: @{radius.control}; padding: 0; }
        QPushButton#assistantPrepare:hover { background: @{button.hoverBackground}; }
        QPushButton#assistantPrepare:pressed { background: @{button.pressedBackground}; }
        QPushButton#assistantPrepare:disabled { background: @{button.secondaryBackground}; color: @{disabledForeground}; }
        QPushButton#assistantCopy, QPushButton#assistantSend { background: transparent; color: @{descriptionForeground}; font-size: @{typography.captionPixels}; padding: 3px 4px; border: 1px solid transparent; border-radius: @{radius.control}; }
        QPushButton#assistantCopy:hover { color: @{foreground}; background: @{toolbar.hoverBackground}; }
        QPushButton#assistantContext:focus, QPushButton#assistantPrepare:focus, QPushButton#assistantCopy:focus { border-color: @{focusBorder}; }
        QPushButton#assistantCopy:disabled, QPushButton#assistantSend:disabled { color: @{disabledForeground}; }
        QLabel#assistantStatus { color: @{descriptionForeground}; font-size: @{typography.captionPixels}; padding: 0; }
    )")));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 6);
    layout->setSpacing(0);
    auto* header = new QFrame;
    header->setObjectName("assistantChatHeader");
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(10, 0, 10, 0);
    header_layout->setSpacing(8);
    auto* chat = new QLabel(QStringLiteral("CHAT"));
    chat->setObjectName("assistantChatTab");
    header_layout->addWidget(chat);
    auto* title = new QLabel(QStringLiteral("Assistente IA"));
    title->setObjectName("assistantTitle");
    header_layout->addWidget(title);
    header_layout->addStretch();
    auto* badge = new QLabel(QStringLiteral("Local"));
    badge->setObjectName("assistantBadge");
    badge->setToolTip(QStringLiteral("Preparação local de solicitações. Nenhum modelo conectado."));
    header_layout->addWidget(badge);
    layout->addWidget(header);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(12, 14, 12, 0);
    body->setSpacing(10);
    empty_state_ = new QWidget;
    auto* empty_layout = new QVBoxLayout(empty_state_);
    empty_layout->setContentsMargins(0, 0, 0, 0);
    empty_layout->setSpacing(4);
    auto* empty_heading = new QHBoxLayout;
    empty_heading->setSpacing(7);
    auto* assistant_mark = new QLabel;
    assistant_mark->setPixmap(workbench_icon("sparkle", theme_color("brand.focus"), 20).pixmap(QSize(20, 20), devicePixelRatioF()));
    assistant_mark->setFixedSize(20, 20);
    empty_heading->addWidget(assistant_mark);
    auto* empty_title = new QLabel(QStringLiteral("Investigue com contexto"));
    empty_title->setObjectName("assistantEmptyTitle");
    empty_heading->addWidget(empty_title, 1);
    empty_layout->addLayout(empty_heading);
    auto* hint = new QLabel(QStringLiteral("Anexe uma função e prepare sua pergunta. A solicitação pode ser copiada; nenhum modelo está conectado."));
    hint->setObjectName("assistantEmptyHint");
    hint->setWordWrap(true);
    empty_layout->addWidget(hint);
    empty_layout->addSpacing(4);
    const QStringList suggestions{
        QStringLiteral("Explique esta função"),
        QStringLiteral("Entenda o fluxo de controle"),
        QStringLiteral("Investigue as chamadas")
    };
    const QStringList prompts{
        QStringLiteral("Explique o comportamento desta função e destaque as evidências no código."),
        QStringLiteral("Explique os desvios e as condições que controlam o fluxo desta função."),
        QStringLiteral("Quais chamadas esta função faz e o que elas indicam sobre seu comportamento?")
    };
    for (int index = 0; index < suggestions.size(); ++index) {
        auto* suggestion = new QPushButton(suggestions[index]);
        suggestion->setProperty("assistantSuggestion", true);
        suggestion->setIcon(workbench_icon(QStringLiteral("comment-discussion"), theme_color("descriptionForeground")));
        suggestion->setIconSize(QSize(16, 16));
        suggestion->setFixedHeight(28);
        suggestion->setCursor(Qt::PointingHandCursor);
        empty_layout->addWidget(suggestion);
        connect(suggestion, &QPushButton::clicked, this, [this, prompt = prompts[index]] {
            question_->setPlainText(prompt);
            question_->setFocus();
        });
    }
    empty_layout->addStretch(1);
    body->addWidget(empty_state_, 1);

    preview_toggle_ = new QToolButton;
    preview_toggle_->setObjectName("assistantPreviewToggle");
    preview_toggle_->setText(QStringLiteral("Contexto e solicitação"));
    preview_toggle_->setCheckable(true);
    preview_toggle_->setIcon(workbench_icon(QStringLiteral("chevron-right"), theme_color("descriptionForeground")));
    preview_toggle_->setIconSize(QSize(16, 16));
    preview_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    preview_toggle_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    preview_toggle_->hide();
    body->addWidget(preview_toggle_);
    previews_ = new QTabWidget;
    previews_->setObjectName("assistantPreviews");
    previews_->setDocumentMode(true);
    previews_->setMinimumHeight(80);
    context_ = new QPlainTextEdit;
    context_->setObjectName("assistantContextPreview");
    context_->setReadOnly(true);
    context_->setPlaceholderText(QStringLiteral("Anexe a função atual para conferir seu contexto."));
    prepared_ = new QPlainTextEdit;
    prepared_->setObjectName("assistantPrepared");
    prepared_->setReadOnly(true);
    prepared_->setPlaceholderText(QStringLiteral("A solicitação preparada aparecerá aqui."));
    for (auto* editor : {context_, prepared_}) {
        editor->setFont(default_code_font());
        // Text is already bounded by the 8,000/32,768-character input limits.
        // A block cap would silently drop the question when context has many short lines.
    }
    previews_->addTab(context_, QStringLiteral("Contexto"));
    previews_->addTab(prepared_, QStringLiteral("Solicitação"));
    previews_->hide();
    body->addWidget(previews_, 1);
    connect(preview_toggle_, &QToolButton::toggled, this, [this](bool expanded) {
        preview_toggle_->setIcon(workbench_icon(expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"), theme_color("descriptionForeground")));
        previews_->setVisible(expanded);
        empty_state_->setVisible(!expanded);
    });

    auto* composer = new QFrame;
    composer->setObjectName("assistantComposer");
    connect(qApp, &QApplication::focusChanged, composer, [composer](QWidget*, QWidget* focused) {
        const bool within = focused && (focused == composer || composer->isAncestorOf(focused));
        if (composer->property("promptFocused").toBool() == within) return;
        composer->setProperty("promptFocused", within);
        composer->style()->unpolish(composer); composer->style()->polish(composer); composer->update();
    });
    auto* composer_layout = new QVBoxLayout(composer);
    composer_layout->setContentsMargins(9, 9, 9, 7);
    composer_layout->setSpacing(4);
    auto* context_row = new QHBoxLayout;
    context_row->setSpacing(4);
    capture_ = new QPushButton(QStringLiteral("Função atual"));
    capture_->setObjectName("assistantContext");
    capture_->setIcon(workbench_icon(QStringLiteral("attach"), theme_color("foreground")));
    capture_->setIconSize(QSize(16, 16));
    capture_->setToolTip(QStringLiteral("Capturar explicitamente o contexto da função selecionada."));
    context_row->addWidget(capture_);
    selection_ = new QLabel;
    selection_->setObjectName("assistantSelection");
    selection_->setTextFormat(Qt::PlainText);
    selection_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    context_row->addWidget(selection_, 1);
    composer_layout->addLayout(context_row);
    question_ = new QPlainTextEdit;
    question_->setObjectName("assistantQuestion");
    question_->setAccessibleName(QStringLiteral("Pergunta sobre a análise"));
    question_->setPlaceholderText(QStringLiteral("Pergunte sobre a análise…"));
    question_->setFont(interface_font());
    question_->setMinimumHeight(64);
    question_->setMaximumHeight(84);
    composer_layout->addWidget(question_);
    auto* composer_actions = new QHBoxLayout;
    auto* mode = new QLabel(QStringLiteral("Solicitação local"));
    mode->setObjectName("assistantComposerMode");
    composer_actions->addWidget(mode);
    composer_actions->addStretch();
    prepare_ = new QPushButton;
    prepare_->setObjectName("assistantPrepare");
    prepare_->setIcon(workbench_icon(QStringLiteral("arrow-up"), theme_color("foreground")));
    prepare_->setIconSize(QSize(16, 16));
    prepare_->setAccessibleName(QStringLiteral("Preparar pergunta"));
    prepare_->setToolTip(QStringLiteral("Preparar solicitação local para copiar."));
    prepare_->setFixedSize(28, 28);
    prepare_->setEnabled(false);
    composer_actions->addWidget(prepare_);
    composer_layout->addLayout(composer_actions);
    body->addWidget(composer);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(4);
    status_ = new QLabel(QStringLiteral("Sem modelo"));
    status_->setToolTip(QStringLiteral("Nenhum modelo conectado. Preparação local de solicitações."));
    status_->setObjectName("assistantStatus");
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(status_, 1);
    copy_ = new QPushButton(QStringLiteral("Copiar"));
    copy_->setObjectName("assistantCopy");
    copy_->setIcon(workbench_icon(QStringLiteral("copy"), theme_color("descriptionForeground")));
    copy_->setIconSize(QSize(16, 16));
    copy_->setToolTip(QStringLiteral("Copiar solicitação preparada."));
    copy_->setAccessibleName(QStringLiteral("Copiar solicitação preparada"));
    copy_->setEnabled(false);
    footer->addWidget(copy_);
    auto* send = new QPushButton(QStringLiteral("Enviar"));
    send->setObjectName("assistantSend");
    send->setEnabled(false);
    send->setToolTip(QStringLiteral("Nenhum modelo conectado. Prepare e copie a solicitação localmente."));
    footer->addWidget(send);
    body->addLayout(footer);
    layout->addLayout(body, 1);

    connect(question_, &QPlainTextEdit::textChanged, this, [this] {
        if (question_->document()->characterCount() > 8001) {
            const QSignalBlocker blocked(question_);
            auto cursor = question_->textCursor();
            cursor.setPosition(8000);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
        }
        invalidate_request();
        prepare_->setEnabled(!question_->toPlainText().trimmed().isEmpty());
    });
    connect(capture_, &QPushButton::clicked, this, [this, capture_context] {
        captured_context_ = capture_context().left(32768);
        context_->setPlainText(captured_context_);
        invalidate_request();
        capture_->setText(captured_context_.isEmpty() ? QStringLiteral("Função atual") : QStringLiteral("Contexto"));
        preview_toggle_->setVisible(!captured_context_.isEmpty());
        previews_->setCurrentWidget(context_);
        status_->setText(captured_context_.isEmpty() ? QStringLiteral("Contexto indisponível") : QStringLiteral("Contexto anexado"));
    });
    connect(prepare_, &QPushButton::clicked, this, [this] {
        const auto question = question_->toPlainText().trimmed();
        if (question.isEmpty()) return;
        prepared_->setPlainText(QStringLiteral("Pergunta\n%1\n\nContexto\n%2\n\nConsidere os limites da análise; diferencie evidências e hipóteses.")
            .arg(question, captured_context_.isEmpty() ? QStringLiteral("Não incluído.") : captured_context_));
        copy_->setEnabled(true);
        preview_toggle_->show();
        preview_toggle_->setChecked(true);
        previews_->setCurrentWidget(prepared_);
        status_->setText(QStringLiteral("Solicitação pronta"));
    });
    connect(copy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(prepared_->toPlainText());
        status_->setText(QStringLiteral("Solicitação copiada"));
    });
    set_selection({}, {});
}

void AssistantPanel::invalidate_request() {
    prepared_->clear();
    copy_->setEnabled(false);
    status_->setText(QStringLiteral("Sem modelo"));
    if (captured_context_.isEmpty()) {
        preview_toggle_->setChecked(false);
        preview_toggle_->hide();
    } else {
        previews_->setCurrentWidget(context_);
    }
}

void AssistantPanel::set_selection(const QString& identity, const QString& label) {
    if (identity != identity_ || label != selection_label_) {
        captured_context_.clear();
        context_->clear();
        capture_->setText(QStringLiteral("Função atual"));
        invalidate_request();
    }
    identity_ = identity;
    selection_label_ = label;
    selection_->setText(label.isEmpty() ? QStringLiteral("Sem seleção") : label);
    selection_->setToolTip(label.isEmpty() ? QStringLiteral("Selecione uma função para anexar seu contexto.") : label);
    capture_->setEnabled(!identity.isEmpty());
}
}
