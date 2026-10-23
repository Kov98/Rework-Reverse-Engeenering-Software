#include "workbench_icons.hpp"
#include "workspace_theme.hpp"

#include <QFontDatabase>
#include <QFileInfo>
#include <QHash>
#include <QGlyphRun>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRawFont>
#include <QResource>
#include <QtMath>
#include <algorithm>
#include <utility>

// Keep resource initialization in the global namespace so the reference pulls
// the RCC object into applications that link the static UI library.
static void initialize_codicons_resource() {
    Q_INIT_RESOURCE(codicons);
}
static void initialize_seti_resource() {
    Q_INIT_RESOURCE(seti);
}

namespace reworkbench::ui {
namespace {

QString codicon_family() {
    static const QString family = [] {
        initialize_codicons_resource();
        const auto id = QFontDatabase::addApplicationFont(QStringLiteral(":/codicons/codicon.ttf"));
        const auto families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString{} : families.front();
    }();
    return family;
}

QString seti_family() {
    static const QString family = [] {
        initialize_seti_resource();
        const auto id = QFontDatabase::addApplicationFont(QStringLiteral(":/seti/seti.ttf"));
        const auto families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString{} : families.front();
    }();
    return family;
}

// Official @vscode/codicons 0.0.46-24 dist/codicon.css mappings.
// See assets/codicons/NOTICE.md and LICENSE-CODE for attribution and provenance.
quint16 codicon_codepoint(const QString& name) {
    static const QHash<QString, quint16> codepoints{
        {QStringLiteral("files"), 0xeaf0},
        {QStringLiteral("search"), 0xea6d},
        {QStringLiteral("type-hierarchy"), 0xebb9},
        {QStringLiteral("debug"), 0xead8},
        {QStringLiteral("sparkle"), 0xec10},
        {QStringLiteral("settings-gear"), 0xeb51},
        {QStringLiteral("folder"), 0xea83},
        {QStringLiteral("file-binary"), 0xeae8},
        {QStringLiteral("symbol-string"), 0xeb8d},
        {QStringLiteral("symbol-variable"), 0xea88},
        {QStringLiteral("symbol-method"), 0xea8c},
        {QStringLiteral("close"), 0xea76},
        {QStringLiteral("chrome-minimize"), 0xeaba},
        {QStringLiteral("chrome-maximize"), 0xeab9},
        {QStringLiteral("chrome-restore"), 0xeabb},
        {QStringLiteral("arrow-left"), 0xea9b},
        {QStringLiteral("arrow-right"), 0xea9c},
        {QStringLiteral("layout-sidebar-left"), 0xebf3},
        {QStringLiteral("layout-panel"), 0xebf2},
        {QStringLiteral("layout-sidebar-right"), 0xebf4},
        {QStringLiteral("split-horizontal"), 0xeb56},
        {QStringLiteral("ellipsis"), 0xea7c},
        {QStringLiteral("zoom-out"), 0xeb82},
        {QStringLiteral("add"), 0xea60},
        {QStringLiteral("screen-full"), 0xeb4c},
        {QStringLiteral("attach"), 0xec34},
        {QStringLiteral("copy"), 0xebcc},
        {QStringLiteral("arrow-up"), 0xeaa1},
        {QStringLiteral("comment-discussion"), 0xeac7},
        {QStringLiteral("chevron-right"), 0xeab6},
        {QStringLiteral("chevron-down"), 0xeab4},
        {QStringLiteral("symbol-class"), 0xeb5b},
        {QStringLiteral("symbol-struct"), 0xea91},
        {QStringLiteral("symbol-namespace"), 0xea8b},
        {QStringLiteral("symbol-reference"), 0xea94},
        {QStringLiteral("symbol-misc"), 0xeb63},
        {QStringLiteral("symbol-keyword"), 0xeb62},
        {QStringLiteral("export"), 0xebac},
        {QStringLiteral("references"), 0xeb36},
        {QStringLiteral("graph"), 0xeb03},
        {QStringLiteral("debug-breakpoint"), 0xea71},
        {QStringLiteral("library"), 0xeb9c},
        {QStringLiteral("chip"), 0xec19}
    };
    return codepoints.value(name);
}

class FontIconEngine final : public QIconEngine {
public:
    FontIconEngine(QString name, QString family, quint16 codepoint, QColor tint, int size, bool fit_outline = false)
        : name_(std::move(name)), family_(std::move(family)), codepoint_(codepoint),
          tint_(std::move(tint)), size_(size), fit_outline_(fit_outline) {}

    QIconEngine* clone() const override { return new FontIconEngine(*this); }
    QString key() const override { return fit_outline_ ? QStringLiteral("REWorkbenchSeti") : QStringLiteral("REWorkbenchCodicon"); }
    QString iconName() override { return name_; }
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override { return {QSize(size_, size_)}; }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        if (rect.isEmpty()) return;
        QFont font(family_);
        font.setPixelSize(std::min(rect.width(), rect.height()));
        font.setStyleStrategy(QFont::NoFontMerging);
        font.setHintingPreference(QFont::PreferNoHinting);
        const auto raw_font = QRawFont::fromFont(font);
        const auto glyphs = raw_font.glyphIndexesForString(QString(QChar(codepoint_)));
        if (glyphs.isEmpty() || !glyphs.front()) return;
        painter->save();
        painter->setClipRect(rect, Qt::IntersectClip);
        painter->setRenderHint(QPainter::TextAntialiasing);
        painter->setPen(mode == QIcon::Disabled ? theme_color("disabledForeground") : tint_);
        if (fit_outline_) {
            // Use the original font outline, fitted proportionally to the icon
            // box. Seti's font metrics differ from Codicons' 16px control grid.
            const auto outline = raw_font.pathForGlyph(glyphs.front());
            const auto bounds = outline.boundingRect();
            if (!bounds.isEmpty()) {
                // File glyphs previously touched all four edges of a 16px box.
                // Keep one logical pixel of breathing room at that size.
                const qreal inset = std::min(rect.width(), rect.height()) / 16.0;
                const auto target = QRectF(rect).adjusted(inset, inset, -inset, -inset);
                const auto scale = std::min(target.width() / bounds.width(), target.height() / bounds.height());
                painter->setRenderHint(QPainter::Antialiasing);
                painter->translate(target.center());
                painter->scale(scale, scale);
                painter->translate(-bounds.center());
                painter->fillPath(outline, painter->pen().color());
            }
        } else {
            // Center the original glyph ink, not its platform-dependent text
            // line. This font's Windows ascent is 327 units for a 300-unit em.
            const auto bounds = raw_font.boundingRect(glyphs.front());
            QGlyphRun run;
            run.setRawFont(raw_font);
            run.setGlyphIndexes(glyphs);
            run.setPositions({QPointF(0, 0)});
            painter->drawGlyphRun(QRectF(rect).center() - bounds.center(), run);
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        if (size.isEmpty() || scale <= 0) return {};
        QPixmap result(QSize(qCeil(size.width() * scale), qCeil(size.height() * scale)));
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return result;
    }

private:
    QString name_;
    QString family_;
    quint16 codepoint_;
    QColor tint_;
    int size_;
    bool fit_outline_;
};

QIcon seti_icon(const QString& name, quint16 codepoint, const char* color, int size) {
    const auto family = seti_family();
    if (family.isEmpty()) return {};
    return QIcon(new FontIconEngine(QStringLiteral("seti:") + name, family, codepoint,
        theme_color(color), std::max(1, size), true));
}

} // namespace

QIcon workbench_icon(const QString& name, const QColor& tint, int size) {
    const auto codepoint = codicon_codepoint(name);
    if (!codepoint) return {};
    const auto family = codicon_family();
    if (family.isEmpty()) return {};
    return QIcon(new FontIconEngine(name, family, codepoint,
        tint.isValid() ? tint : theme_color("icon.foreground"), std::max(1, size)));
}

QIcon file_icon(const QString& filename, int size) {
    const auto name = QFileInfo(filename).fileName().toLower();
    const auto extension = QFileInfo(name).suffix();
    struct FileGlyph { const char* name; quint16 codepoint; const char* color; };
    // Original Seti glyphs/associations. Native binary/library roles are mapped
    // below because Seti has no corresponding glyphs. See assets/seti/NOTICE.md.
    static const QHash<QString, FileGlyph> extensions{
        {"c", {"c", 0xe00c, "fileIcon.blue"}},
        {"cpp", {"cpp", 0xe01a, "fileIcon.blue"}}, {"cc", {"cpp", 0xe01a, "fileIcon.blue"}},
        {"cxx", {"cpp", 0xe01a, "fileIcon.blue"}}, {"c++", {"cpp", 0xe01a, "fileIcon.blue"}},
        {"h", {"c", 0xe00c, "fileIcon.purple"}},
        {"hpp", {"cpp", 0xe01a, "fileIcon.purple"}}, {"hh", {"cpp", 0xe01a, "fileIcon.purple"}},
        {"hxx", {"cpp", 0xe01a, "fileIcon.purple"}}, {"h++", {"cpp", 0xe01a, "fileIcon.purple"}},
        {"cs", {"c-sharp", 0xe00b, "fileIcon.blue"}},
        {"java", {"java", 0xe050, "fileIcon.red"}}, {"class", {"java", 0xe050, "fileIcon.red"}},
        {"py", {"python", 0xe07b, "fileIcon.blue"}}, {"pyw", {"python", 0xe07b, "fileIcon.blue"}},
        {"rs", {"rust", 0xe082, "fileIcon.grey"}},
        {"js", {"javascript", 0xe051, "fileIcon.yellow"}}, {"mjs", {"javascript", 0xe051, "fileIcon.yellow"}},
        {"cjs", {"javascript", 0xe051, "fileIcon.yellow"}},
        {"ts", {"typescript", 0xe099, "fileIcon.blue"}},
        {"jsx", {"react", 0xe07d, "fileIcon.blue"}}, {"tsx", {"react", 0xe07d, "fileIcon.blue"}},
        {"asm", {"asm", 0xe004, "fileIcon.red"}}, {"s", {"asm", 0xe004, "fileIcon.red"}},
        {"html", {"html", 0xe048, "fileIcon.orange"}}, {"htm", {"html", 0xe048, "fileIcon.orange"}},
        {"css", {"css", 0xe01d, "fileIcon.blue"}},
        {"json", {"json", 0xe055, "fileIcon.yellow"}}, {"jsonc", {"json", 0xe055, "fileIcon.yellow"}},
        {"xml", {"xml", 0xe0a4, "fileIcon.orange"}},
        {"db", {"db", 0xe022, "fileIcon.blue"}}, {"sqlite", {"db", 0xe022, "fileIcon.blue"}},
        {"sqlite3", {"db", 0xe022, "fileIcon.blue"}},
        {"code-workspace", {"project", 0xe076, "fileIcon.default"}}
    };
    const auto glyph = extensions.constFind(extension);
    if (glyph != extensions.cend()) return seti_icon(QString::fromLatin1(glyph->name), glyph->codepoint, glyph->color, size);
    if (extension == "dll" || extension == "so" || extension == "dylib" || extension == "a" || extension == "lib" ||
        extension == "jar" || name.contains(QStringLiteral(".so.")))
        return workbench_icon(QStringLiteral("library"), theme_color("fileIcon.default"), size);
    if (extension == "exe" || extension == "bin" || extension == "elf" || extension == "raw" ||
        extension == "o" || extension == "obj" || extension == "wasm")
        return workbench_icon(QStringLiteral("file-binary"), theme_color("fileIcon.default"), size);
    return seti_icon(QStringLiteral("default"), 0xe023, "fileIcon.default", size);
}

QIcon folder_icon(bool project, int size) {
    return project ? seti_icon(QStringLiteral("project"), 0xe076, "fileIcon.default", size)
                   : seti_icon(QStringLiteral("folder"), 0xe032, "fileIcon.default", size);
}

QIcon symbol_icon(const QString& kind, int size) {
    const auto normalized = kind.trimmed().toLower();
    if (normalized == "function" || normalized == "method" || normalized == "func")
        return workbench_icon(QStringLiteral("symbol-method"), theme_color("symbolIcon.functionForeground"), size);
    if (normalized == "class") return workbench_icon(QStringLiteral("symbol-class"), theme_color("symbolIcon.classForeground"), size);
    if (normalized == "struct" || normalized == "structure")
        return workbench_icon(QStringLiteral("symbol-struct"), theme_color("symbolIcon.structForeground"), size);
    if (normalized == "namespace" || normalized == "module")
        return workbench_icon(QStringLiteral("symbol-namespace"), theme_color("symbolIcon.namespaceForeground"), size);
    if (normalized == "variable" || normalized == "object" || normalized == "data")
        return workbench_icon(QStringLiteral("symbol-variable"), theme_color("symbolIcon.variableForeground"), size);
    if (normalized == "import")
        return workbench_icon(QStringLiteral("symbol-reference"), theme_color("symbolIcon.namespaceForeground"), size);
    if (normalized == "export")
        return workbench_icon(QStringLiteral("export"), theme_color("symbolIcon.namespaceForeground"), size);
    if (normalized == "reference" || normalized == "xref")
        return workbench_icon(QStringLiteral("references"), theme_color("symbolIcon.namespaceForeground"), size);
    if (normalized == "instruction" || normalized == "assembly")
        return workbench_icon(QStringLiteral("symbol-keyword"), theme_color("syntax.mnemonic"), size);
    if (normalized == "graph" || normalized == "cfg")
        return workbench_icon(QStringLiteral("graph"), theme_color("symbolIcon.namespaceForeground"), size);
    if (normalized == "breakpoint")
        return workbench_icon(QStringLiteral("debug-breakpoint"), theme_color("debugIcon.breakpointForeground"), size);
    if (normalized == "memory" || normalized == "region" || normalized == "section")
        return workbench_icon(QStringLiteral("chip"), theme_color("symbolIcon.variableForeground"), size);
    if (normalized == "string") return workbench_icon(QStringLiteral("symbol-string"), theme_color("symbolIcon.stringForeground"), size);
    return workbench_icon(QStringLiteral("symbol-misc"), theme_color("symbolIcon.namespaceForeground"), size);
}

} // namespace reworkbench::ui
