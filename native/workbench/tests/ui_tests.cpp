#include "main_window.hpp"
#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QInputDialog>
#include <QTimer>
#include <QTabBar>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QFile>
#include <QTreeWidget>
#include "code_highlighter.hpp"
#include "workbench_icons.hpp"
#include "workspace_theme.hpp"
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QImage>
#include <QHeaderView>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QScreen>
#include <QFontInfo>
#include <QAbstractTextDocumentLayout>
#include "graph_view.hpp"
#include "quick_picker.hpp"
#include "assistant_panel.hpp"
#include "hex_view.hpp"
#include "reworkbench/decompilation_cache.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "code_editor.hpp"
#include <QPlainTextDocumentLayout>

using reworkbench::ui::MainWindow;

namespace {
QString navigation_project(const QString& directory, bool multiple_analyses = false, int padding = 0) {
    using namespace reworkbench;
    const auto sample_path = directory + "/navigation.bin";
    QFile file(sample_path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    // nop; call 0x1007; ret; mov eax,7; ret. The call is inside the first function.
    file.write(QByteArray(padding, char(0x90)) + QByteArray::fromHex("90e801000000c3b807000000c3")); file.close();
    auto project = Project::create(directory + "/project");
    const auto sample = project->import_sample(sample_path);
    for (int number = 0; number < (multiple_analyses ? 2 : 1); ++number) {
        OpenOptions options; options.raw = true; options.raw_architecture = Architecture::X64;
        options.raw_base = number ? 0x2000 : 0x1000; options.entries = {{"ram", options.raw_base}};
        auto snapshot = analyze_binary(project->root() + '/' + sample.relative_path, options);
        if (snapshot.functions.size() != 2) return {};
        snapshot.metadata.sha256 = sample.sha256; snapshot.metadata.original_name = sample.original_name;
        if (project->save_new_analysis(sample, snapshot).isEmpty()) return {};
    }
    return project->root();
}
}

class WorkspaceTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_dir_;
private Q_SLOTS:
    void initTestCase() {
        QVERIFY(settings_dir_.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
        QSettings isolated(QSettings::defaultFormat(), QSettings::UserScope, "REWorkbench", "Native");
        QVERIFY(isolated.fileName().startsWith(settings_dir_.path()));
    }
    void init() { QSettings(QSettings::defaultFormat(), QSettings::UserScope, "REWorkbench", "Native").clear(); }
    void editor_find_shortcuts_search_literal_text_and_wrap() {
        reworkbench::ui::CodeEditor editor;
        editor.resize(650, 280); editor.show(); editor.activateWindow(); editor.setFocus();
        editor.setPlainText("Alpha.*\nbeta\nalpha.*");
        QApplication::processEvents();
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        auto* query = editor.findChild<QLineEdit*>("editorFindQuery");
        auto* count = editor.findChild<QLabel*>("editorFindCount");
        auto* match_case = editor.findChild<QToolButton*>("editorFindCaseSensitive");
        QVERIFY(query && count && match_case);
        QVERIFY(query->isVisible()); QTRY_VERIFY(query->hasFocus());
        QTest::keyClicks(query, "alpha.*");
        QCOMPARE(editor.textCursor().selectionStart(), 0);
        QCOMPARE(editor.textCursor().selectedText(), QString("Alpha.*"));
        QCOMPARE(count->text(), QString("1 / 2"));
        QTest::keyClick(query, Qt::Key_F3);
        QCOMPARE(editor.textCursor().selectionStart(), 13);
        QCOMPARE(count->text(), QString("2 / 2"));
        QTest::keyClick(query, Qt::Key_F3);
        QCOMPARE(editor.textCursor().selectionStart(), 0);
        QTest::keyClick(query, Qt::Key_F3, Qt::ShiftModifier);
        QCOMPARE(editor.textCursor().selectionStart(), 13);
        QTest::mouseClick(match_case, Qt::LeftButton);
        QCOMPARE(editor.textCursor().selectedText(), QString("alpha.*"));
        QCOMPARE(count->text(), QString("1 / 1"));
        QTest::keyClick(query, Qt::Key_Escape);
        QVERIFY(!query->isVisible()); QTRY_VERIFY(editor.hasFocus());
        QTest::keyClick(&editor, Qt::Key_F3);
        QCOMPARE(editor.textCursor().selectedText(), QString("alpha.*"));
        QVERIFY(editor.isReadOnly());
    }
    void editor_find_updates_no_match_and_document_replacement() {
        reworkbench::ui::CodeEditor editor;
        editor.resize(650, 280); editor.show(); editor.activateWindow(); editor.setFocus();
        editor.setPlainText("one needle\ntwo needle");
        QApplication::processEvents();
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        auto* query = editor.findChild<QLineEdit*>("editorFindQuery");
        auto* count = editor.findChild<QLabel*>("editorFindCount");
        QVERIFY(query && count);
        QTest::keyClicks(query, "needle");
        QCOMPARE(editor.textCursor().selectedText(), QString("needle"));
        QCOMPARE(count->text(), QString("1 / 2"));
        query->selectAll(); QTest::keyClicks(query, "absent");
        QVERIFY(!editor.textCursor().hasSelection());
        QCOMPARE(count->text(), QString("Nenhum resultado"));
        QTest::keyClick(query, Qt::Key_F3);
        QVERIFY(!editor.textCursor().hasSelection());
        query->selectAll(); QTest::keyClicks(query, "needle");
        editor.setPlainText("replacement needle");
        QCOMPARE(count->text(), QString("1 / 1"));
        QCOMPARE(editor.textCursor().selectedText(), QString("needle"));
        editor.setPlainText("replacement without the query");
        QCOMPARE(count->text(), QString("Nenhum resultado"));
        QVERIFY(!editor.textCursor().hasSelection());
        // Async analysis can replace a document while its tab is hidden.
        editor.hide();
        editor.setPlainText("needle\nhidden replacement needle");
        editor.show(); QApplication::processEvents();
        int search_highlights = 0;
        for (const auto& selection : editor.extraSelections())
            if (!selection.format.property(QTextFormat::FullWidthSelection).toBool()) ++search_highlights;
        QCOMPARE(search_highlights, 2);
    }
    void editor_find_bounds_highlights_and_clips_the_overlay_on_resize() {
        reworkbench::ui::CodeEditor editor;
        editor.resize(650, 280); editor.show(); editor.activateWindow(); editor.setFocus();
        editor.setPlainText(QStringLiteral("needle\n").repeated(3000));
        QApplication::processEvents();
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        auto* query = editor.findChild<QLineEdit*>("editorFindQuery");
        auto* count = editor.findChild<QLabel*>("editorFindCount");
        auto* overlay = editor.findChild<QWidget*>("editorFindOverlay");
        QVERIFY(query && count && overlay);
        QTest::keyClicks(query, "needle");
        QVERIFY(count->text().contains('+'));
        QVERIFY(editor.extraSelections().size() <= 1001);
        bool preserves_current_line = false;
        for (const auto& selection : editor.extraSelections())
            preserves_current_line |= selection.format.property(QTextFormat::FullWidthSelection).toBool();
        QVERIFY(preserves_current_line);
        // Previous from the first result must still reach beyond the highlight cap.
        QTest::keyClick(query, Qt::Key_F3, Qt::ShiftModifier);
        QCOMPARE(editor.textCursor().blockNumber(), 2999);
        editor.resize(180, 140); QApplication::processEvents();
        const QRect overlay_in_editor(overlay->mapTo(&editor, QPoint()), overlay->size());
        QVERIFY(editor.rect().contains(overlay_in_editor));
        auto* close = editor.findChild<QToolButton*>("editorFindClose"); QVERIFY(close);
        QVERIFY(overlay->rect().contains(QRect(close->mapTo(overlay, QPoint()), close->size())));
        QVERIFY(query->width() > 0);
        QCOMPARE(QFontInfo(query->font()).pixelSize(), 13);
        QTest::keyClick(query, Qt::Key_Escape);
        QCOMPARE(editor.extraSelections().size(), 1);
    }
    void editor_find_count_is_exact_at_the_highlight_limit_and_shortcuts_are_local() {
        QWidget workspace;
        workspace.resize(1000, 350);
        reworkbench::ui::CodeEditor first(&workspace), second(&workspace);
        first.setGeometry(0, 0, 490, 350); second.setGeometry(500, 0, 490, 350);
        first.setPlainText(QStringLiteral("needle\n").repeated(1000));
        second.setPlainText("other text");
        workspace.show(); workspace.activateWindow(); first.setFocus();
        QApplication::processEvents();
        QTest::keyClick(&first, Qt::Key_F, Qt::ControlModifier);
        auto* first_query = first.findChild<QLineEdit*>("editorFindQuery");
        auto* first_count = first.findChild<QLabel*>("editorFindCount");
        auto* second_query = second.findChild<QLineEdit*>("editorFindQuery");
        QVERIFY(first_query && first_count && second_query);
        QVERIFY(first_query->isVisible()); QVERIFY(!second_query->isVisible());
        QTest::keyClicks(first_query, "needle");
        QCOMPARE(first_count->text(), QString("1 / 1000"));
        first.setPlainText(QStringLiteral("needle\n").repeated(1001));
        QVERIFY(first_count->text().contains('+'));
        second.setFocus();
        QTest::keyClick(&second, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(second_query->isVisible()); QTRY_VERIFY(second_query->hasFocus());
        QTest::keyClicks(second_query, "other");
        QCOMPARE(second.textCursor().selectedText(), QString("other"));
        QCOMPARE(first_query->text(), QString("needle"));
        QCOMPARE(first.textCursor().blockNumber(), 0);
    }
    void editor_source_addresses_are_exact_and_programmatic_selection_does_not_echo() {
        using reworkbench::Address;
        reworkbench::ui::CodeEditor editor;
        editor.resize(650, 280); editor.show(); editor.activateWindow(); editor.setFocus();
        editor.setPlainText("heading\ncall\nreturn\ncall duplicate");
        const Address call{"ram", 0x1010}, ret{"ram", 0x1020};
        editor.set_source_addresses({{-1, ret}, {1, call}, {2, ret}, {3, call}, {4, ret}});
        QVector<Address> emitted;
        editor.address_selected = [&](Address address) { emitted.append(address); };
        QVERIFY(!editor.current_address().has_value());
        editor.select_address(call);
        QCOMPARE(editor.textCursor().blockNumber(), 1);
        QVERIFY(editor.current_address() == std::optional<Address>(call));
        QVERIFY(emitted.isEmpty());
        editor.select_address({"file", 0x1010});
        QCOMPARE(editor.textCursor().blockNumber(), 1);
        QApplication::processEvents();
        QTest::keyClick(&editor, Qt::Key_Down);
        QCOMPARE(editor.textCursor().blockNumber(), 2);
        QVERIFY(editor.current_address() == std::optional<Address>(ret));
        QCOMPARE(emitted.size(), 1); QVERIFY(emitted.constLast() == ret);
        editor.select_address(call);
        QCOMPARE(emitted.size(), 1);
        QTest::keyClick(&editor, Qt::Key_Up);
        QVERIFY(!editor.current_address().has_value());
        QCOMPARE(emitted.size(), 1);
        QTest::keyClick(&editor, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(editor.textCursor().selectedText(), QString("h"));
        QTest::keyClicks(&editor, "x");
        QTest::keyClick(&editor, Qt::Key_Delete);
        QTest::keyClick(&editor, Qt::Key_Backspace);
        QCOMPARE(editor.toPlainText(), QString("heading\ncall\nreturn\ncall duplicate"));
        QVERIFY(editor.isReadOnly());
    }
    void editor_source_addresses_clear_on_replacement_and_survive_find_navigation() {
        using reworkbench::Address;
        reworkbench::ui::CodeEditor editor;
        editor.resize(650, 280); editor.show(); editor.activateWindow(); editor.setFocus();
        editor.setPlainText("needle\nheading\nneedle");
        const Address first{"ram", 0x1010}, last{"ram", 0x1030};
        editor.set_source_addresses({{0, first}, {2, last}});
        QVector<Address> emitted;
        editor.address_selected = [&](Address address) { emitted.append(address); };
        QApplication::processEvents();
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        auto* query = editor.findChild<QLineEdit*>("editorFindQuery"); QVERIFY(query);
        QTest::keyClicks(query, "needle");
        emitted.clear();
        QTest::keyClick(query, Qt::Key_F3);
        QVERIFY(editor.current_address() == std::optional<Address>(last));
        QVERIFY(!emitted.isEmpty()); QVERIFY(emitted.constLast() == last);
        editor.select_address(first);
        QVERIFY(editor.current_address() == std::optional<Address>(first));
        emitted.clear();
        static_cast<QPlainTextEdit*>(&editor)->setPlainText("needle\nreplacement\nneedle");
        QVERIFY(!editor.current_address().has_value());
        QTest::keyClick(query, Qt::Key_F3);
        QVERIFY(emitted.isEmpty());
        editor.select_address(last);
        QVERIFY(!editor.current_address().has_value());
        editor.set_source_addresses({{0, first}, {2, last}});
        emitted.clear();
        auto* replacement = new QTextDocument(&editor);
        replacement->setDocumentLayout(new QPlainTextDocumentLayout(replacement));
        replacement->setPlainText("new document\nneedle\nneedle");
        static_cast<QPlainTextEdit*>(&editor)->setDocument(replacement);
        QVERIFY(!editor.current_address().has_value());
        QTest::keyClick(query, Qt::Key_F3);
        QVERIFY(emitted.isEmpty());
    }
    void dark_modern_editors_and_activity_bar() {
        MainWindow window;
        auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QVERIFY(editor);
        QCOMPARE(editor->palette().color(QPalette::Base), QColor("#232327"));
        QCOMPARE(editor->palette().color(QPalette::Text), QColor("#d4d4d8"));
        auto* activity = window.findChild<QToolBar*>("activityBar");
        QVERIFY(activity);
        QCOMPARE(activity->orientation(), Qt::Vertical);
        QVERIFY(window.findChild<QAction*>("command_palette"));
        QVERIFY(QFile::exists(":/codicons/codicon.ttf"));
    }
    void rework_identity_and_native_surfaces_render_consistently() {
        using namespace reworkbench::ui;
        const auto capture_dir = qEnvironmentVariable("REWORKBENCH_UI_CAPTURE_DIR");
        if (!capture_dir.isEmpty()) QVERIFY(QDir().mkpath(capture_dir));
        MainWindow window;
        window.resize(1000, 700);
        window.show(); window.ensurePolished(); QApplication::processEvents();
        QVERIFY(window.windowTitle().contains("Rework"));
        QVERIFY(!window.windowIcon().isNull());
        auto* menus = window.findChild<QMenuBar*>("workspaceMenus");
        auto* tabs = window.findChild<QTabBar*>("functionTabs");
        auto* explorer = window.findChild<QTreeWidget*>("binaryTree");
        auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QVERIFY(menus && tabs && explorer && editor);
        for (auto* widget : QList<QWidget*>{menus, tabs, explorer}) {
            widget->ensurePolished();
            qInfo() << "Polished UI font:" << widget->objectName() << widget->font()
                    << "effective:" << QFontInfo(widget->font()).family() << QFontInfo(widget->font()).pixelSize()
                    << "menu class default:" << QApplication::font("QMenuBar");
            QCOMPARE(QFontInfo(widget->font()).pixelSize(), 13);
        }
        editor->ensurePolished();
        QCOMPARE(QFontInfo(editor->font()).pixelSize(), 14);

        // Inspect rendered alpha, not stylesheet text. Physical image bounds
        // make these samples valid at both normal and high device pixel ratios.
        const auto surface_alpha = [](QWidget* widget) {
            const auto image = widget->grab().toImage().convertToFormat(QImage::Format_ARGB32);
            if (image.isNull()) return QVector<int>{};
            return QVector<int>{image.pixelColor(0, 0).alpha(),
                image.pixelColor(image.width() - 1, 0).alpha(),
                image.pixelColor(0, image.height() - 1).alpha(),
                image.pixelColor(image.width() - 1, image.height() - 1).alpha(),
                image.pixelColor(image.width() / 2, image.height() / 2).alpha()};
        };
        const QVector<int> rounded_surface{0, 0, 0, 0, 255};
        QMenu* menu = nullptr;
        for (auto* action : menus->actions())
            if (action->menu()) { menu = action->menu(); break; }
        QVERIFY(menu);
        menu->ensurePolished();
        menu->popup(menus->mapToGlobal(QPoint(0, menus->height())));
        QApplication::processEvents();
        QVERIFY(menu->isVisible());
        QVERIFY(menu->testAttribute(Qt::WA_TranslucentBackground));
        QCOMPARE(QFontInfo(menu->font()).pixelSize(), 13);
        const auto menu_alpha = surface_alpha(menu);
        const bool menu_saved = capture_dir.isEmpty() || menu->grab().save(QDir(capture_dir).filePath("menu-surface.png"));
        menu->hide();
        QVERIFY(menu_saved);
        QCOMPARE(menu_alpha, rounded_surface);

        const QVector<QuickPickerEntry> entries{
            {symbol_icon("function"), "Alpha", "First function", {}, 1, true},
            {symbol_icon("function"), "Beta", "Second function", {}, 2, true},
            {symbol_icon("function"), "Gamma", "Third function", {}, 3, true},
            {symbol_icon("function"), "Delta", "Fourth function", {}, 4, true}
        };
        bool picker_found = false, picker_translucent = false, resized = false;
        QVector<int> picker_alpha, resized_alpha;
        QTimer safety;
        safety.setSingleShot(true);
        connect(&safety, &QTimer::timeout, &window, [&window] {
            for (auto* dialog : window.findChildren<QDialog*>())
                if (dialog->isVisible()) dialog->reject();
        });
        safety.start(2000);
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("quickOpen");
            if (!dialog) return;
            picker_found = true;
            dialog->ensurePolished();
            picker_translucent = dialog->testAttribute(Qt::WA_TranslucentBackground);
            picker_alpha = surface_alpha(dialog);
            const int original_height = dialog->height();
            if (auto* query = dialog->findChild<QLineEdit*>("quickPickerQuery")) {
                query->setText("Beta");
                window.resize(900, 650);
                QApplication::processEvents();
                resized = dialog->height() < original_height;
                resized_alpha = surface_alpha(dialog);
                QTest::keyClick(query, Qt::Key_Return);
            }
            if (dialog->isVisible()) dialog->reject();
        });
        const auto chosen = choose_quick_picker(&window, false, entries);
        safety.stop();
        QVERIFY(picker_found); QVERIFY(picker_translucent); QVERIFY(resized);
        QCOMPARE(picker_alpha, rounded_surface);
        QCOMPARE(resized_alpha, rounded_surface);
        QVERIFY(chosen.has_value()); QCOMPARE(chosen->toInt(), 2);
        window.close();
    }
    void effective_typography_is_legible_and_uses_the_requested_family() {
        using namespace reworkbench::ui;
        MainWindow window;
        window.show();
        auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QVERIFY(editor);
        const QFontInfo actual(editor->font());
        const QFontInfo ui(interface_font());
        qInfo() << "Effective fonts:" << actual.family() << actual.pixelSize() << actual.fixedPitch()
                << "UI:" << ui.family() << ui.pixelSize() << "DPR:" << window.devicePixelRatioF() << "DPI:" << window.logicalDpiY();
        QVERIFY(window.height() <= window.screen()->availableGeometry().height());
        QCOMPARE(actual.family(), default_code_font().family());
        QVERIFY(actual.fixedPitch());
        QCOMPARE(actual.pixelSize(), 14);
        QCOMPARE(ui.family(), QString("Segoe UI"));
        QCOMPARE(ui.pixelSize(), 13);
        editor->setPlainText("int value = 1;\n    return value;\n}");
        QApplication::processEvents();
        const auto line = editor->document()->documentLayout()->blockBoundingRect(editor->document()->firstBlock());
        QVERIFY(line.height() >= 19);
        QTextCursor cursor(editor->document()); cursor.setPosition(4); cursor.setPosition(9, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        const auto selection = editor->textCursor().selectedText();
        const auto first_bounds = editor->document()->documentLayout()->blockBoundingRect(editor->document()->firstBlock());
        for (int index = 0; index < 10; ++index) {
            editor->viewport()->update(); QApplication::processEvents();
            QCOMPARE(editor->document()->documentLayout()->blockBoundingRect(editor->document()->firstBlock()), first_bounds);
        }
        QCOMPARE(editor->textCursor().selectedText(), selection);
        cursor.clearSelection(); cursor.setPosition(0); editor->setTextCursor(cursor);
        const int first_top = editor->cursorRect().top();
        cursor.movePosition(QTextCursor::NextBlock); editor->setTextCursor(cursor);
        QCOMPARE(editor->cursorRect().top() - first_top, 19);
        window.close();
    }
    void missing_custom_font_uses_an_explicit_available_fallback() {
        using namespace reworkbench::ui;
        QFont missing("REWorkbench deliberately unavailable font", 12);
        missing.setBold(true); missing.setItalic(true);
        const auto resolved = resolve_code_font(missing);
        QCOMPARE(resolved.family(), default_code_font().family());
        QCOMPARE(resolved.pointSize(), 12);
        QVERIFY(resolved.bold()); QVERIFY(resolved.italic());
        QCOMPARE(QFontInfo(resolved).family(), resolved.family());
    }
    void legacy_custom_font_attributes_are_not_replaced_by_defaults() {
        using namespace reworkbench::ui;
        auto custom = default_code_font(); custom.setPointSize(11);
        custom.setWeight(QFont::Light); custom.setUnderline(true);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "REWorkbench", "Native");
        settings.setValue("workspace/codeFont", custom.toString());
        settings.setValue("workspace/typographyVersion", 2);
        MainWindow window;
        auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QVERIFY(editor);
        QCOMPARE(editor->font().weight(), QFont::Light);
        QVERIFY(editor->font().underline());
        QCOMPARE(editor->font().pointSize(), 11);
        window.close();
    }
    void local_assistant_preserves_question_with_many_short_context_lines() {
        using namespace reworkbench::ui;
        AssistantPanel panel([] { return QStringLiteral("x\n").repeated(6000); });
        panel.set_selection("function-id", "function");
        auto* capture = panel.findChild<QPushButton*>("assistantContext");
        auto* question = panel.findChild<QPlainTextEdit*>("assistantQuestion");
        auto* prepare = panel.findChild<QPushButton*>("assistantPrepare");
        auto* copy = panel.findChild<QPushButton*>("assistantCopy");
        QVERIFY(capture && question && prepare && copy);
        capture->click(); question->setPlainText("Preserve this precise question"); prepare->click(); copy->click();
        QVERIFY(QApplication::clipboard()->text().startsWith("Pergunta\nPreserve this precise question\n\nContexto\n"));
        QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("x\n").repeated(6000)));
    }
    void hexadecimal_toolbar_remains_readable_with_a_long_filename() {
        using namespace reworkbench::ui;
        MainWindow theme;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath(QString(120, QLatin1Char('a')) + ".bin");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("12345678"); file.close();
        HexView view; view.resize(400, 100); view.show(); view.show_file(path);
        auto* detail = view.findChild<QLabel*>("hexFileDetail");
        auto* offset = view.findChild<QLineEdit*>("hexOffset");
        auto* next = view.findChild<QPushButton*>("hexNext");
        QVERIFY(detail && offset && next);
        QTRY_VERIFY(detail->text().contains("8 bytes"));
        QCOMPARE(detail->toolTip(), path);
        QVERIFY(view.width() <= 400);
        QVERIFY(offset->height() >= offset->sizeHint().height());
        QVERIFY(next->height() >= next->sizeHint().height());
        QVERIFY(view.rect().contains(next->geometry().translated(next->parentWidget()->pos())));
        view.close(); theme.close();
    }
    void quick_picker_keeps_shortcuts_separate_and_skips_disabled_results() {
        using namespace reworkbench::ui;
        MainWindow window; window.resize(900, 650); window.show();
        const QVector<QuickPickerEntry> entries{
            {symbol_icon("function"), "Primeiro", "Arquivo", "Ctrl+P", 1, true},
            {{}, "Indisponível", "Análise", {}, 2, false},
            {{}, "Último", "Visualizar", "Ctrl+J", 3, true}
        };
        bool found = false, separate = false, skips = false, prefix = false, editing = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("commandPalette");
            if (!dialog) return;
            auto* query = dialog->findChild<QLineEdit*>("quickPickerQuery");
            auto* list = dialog->findChild<QListWidget*>("quickPickerResults");
            if (query && list) {
                found = true;
                separate = list->item(0)->text() == "Primeiro" && list->item(0)->data(Qt::UserRole + 2).toString() == "Ctrl+P";
                QTest::keyClick(query, Qt::Key_Down);
                skips = list->currentItem()->data(Qt::UserRole).toInt() == 3;
                query->setText("> Primeiro"); prefix = list->count() == 1 && list->currentItem()->data(Qt::UserRole).toInt() == 1;
                query->setCursorPosition(query->text().size()); QTest::keyClick(query, Qt::Key_Home);
                editing = query->cursorPosition() == 0;
                QTest::keyClick(query, Qt::Key_Return);
            }
            if (dialog->isVisible()) dialog->reject();
        });
        const auto chosen = choose_quick_picker(&window, true, entries);
        QVERIFY(found); QVERIFY(separate); QVERIFY(skips); QVERIFY(prefix); QVERIFY(editing);
        QVERIFY(chosen.has_value()); QCOMPARE(chosen->toInt(), 1);
        window.close();
    }
    void file_and_symbol_icons_remain_distinct_and_render_without_installation() {
        using namespace reworkbench::ui;
        MainWindow window;
        QVERIFY(QFile::exists(":/seti/seti.ttf"));
        for (const auto* filename : {"source.c", "source.cpp", "source.cs", "source.java", "source.py", "source.rs", "source.js", "source.ts", "source.asm", "source.html", "source.css", "source.json", "source.xml", "source.hpp", "source.exe", "source.bin", "source.dll", "unknown.file"}) {
            const auto icon = file_icon(QString::fromLatin1(filename));
            QVERIFY2(!icon.isNull(), filename);
            const auto pixels = icon.pixmap(QSize(16, 16), 2.0).toImage();
            bool visible = false;
            for (int y = 0; y < pixels.height(); ++y) for (int x = 0; x < pixels.width(); ++x)
                visible |= pixels.pixelColor(x, y).alpha() > 128;
            QVERIFY2(visible, filename);
        }
        QVERIFY(file_icon("source.cpp").pixmap(16).toImage() != file_icon("source.py").pixmap(16).toImage());
        QVERIFY(file_icon("source.cpp").name() != symbol_icon("function").name());
        QCOMPARE(file_icon("header.HPP").name(), file_icon("header.hpp").name());
        for (const auto* kind : {"class", "struct", "namespace", "function", "variable", "import", "export", "memory", "string", "symbol", "reference", "instruction", "graph", "breakpoint"})
            QVERIFY2(!symbol_icon(QString::fromLatin1(kind)).isNull(), kind);
        window.close();
    }
    void reduced_motion_preference_survives_restart() {
        {
            MainWindow window;
            auto* reduced = window.findChild<QAction*>("reduce_motion");
            QVERIFY(reduced); reduced->setChecked(true);
            QVERIFY(!window.dockOptions().testFlag(QMainWindow::AnimatedDocks));
            window.close();
        }
        MainWindow window;
        auto* reduced = window.findChild<QAction*>("reduce_motion");
        QVERIFY(reduced && reduced->isChecked());
        QVERIFY(!window.dockOptions().testFlag(QMainWindow::AnimatedDocks));
        window.close();
    }
    void empty_quick_open_is_compact_and_enter_does_not_close_it() {
        MainWindow window;
        window.resize(900, 650); window.show();
        bool found = false, compact = false, no_activation = false, fits = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("quickOpen");
            if (!dialog) return;
            found = true;
            auto* query = dialog->findChild<QLineEdit*>("quickPickerQuery");
            compact = dialog->height() < 140;
            fits = dialog->width() <= 600 && dialog->width() <= window.width() - 32;
            if (query) { QTest::keyClick(query, Qt::Key_Return); no_activation = dialog->isVisible(); }
            dialog->reject();
        });
        window.findChild<QAction*>("quick_open")->trigger();
        QVERIFY(found); QVERIFY(compact); QVERIFY(fits); QVERIFY(no_activation);
        window.close();
    }
    void graph_opens_at_readable_scale_and_still_offers_overview() {
        using namespace reworkbench;
        MainWindow theme;
        Function function; function.address = {"ram", 0x1000}; function.name = "flow";
        for (int index = 0; index < 8; ++index) {
            const Address address{"ram", quint64(0x1000 + index * 16)};
            function.instructions.push_back({address, QByteArray::fromHex("c3"), "ret", ""});
            BasicBlock block{address, {quint32(index)}, {}};
            if (index < 7) block.successors.push_back({"ram", address.value + 16});
            function.blocks.push_back(block);
        }
        reworkbench::ui::GraphView graph;
        graph.resize(380, 320); graph.show();
        graph.show_function(&function);
        QApplication::processEvents();
        QCOMPARE(graph.transform().m11(), qreal(1));
        graph.fit_graph();
        QVERIFY(graph.transform().m11() < 1);
        auto large_font = reworkbench::ui::default_code_font();
        large_font.setPointSize(24);
        graph.setFont(large_font);
        graph.show_function(&function);
        QApplication::processEvents();
        int checked_panels = 0;
        for (auto* item : graph.scene()->items()) {
            auto* panel = qgraphicsitem_cast<QGraphicsRectItem*>(item);
            if (!panel || panel->parentItem()) continue;
            QGraphicsTextItem* title = nullptr;
            QGraphicsTextItem* body = nullptr;
            for (auto* child : panel->childItems()) {
                auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(child);
                if (!text) continue;
                if (!title || text->pos().y() < title->pos().y()) { body = title; title = text; }
                else body = text;
            }
            QVERIFY(title && body);
            QVERIFY(title->sceneBoundingRect().bottom() <= body->sceneBoundingRect().top());
            QVERIFY(body->sceneBoundingRect().bottom() <= panel->sceneBoundingRect().bottom());
            ++checked_panels;
        }
        QCOMPARE(checked_panels, 8);
        graph.close(); theme.close();
    }
    void syntax_distinguishes_strings_comments_calls_and_variables() {
        QTextDocument document;
        reworkbench::ui::CodeHighlighter highlighter(&document);
        document.setPlainText("const char* message = \"https://host/*literal*/\"; // comment\n"
                              "int total = compute(value) + 42;\n"
                              "/* block\nreturn 10;\n*/ if (value) return 1;");
        highlighter.rehighlight();
        const auto color_at = [](const QTextBlock& block, int position) {
            for (const auto& range : block.layout()->formats())
                if (position >= range.start && position < range.start + range.length) return range.format.foreground().color();
            return QColor();
        };
        const auto first = document.firstBlock();
        QCOMPARE(color_at(first, int(first.text().indexOf("https"))), QColor("#ce9178"));
        QCOMPARE(color_at(first, int(first.text().indexOf("literal"))), QColor("#ce9178"));
        QCOMPARE(color_at(first, int(first.text().indexOf("comment"))), QColor("#6a9955"));
        const auto second = first.next();
        QCOMPARE(color_at(second, int(second.text().indexOf("total"))), QColor("#9cdcfe"));
        QCOMPARE(color_at(second, int(second.text().indexOf("compute"))), QColor("#dcdcaa"));
        QCOMPARE(color_at(second, int(second.text().indexOf("42"))), QColor("#b5cea8"));
        QCOMPARE(color_at(second.next().next(), 0), QColor("#6a9955"));
        QCOMPARE(color_at(document.lastBlock(), 3), QColor("#c586c0"));
    }
    void embedded_codicons_render_with_tint_and_high_dpi() {
        using namespace reworkbench::ui;
        MainWindow window;
        for (const auto* name : {"files", "search", "type-hierarchy", "debug", "sparkle", "settings-gear", "attach", "copy", "arrow-up", "comment-discussion", "chevron-right", "chevron-down"})
            QVERIFY2(!workbench_icon(QString::fromLatin1(name)).isNull(), name);
        const auto icon = workbench_icon("files", QColor("#75beff"));
        const auto image = icon.pixmap(QSize(16, 16), 2.0).toImage();
        QCOMPARE(image.devicePixelRatio(), qreal(2));
        QCOMPARE(image.size(), QSize(32, 32));
        bool visible = false;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 200) {
                visible = true;
                QVERIFY(qAbs(pixel.red() - 0x75) <= 2 && qAbs(pixel.green() - 0xbe) <= 2 && qAbs(pixel.blue() - 0xff) <= 2);
            }
        }
        QVERIFY(visible);
        QCOMPARE(theme_color("widget.shadow").alpha(), 102);
        QCOMPARE(theme_color("sideBar.background"), QColor("#1e1e22"));
        window.close();
    }
    void visible_windows_can_be_destroyed_without_an_explicit_close() {
        for (int count = 0; count < 4; ++count) {
            auto window = std::make_unique<MainWindow>();
            window->show(); QApplication::processEvents();
            window.reset(); QApplication::processEvents();
        }
    }
    void compact_shell_has_a_command_center_and_collapsible_regions() {
        MainWindow window;
        window.show();
        auto* chrome = window.findChild<QWidget*>("workspaceChrome");
        auto* center = window.findChild<QPushButton*>("commandCenter");
        auto* tree = window.findChild<QTreeWidget*>("binaryTree");
        auto* sidebar = window.findChild<QDockWidget*>("explorerDock");
        auto* bottom = window.findChild<QDockWidget*>("panelDock");
        auto* panels = window.findChild<QTabWidget*>("bottomPanel");
        auto* toggle_sidebar = window.findChild<QAction*>("toggle_sidebar");
        auto* toggle_panel = window.findChild<QAction*>("toggle_panel");
        QVERIFY(chrome && center && tree && sidebar && bottom && panels && toggle_sidebar && toggle_panel);
        QVERIFY(chrome->height() <= 40);
        QCOMPARE(panels->tabPosition(), QTabWidget::North);
        QVERIFY(sidebar->isVisible());
        toggle_sidebar->trigger(); QVERIFY(!sidebar->isVisible());
        toggle_sidebar->trigger(); QVERIFY(sidebar->isVisible());
        auto* sidebar_button = chrome->findChild<QToolButton*>("chrome_toggle_sidebar");
        QVERIFY(sidebar_button && !sidebar_button->icon().isNull());
        QCOMPARE(sidebar_button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        toggle_panel->trigger(); QVERIFY(!bottom->isVisible());
        toggle_panel->trigger(); QVERIFY(bottom->isVisible());
        window.close();
    }
    void editor_order_and_hidden_views_survive_restart() {
        {
            MainWindow window;
            auto* tabs = window.findChild<QTabWidget*>("representationTabs");
            auto* ir = window.findChild<QAction*>("view_ir");
            QVERIFY(tabs && ir);
            QVERIFY(tabs->isMovable());
            QCOMPARE(tabs->count(), 4);
            tabs->tabBar()->moveTab(0, 3);
            ir->trigger();
            QCOMPARE(tabs->count(), 3);
            window.close();
        }
        MainWindow restored;
        auto* tabs = restored.findChild<QTabWidget*>("representationTabs");
        QVERIFY(tabs);
        QStringList keys;
        for (int i = 0; i < tabs->count(); ++i) keys << tabs->widget(i)->objectName();
        QCOMPARE(keys, QStringList({"ssaEditor", "graphView", "pseudocodePanel"}));
        auto* ir = restored.findChild<QAction*>("view_ir");
        QVERIFY(ir && !ir->isChecked());
        ir->trigger();
        QCOMPARE(tabs->count(), 4);
        restored.close();
    }
    void reset_layout_recovers_closed_and_floating_panels() {
        MainWindow window;
        auto* functions = window.findChild<QDockWidget*>("explorerDock");
        auto* assistant = window.findChild<QDockWidget*>("assistantDock");
        auto* reset = window.findChild<QAction*>("layout_reset");
        QVERIFY(functions && assistant && reset);
        functions->setFloating(true);
        functions->hide(); assistant->hide();
        reset->trigger();
        QVERIFY(!functions->isFloating());
        QVERIFY(!functions->isHidden());
        QVERIFY(!assistant->isHidden());
        QCOMPARE(window.dockWidgetArea(functions), Qt::LeftDockWidgetArea);
        QCOMPARE(window.dockWidgetArea(assistant), Qt::RightDockWidgetArea);
        window.close();
    }
    void assistant_prepares_a_real_local_request_without_fake_response() {
        MainWindow window;
        auto* question = window.findChild<QPlainTextEdit*>("assistantQuestion");
        auto* prepared = window.findChild<QPlainTextEdit*>("assistantPrepared");
        auto* prepare = window.findChild<QPushButton*>("assistantPrepare");
        auto* send = window.findChild<QPushButton*>("assistantSend");
        auto* copy = window.findChild<QPushButton*>("assistantCopy");
        QVERIFY(question && prepared && prepare && send && copy);
        QVERIFY(!send->isEnabled());
        question->setPlainText(QString::fromUtf8("Explique este salto e suas condições."));
        prepare->click();
        QVERIFY(prepared->toPlainText().contains(question->toPlainText()));
        QVERIFY(prepared->toPlainText().contains("Contexto"));
        QVERIFY(copy->isEnabled());
        copy->click();
        QCOMPARE(QApplication::clipboard()->text(), prepared->toPlainText());
        QVERIFY(!send->isEnabled());
        window.close();
    }
    void code_font_preference_survives_restart() {
        QFont chosen("Consolas", 12);
        chosen.setBold(true); chosen.setItalic(true);
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, "REWorkbench", "Native").setValue("workspace/codeFont", chosen.toString());
        int previous = 0;
        {
            MainWindow window;
            auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
            auto* increase = window.findChild<QAction*>("font_increase");
            QVERIFY(editor && increase);
            previous = editor->font().pointSize();
            QVERIFY(editor->font().bold());
            QVERIFY(editor->font().italic());
            increase->trigger();
            QCOMPARE(editor->font().pointSize(), previous + 1);
            auto* listing = window.findChild<QWidget*>("listingPanel");
            QVERIFY(listing);
            auto* table = listing->findChild<QTableView*>();
            QVERIFY(table);
            QVERIFY(table->verticalHeader()->defaultSectionSize() >= table->fontMetrics().height() + 6);
            window.close();
        }
        MainWindow restored;
        auto* editor = restored.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QVERIFY(editor);
        QCOMPARE(editor->font().pointSize(), previous + 1);
        QVERIFY(editor->font().bold());
        QVERIFY(editor->font().italic());
        restored.close();
    }
private:
    static reworkbench::AnalysisSnapshot source_address_fixture() {
        using namespace reworkbench;
        AnalysisSnapshot snapshot;
        snapshot.metadata.architecture = Architecture::X64;
        snapshot.metadata.bits = 64;
        snapshot.metadata.sha256 = QString(64, QLatin1Char('a'));
        Function function; function.address = {"ram", 0x1000}; function.name = "source_fixture";
        Operand eax; eax.kind = OperandKind::Register; eax.reg = "eax"; eax.width_bits = 32;
        Operand ebx = eax; ebx.reg = "ebx";
        Operand ecx = eax; ecx.reg = "ecx";
        Instruction move; move.address = {"ram", 0x1000}; move.bytes = QByteArray::fromHex("89d8");
        move.mnemonic = "mov"; move.operands_text = "eax, ebx"; move.operands = {eax, ebx};
        Instruction add; add.address = {"ram", 0x1002}; add.bytes = QByteArray::fromHex("01c8");
        add.mnemonic = "add"; add.operands_text = "eax, ecx"; add.operands = {eax, ecx};
        Instruction ret; ret.address = {"ram", 0x1004}; ret.bytes = QByteArray::fromHex("c3");
        ret.mnemonic = "ret"; ret.flow = FlowKind::Return; ret.falls_through = false;
        function.instructions = {move, add, ret};
        function.blocks = {{{"ram", 0x1000}, {0, 1, 2}, {}}};
        snapshot.functions = {function};
        return snapshot;
    }
private Q_SLOTS:
    void renderers_preserve_exact_instruction_sources() {
        using namespace reworkbench;
        const auto snapshot = source_address_fixture();
        const auto& function = snapshot.functions.front();
        const auto result = decompile_function(snapshot, 0);
        const auto ir = render_ir(function, snapshot.metadata.architecture);
        QVERIFY(result.dataflow_converged);
        QVERIFY(!result.pseudocode_addresses.isEmpty());
        QVERIFY(!result.ssa_addresses.isEmpty());
        QVERIFY(!ir.source_addresses.isEmpty());
        const auto code_lines = result.pseudocode.split('\n');
        const auto ssa_lines = result.ssa_text.split('\n');
        const auto ir_lines = ir.text.split('\n');
        const auto contains_source = [&](const Address& address) {
            for (const auto& instruction : function.instructions) if (instruction.address == address) return true;
            return false;
        };
        for (const auto* addresses : {&result.pseudocode_addresses, &result.ssa_addresses, &ir.source_addresses})
            for (auto source = addresses->cbegin(); source != addresses->cend(); ++source)
                QVERIFY(contains_source(source.value()));
        for (const auto& instruction : function.instructions) {
            int code_comment = -1, ir_comment = -1;
            for (int line = 0; line < code_lines.size(); ++line)
                if (code_lines[line].contains("/* " + instruction.address.text() + ": ")) code_comment = line;
            for (int line = 0; line < ir_lines.size(); ++line)
                if (ir_lines[line].contains("; " + instruction.address.text() + "  ")) ir_comment = line;
            QVERIFY(code_comment >= 0); QVERIFY(ir_comment >= 0);
            QVERIFY(result.pseudocode_addresses.value(code_comment) == instruction.address);
            QVERIFY(ir.source_addresses.value(ir_comment) == instruction.address);
            QVERIFY(ir.source_addresses.value(ir_comment + 1) == instruction.address);
            bool code_definition = false, ssa_definition = false;
            for (auto source = result.pseudocode_addresses.cbegin(); source != result.pseudocode_addresses.cend(); ++source)
                if (source.value() == instruction.address && code_lines[source.key()].trimmed().startsWith("v_")) code_definition = true;
            for (auto source = result.ssa_addresses.cbegin(); source != result.ssa_addresses.cend(); ++source)
                if (source.value() == instruction.address && ssa_lines[source.key()].contains(" = ")) ssa_definition = true;
            QVERIFY(code_definition); QVERIFY(ssa_definition);
        }
        for (int line = 0; line < code_lines.size(); ++line) {
            const auto text = code_lines[line].trimmed();
            if (text.startsWith("u32 ") || text.startsWith("u64 ") || text.startsWith("u1 ") ||
                text.startsWith("machine_state ") || text.startsWith("L_"))
                QVERIFY(!result.pseudocode_addresses.contains(line));
            if (text.startsWith("return machine_return(")) {
                QVERIFY(result.pseudocode_addresses.contains(line));
                QCOMPARE(result.pseudocode_addresses.value(line).value, quint64(0x1004));
            }
        }
        for (int line = 0; line < ssa_lines.size(); ++line)
            if (ssa_lines[line].contains(" = input(") || ssa_lines[line].contains(" = phi(") || ssa_lines[line].contains(" simplified to "))
                QVERIFY(!result.ssa_addresses.contains(line));
        QVERIFY(!result.pseudocode_addresses.contains(0));
        QVERIFY(!result.ssa_addresses.contains(0));
        QVERIFY(!ir.source_addresses.contains(0));
    }
    void source_addresses_survive_only_complete_retained_lines() {
        using namespace reworkbench;
        auto snapshot = source_address_fixture();
        auto& function = snapshot.functions.front();
        const auto prototype = function.instructions[1];
        function.instructions.clear(); function.blocks.clear();
        for (int index = 0; index < 100; ++index) {
            auto instruction = prototype; instruction.address.value = 0x1000 + 2 * index;
            function.instructions.append(instruction);
        }
        auto ret = source_address_fixture().functions.front().instructions.back();
        ret.address.value = 0x1000 + 200; function.instructions.append(ret);
        DecompilationOptions options; options.max_output_characters = 16384;
        const auto verify_retained = [](const QString& full, const QMap<int, Address>& full_addresses,
                                       const QString& limited, const QMap<int, Address>& limited_addresses) {
            const auto full_lines = full.split('\n'), limited_lines = limited.split('\n');
            QMap<int, Address> expected;
            for (auto source = full_addresses.cbegin(); source != full_addresses.cend(); ++source)
                if (source.key() < limited_lines.size() && full_lines[source.key()] == limited_lines[source.key()]) expected.insert(source.key(), source.value());
            return !expected.isEmpty() && limited_addresses == expected;
        };
        const auto full = decompile_function(snapshot, 0);
        const auto limited = decompile_function(snapshot, 0, options);
        QVERIFY(limited.output_truncated);
        QVERIFY(verify_retained(full.pseudocode, full.pseudocode_addresses, limited.pseudocode, limited.pseudocode_addresses));
        QVERIFY(verify_retained(full.ssa_text, full.ssa_addresses, limited.ssa_text, limited.ssa_addresses));
        snapshot.metadata.architecture = Architecture::Arm64;
        for (auto& instruction : function.instructions) instruction.operands_text = QString(127, QLatin1Char('x'));
        const auto opaque_full = decompile_function(snapshot, 0);
        const auto opaque_limited = decompile_function(snapshot, 0, options);
        QVERIFY(opaque_limited.output_truncated);
        QVERIFY(verify_retained(opaque_full.pseudocode, opaque_full.pseudocode_addresses, opaque_limited.pseudocode, opaque_limited.pseudocode_addresses));
        QVERIFY(opaque_limited.ssa_addresses.isEmpty());
        function.instructions.clear();
        const auto empty = decompile_function(snapshot, 0);
        QVERIFY(empty.pseudocode_addresses.isEmpty()); QVERIFY(empty.ssa_addresses.isEmpty());
    }
    void decompilation_cache_preserves_and_validates_source_addresses() {
        using namespace reworkbench;
        QTemporaryDir folder; QVERIFY(folder.isValid());
        auto project = Project::create(folder.filePath("project"));
        const auto snapshot = source_address_fixture();
        const auto first = cached_decompile(project, snapshot, 0);
        QVERIFY(first.dataflow_converged); QVERIFY(!first.output_truncated);
        QString cache_key; QJsonObject stored;
        const auto connection_name = QStringLiteral("source_address_cache_test");
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", connection_name);
            database.setDatabaseName(project->root() + "/index.sqlite3");
            QVERIFY(database.open());
            {
                QSqlQuery query(database);
                QVERIFY(query.exec("SELECT cache_key,payload FROM derived_cache")); QVERIFY(query.next());
                cache_key = query.value(0).toString();
                stored = QJsonDocument::fromJson(query.value(1).toByteArray()).object();
                QVERIFY(!query.next());
            }
            database.close();
        }
        QSqlDatabase::removeDatabase(connection_name);
        QVERIFY(!cache_key.isEmpty());
        const auto marker = QStringLiteral("cache hit sentinel");
        auto marked = stored; auto warnings = marked.value("warnings").toArray(); warnings.append(marker); marked.insert("warnings", warnings);
        project->cache_result(cache_key, marked);
        const auto cached = cached_decompile(project, snapshot, 0);
        QVERIFY(cached.warnings.contains(marker));
        QCOMPARE(cached.pseudocode, first.pseudocode); QCOMPARE(cached.ssa_text, first.ssa_text);
        QVERIFY(cached.pseudocode_addresses == first.pseudocode_addresses);
        QVERIFY(cached.ssa_addresses == first.ssa_addresses);
        auto old_format = marked; old_format.insert("kind", "decompiler-default-v1");
        old_format.remove("pseudocode_addresses"); old_format.remove("ssa_addresses");
        auto outside_line = marked; auto addresses = outside_line.value("pseudocode_addresses").toArray();
        QVERIFY(!addresses.isEmpty());
        auto bad_line = addresses[0].toObject(); bad_line.insert("line", 2000000); addresses[0] = bad_line;
        outside_line.insert("pseudocode_addresses", addresses);
        auto unknown_instruction = marked; addresses = unknown_instruction.value("ssa_addresses").toArray();
        QVERIFY(!addresses.isEmpty());
        auto bad_source = addresses[0].toObject(); bad_source.insert("address", QJsonObject{{"space", "ram"}, {"value", "0xdeadbeef"}}); addresses[0] = bad_source;
        unknown_instruction.insert("ssa_addresses", addresses);
        for (const auto& invalid : {old_format, outside_line, unknown_instruction}) {
            project->cache_result(cache_key, invalid);
            const auto recalculated = cached_decompile(project, snapshot, 0);
            QVERIFY(!recalculated.warnings.contains(marker));
            QVERIFY(recalculated.pseudocode_addresses == first.pseudocode_addresses);
            QVERIFY(recalculated.ssa_addresses == first.ssa_addresses);
        }
    }
    void references_keep_the_precise_instruction_source() {
        using namespace reworkbench;
        auto snapshot = std::make_shared<AnalysisSnapshot>();
        snapshot->references.push_back({{"ram", 0x1004}, {"ram", 0x2000}, "call", Address{"ram", 0x1000}});
        reworkbench::ui::Explorer explorer(reworkbench::ui::Dataset::References, {"Origem", "Destino", "Tipo"});
        explorer.set_snapshot(snapshot);
        QTRY_COMPARE(explorer.view()->model()->rowCount(), 1);
        QCOMPARE(explorer.view()->model()->index(0, 0).data().toString(), QString("00001004"));
        std::optional<Address> source;
        explorer.activated = [&source](const reworkbench::ui::Record& row, int) { source = row.address; };
        explorer.view()->setCurrentIndex(explorer.view()->model()->index(0, 0));
        explorer.activate_current();
        QVERIFY(source.has_value()); QCOMPARE(source->value, quint64(0x1004));
    }
    void history_returns_to_the_selected_call_instruction() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path());
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window; window.show(); window.open_project_path(root);
        auto* listing = window.findChild<QWidget*>("listingPanel")->findChild<QTableView*>();
        auto* heading = window.findChild<QLabel*>("functionHeading");
        QTRY_COMPARE_WITH_TIMEOUT(listing->model()->rowCount(), 3, 15000);
        listing->setCurrentIndex(listing->model()->index(1, 0));
        QTRY_VERIFY(heading->text().endsWith("0x1001"));
        window.findChild<QAction*>("follow_target")->trigger();
        QTRY_VERIFY(heading->text().endsWith("0x1007"));
        window.findChild<QAction*>("back")->trigger();
        QTRY_VERIFY(heading->text().endsWith("0x1001"));
        QTRY_COMPARE(listing->currentIndex().data().toString(), QString("00001001"));
        window.findChild<QAction*>("forward")->trigger();
        QTRY_VERIFY(heading->text().endsWith("0x1007"));
    }
    void closing_background_function_preserves_the_active_instruction() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path());
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window; window.show(); window.open_project_path(root);
        auto* listing = window.findChild<QWidget*>("listingPanel")->findChild<QTableView*>();
        auto* heading = window.findChild<QLabel*>("functionHeading");
        auto* tabs = window.findChild<QTabBar*>("functionTabs");
        QTRY_COMPARE_WITH_TIMEOUT(listing->model()->rowCount(), 3, 15000);
        listing->setCurrentIndex(listing->model()->index(1, 0));
        window.findChild<QAction*>("follow_target")->trigger();
        QTRY_VERIFY(heading->text().endsWith("0x1007"));
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(listing->model()->rowCount(), 3);
        listing->setCurrentIndex(listing->model()->index(1, 0));
        QTRY_VERIFY(heading->text().endsWith("0x1001"));
        QCOMPARE(tabs->count(), 2);
        const auto before = heading->text();
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, 1)));
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(heading->text(), before);
        QCOMPARE(listing->currentIndex().data().toString(), QString("00001001"));
        window.close();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
    void switching_analysis_adds_a_history_destination() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path(), true);
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window; window.show(); window.open_project_path(root);
        auto* analyses = window.findChild<QComboBox*>();
        auto* heading = window.findChild<QLabel*>("functionHeading");
        auto* back = window.findChild<QAction*>("back");
        QTRY_VERIFY_WITH_TIMEOUT(analyses->count() == 2 && !heading->text().contains("Selecione"), 15000);
        const auto previous = heading->text();
        const int previous_index = analyses->currentIndex();
        analyses->setCurrentIndex(1 - previous_index);
        QTRY_VERIFY_WITH_TIMEOUT(heading->text() != previous && heading->text().contains("0x"), 15000);
        const auto next = heading->text();
        QTRY_VERIFY_WITH_TIMEOUT(back->isEnabled(), 15000);
        back->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(heading->text(), previous, 15000);
        QCOMPARE(analyses->currentIndex(), previous_index);
        window.findChild<QAction*>("forward")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(heading->text(), next, 15000);
    }
    void instruction_selection_synchronizes_real_code_locations() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path());
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window; window.show(); window.open_project_path(root);
        auto* listing = window.findChild<QWidget*>("listingPanel")->findChild<QTableView*>();
        auto* pseudocode = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        auto* ssa = window.findChild<QPlainTextEdit*>("ssaEditor");
        auto* ir = window.findChild<QPlainTextEdit*>("irEditor");
        QTRY_COMPARE_WITH_TIMEOUT(listing->model()->rowCount(), 3, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(pseudocode->toPlainText().contains("/* ram:0x1006:"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(ir->toPlainText().contains("; ram:0x1006"), 15000);
        listing->setCurrentIndex(listing->model()->index(1, 0));
        QVERIFY(pseudocode->textCursor().block().text().contains("ram:0x1001"));
        QVERIFY(ssa->textCursor().block().text().contains("ram:0x1001"));
        QVERIFY(ir->textCursor().block().text().contains("ram:0x1001"));
        const auto ret_line = pseudocode->document()->find("/* ram:0x1006:");
        QVERIFY(!ret_line.isNull());
        pseudocode->setTextCursor(ret_line);
        QTRY_COMPARE(listing->currentIndex().data().toString(), QString("00001006"));
        QVERIFY(window.findChild<QLabel*>("functionHeading")->text().endsWith("0x1006"));
        QSignalSpy rebuilt(pseudocode, &QPlainTextEdit::textChanged);
        auto* graph = static_cast<reworkbench::ui::GraphView*>(window.findChild<QGraphicsView*>("graphView"));
        QVERIFY(graph);
        // Following a block in the same function should reuse the loaded document.
        graph->activated({"ram", 0x1000});
        QTRY_COMPARE(listing->currentIndex().data().toString(), QString("00001000"));
        QCOMPARE(rebuilt.count(), 0);
    }
    void find_keeps_its_selection_when_assembly_loads_another_page() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path(), false, 201);
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window;
        const auto scale = window.devicePixelRatioF();
        window.resize(qRound(1366 / scale), qRound(738 / scale));
        window.show(); window.open_project_path(root);
        auto* listing = window.findChild<QWidget*>("listingPanel")->findChild<QTableView*>();
        auto* pseudocode = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        QTRY_COMPARE_WITH_TIMEOUT(listing->model()->rowCount(), 200, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(pseudocode->toPlainText().contains("/* ram:0x10ca:"), 15000);
        pseudocode->setFocus();
        QTest::keyClick(pseudocode, Qt::Key_F, Qt::ControlModifier);
        auto* query = pseudocode->findChild<QLineEdit*>("editorFindQuery");
        QVERIFY(query && query->isVisible());
        query->setText("opaque_instruction(");
        QCOMPARE(pseudocode->textCursor().selectedText(), QString("opaque_instruction("));
        QTRY_COMPARE_WITH_TIMEOUT(listing->currentIndex().data().toString(), QString("000010ca"), 10000);
        QCOMPARE(pseudocode->textCursor().selectedText(), QString("opaque_instruction("));
        QVERIFY(window.findChild<QLabel*>("functionHeading")->text().endsWith("0x10ca"));
        const auto capture_dir = qEnvironmentVariable("REWORKBENCH_UI_CAPTURE_DIR");
        if (!capture_dir.isEmpty()) {
            QVERIFY(QDir().mkpath(capture_dir));
            QCoreApplication::processEvents();
            QVERIFY(window.grab().save(capture_dir + "/navigation-find.png"));
        }
    }
    void renamed_function_rebuilds_its_documents_and_context() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto root = navigation_project(directory.path());
        if (root.isEmpty()) QSKIP("Requires the Capstone-enabled test build");
        MainWindow window; window.show(); window.open_project_path(root);
        auto* pseudocode = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
        auto* ir = window.findChild<QPlainTextEdit*>("irEditor");
        auto* heading = window.findChild<QLabel*>("functionHeading");
        QTRY_VERIFY_WITH_TIMEOUT(pseudocode->toPlainText().contains("machine_state sub_1000_at_1000"), 15000);
        auto* context = window.findChild<QPushButton*>("assistantContext");
        auto* question = window.findChild<QPlainTextEdit*>("assistantQuestion");
        auto* prepared = window.findChild<QPlainTextEdit*>("assistantPrepared");
        context->click(); question->setPlainText("Explain this function");
        window.findChild<QPushButton*>("assistantPrepare")->click();
        QVERIFY(!prepared->toPlainText().isEmpty());
        QTimer::singleShot(0, &window, [] {
            if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                dialog->setTextValue("inspect_entry"); dialog->accept();
            }
        });
        window.findChild<QAction*>("rename")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains("inspect_entry"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(pseudocode->toPlainText().contains("machine_state inspect_entry_at_1000"), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(ir->toPlainText().contains("inspect_entry"), 15000);
        QVERIFY(prepared->toPlainText().isEmpty());
        context->click(); window.findChild<QPushButton*>("assistantPrepare")->click();
        QVERIFY(prepared->toPlainText().contains("inspect_entry"));
        QVERIFY(!prepared->toPlainText().contains("Função: sub_1000"));
    }
    void function_documents_navigate_close_and_reopen() {
        using namespace reworkbench;
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto sample_path = folder.filePath("code.bin");
        QFile file(sample_path); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromHex("e801000000c3b807000000c3")); file.close();
        const auto root = folder.filePath("project");
        {
            auto project = Project::create(root);
            const auto sample = project->import_sample(sample_path);
            OpenOptions options; options.raw = true; options.raw_architecture = Architecture::X64;
            options.raw_base = 0x1000; options.entries = {{"ram", 0x1000}};
            auto snapshot = analyze_binary(project->root() + '/' + sample.relative_path, options);
            if (snapshot.functions.size() != 2) QSKIP("Requires the Capstone-enabled test build");
            snapshot.metadata.sha256 = sample.sha256;
            snapshot.metadata.original_name = sample.original_name;
            const auto analysis_id = project->save_new_analysis(sample, snapshot);
            QVERIFY(!analysis_id.isEmpty());
        }
        MainWindow window;
        auto* tabs = window.findChild<QTabBar*>("functionTabs");
        auto* tree = window.findChild<QTreeWidget*>("binaryTree");
        auto* heading = window.findChild<QLabel*>("functionHeading");
        auto* close = window.findChild<QAction*>("document_close");
        QVERIFY(tabs && tree && heading && close);
        window.show();
        window.open_project_path(root);
        QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 1, 15000);
        QTreeWidgetItem* second = nullptr;
        const auto find_second = [&] {
            for (auto* item : tree->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive))
                if (item->data(0, Qt::UserRole).toString() == "ram:0x1006") { second = item; return true; }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(find_second(), 15000);
        auto* functions_group = second->parent();
        QVERIFY(functions_group && functions_group->isExpanded());
        const auto category_rect = tree->visualItemRect(functions_group);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(category_rect.left() - tree->indentation() + 8, category_rect.center().y()));
        QVERIFY(!functions_group->isExpanded());
        QCOMPARE(tabs->count(), 1);
        tree->setCurrentItem(functions_group);
        QTest::keyClick(tree, Qt::Key_Right);
        QVERIFY(functions_group->isExpanded());
        QCOMPARE(tree->indentation(), 8);
        tree->scrollToItem(second);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualItemRect(second).center());
        QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 2, 10000);
        QVERIFY(heading->text().contains("0x1006"));
        QVERIFY(second->isSelected());
        const auto selected_row = tree->visualItemRect(second);
        QVERIFY(tree->viewport()->rect().contains(selected_row.center()));
        QVERIFY(tree->viewport()->width() > 24);
        const auto selection_pixels = tree->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!selection_pixels.isNull());
        const auto selection_dpr = selection_pixels.devicePixelRatio();
        const int selection_y = qRound(selected_row.center().y() * selection_dpr);
        const int selection_x = qRound((tree->viewport()->width() - 12) * selection_dpr);
        // The empty right side must share the selection; the outer inset stays
        // sidebar-colored. This catches a highlight confined to the branch gutter.
        QCOMPARE(selection_pixels.pixelColor(selection_x, selection_y),
            ui::theme_color(tree->hasFocus() ? "list.activeSelectionBackground" : "list.inactiveSelectionBackground"));
        QCOMPARE(selection_pixels.pixelColor(0, selection_y), ui::theme_color("sideBar.background"));
        auto* back = window.findChild<QAction*>("back");
        QVERIFY(back && back->isEnabled()); back->trigger();
        QTRY_VERIFY(heading->text().contains("0x1000"));
        window.findChild<QAction*>("forward")->trigger();
        QTRY_VERIFY(heading->text().contains("0x1006"));
        window.findChild<QAction*>("previous_document")->trigger();
        QTRY_VERIFY(heading->text().contains("0x1000"));
        window.findChild<QAction*>("next_document")->trigger();
        QTRY_VERIFY(heading->text().contains("0x1006"));
        auto* context = window.findChild<QPushButton*>("assistantContext");
        auto* question = window.findChild<QPlainTextEdit*>("assistantQuestion");
        auto* prepare = window.findChild<QPushButton*>("assistantPrepare");
        auto* prepared = window.findChild<QPlainTextEdit*>("assistantPrepared");
        QVERIFY(context && question && prepare && prepared);
        context->click();
        question->setPlainText("Explain this function"); prepare->click();
        QVERIFY(prepared->toPlainText().contains("0x1006"));
        tabs->setCurrentIndex(0);
        QTRY_VERIFY(heading->text().contains("0x1000"));
        QVERIFY(prepared->toPlainText().isEmpty());
        close->trigger();
        QCOMPARE(tabs->count(), 1);
        QTRY_VERIFY(heading->text().contains("0x1006"));
        QTimer::singleShot(0, &window, [] {
            if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
                dialog->setTextValue("ram:0x9000"); dialog->accept();
            }
        });
        window.findChild<QAction*>("jump")->trigger();
        QVERIFY(heading->text().contains("0x9000"));
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(tabs->currentIndex()).center());
        QTRY_VERIFY(heading->text().contains("0x1006"));
        QTimer::singleShot(0, &window, [&window] {
            if (auto* dialog = window.findChild<QDialog*>("quickOpen")) {
                auto* query = dialog->findChild<QLineEdit*>("quickPickerQuery");
                query->setText("1000"); QTest::keyClick(query, Qt::Key_Return);
            }
        });
        window.findChild<QAction*>("quick_open")->trigger();
        QTRY_VERIFY(heading->text().contains("0x1000"));
        auto* listing = window.findChild<QWidget*>("listingPanel")->findChild<QTableView*>();
        QTRY_VERIFY(listing->model()->rowCount() > 0);
        listing->selectRow(0);
        window.findChild<QAction*>("follow_target")->trigger();
        QTRY_VERIFY(heading->text().contains("0x1006"));
        const auto capture_dir = qEnvironmentVariable("REWORKBENCH_UI_CAPTURE_DIR");
        if (!capture_dir.isEmpty()) {
            QVERIFY(QDir().mkpath(capture_dir));
            QVERIFY(window.grab().save(QDir(capture_dir).filePath("workspace-fixture.png")));
            auto* assistant = window.findChild<QDockWidget*>("assistantDock");
            auto* editor = window.findChild<QPlainTextEdit*>("pseudocodeEditor");
            auto* palette_action = window.findChild<QAction*>("command_palette");
            auto* reset_action = window.findChild<QAction*>("layout_reset");
            auto* quick_action = window.findChild<QAction*>("quick_open");
            QVERIFY(assistant && editor && palette_action && reset_action && quick_action);
            QCOMPARE(QFontInfo(editor->font()).pixelSize(), 14);
            assistant->hide();
            QSignalSpy reset_triggered(reset_action, &QAction::triggered);
            QTimer capture_safety;
            capture_safety.setSingleShot(true);
            connect(&capture_safety, &QTimer::timeout, &window, [&window] {
                for (auto* dialog : window.findChildren<QDialog*>())
                    if (dialog->isVisible()) dialog->reject();
            });
            bool palette_found = false, palette_saved = false;
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = window.findChild<QDialog*>("commandPalette")) {
                    palette_found = true;
                    palette_saved = dialog->grab().save(QDir(capture_dir).filePath("palette-surface.png"));
                    if (auto* query = dialog->findChild<QLineEdit*>("quickPickerQuery")) {
                        query->setText(QStringLiteral("Restaurar layout"));
                        QTest::keyClick(query, Qt::Key_Return);
                    }
                    if (dialog->isVisible()) dialog->reject();
                }
            });
            capture_safety.start(2000);
            palette_action->trigger();
            capture_safety.stop();
            QVERIFY(palette_found && palette_saved);
            QCOMPARE(reset_triggered.count(), 1);
            QCOMPARE(QFontInfo(editor->font()).pixelSize(), 14);
            QVERIFY(assistant->isVisible());
            QVERIFY(!assistant->isFloating());
            QCOMPARE(window.dockWidgetArea(assistant), Qt::RightDockWidgetArea);

            bool quick_found = false, quick_saved = false;
            QTimer::singleShot(0, &window, [&] {
                if (auto* dialog = window.findChild<QDialog*>("quickOpen")) {
                    quick_found = true;
                    quick_saved = dialog->grab().save(QDir(capture_dir).filePath("quick-open-surface.png"));
                    dialog->reject();
                }
            });
            capture_safety.start(2000);
            quick_action->trigger();
            capture_safety.stop();
            QVERIFY(quick_found && quick_saved);
        }
        window.close();
    }
};
QTEST_MAIN(WorkspaceTests)
#include "ui_tests.moc"
