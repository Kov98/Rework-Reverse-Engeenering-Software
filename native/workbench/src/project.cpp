#include "reworkbench/project.hpp"
#include "serialization.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <filesystem>
#include <limits>
#include <utility>

namespace reworkbench {
namespace {

constexpr auto manifest_name = "project.rwb.json";
constexpr auto database_name = "index.sqlite3";
constexpr auto lock_name = ".reworkbench.lock";
constexpr auto application_tag = "reworkbench-independent-native";
constexpr int sqlite_application_id = 0x52574231; // RWB1
constexpr qint64 copy_chunk = 1024 * 1024;

[[noreturn]] void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }

std::filesystem::path native_path(const QString& path) {
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).constData());
#endif
}

bool same_path(const QString& first, const QString& second) {
#ifdef Q_OS_WIN
    return first.compare(second, Qt::CaseInsensitive) == 0;
#else
    return first == second;
#endif
}

bool within(const QString& path, const QString& directory) {
    const auto normalized_path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const auto normalized_directory = QDir::cleanPath(QDir::fromNativeSeparators(directory));
    const auto prefix = normalized_directory.endsWith(QLatin1Char('/')) ? normalized_directory : normalized_directory + QLatin1Char('/');
#ifdef Q_OS_WIN
    return same_path(normalized_path, normalized_directory) || normalized_path.startsWith(prefix, Qt::CaseInsensitive);
#else
    return normalized_path == normalized_directory || normalized_path.startsWith(prefix);
#endif
}

bool equivalent_files(const QString& first, const QString& second) {
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(native_path(first), native_path(second), error);
    // A missing path cannot identify an existing destination; all other errors fail closed.
    if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
        fail(QStringLiteral("Não foi possível verificar a identidade de um arquivo protegido: ") + first);
    return !error && equivalent;
}

void reject_shared_file(const QString& path) {
    if (!QFileInfo::exists(path)) return;
    std::error_code error;
    const auto count = std::filesystem::hard_link_count(native_path(path), error);
    if (error || count > 1) fail(QStringLiteral("Arquivo interno possui links adicionais ou identidade não verificável: ") + path);
}

QString internal_path(const QString& root, const QString& relative) {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QLatin1Char('\\')))
        fail(QStringLiteral("Caminho interno inválido."));
    const auto components = relative.split(QLatin1Char('/'));
    QString current = root;
    for (const auto& component : components) {
        if (component.isEmpty() || component == QStringLiteral(".") || component == QStringLiteral("..") || component.contains(QLatin1Char(':')))
            fail(QStringLiteral("Caminho interno inválido."));
        current = QDir(current).filePath(component);
        const QFileInfo info(current);
        if (info.isSymLink()) fail(QStringLiteral("Links simbólicos não são permitidos nos caminhos internos: ") + current);
        if (info.exists()) {
            const auto canonical = info.canonicalFilePath();
            if (canonical.isEmpty() || !within(canonical, root)) fail(QStringLiteral("Caminho interno escapa do projeto."));
        }
    }
    return QDir::cleanPath(current);
}

QString checked_database_path(const QString& root) {
    const auto path = internal_path(root, QLatin1String(database_name));
    reject_shared_file(path);
    for (const auto* suffix : {"-journal", "-wal", "-shm"}) {
        const auto auxiliary = internal_path(root, QLatin1String(database_name) + QLatin1String(suffix));
        reject_shared_file(auxiliary);
    }
    return path;
}

bool valid_hash(const QString& hash) {
    if (hash.size() != 64) return false;
    for (const auto character : hash)
        if (!(character >= QLatin1Char('0') && character <= QLatin1Char('9')) &&
            !(character >= QLatin1Char('a') && character <= QLatin1Char('f'))) return false;
    return true;
}

void valid_address(const Address& address) {
    if (address.space.isEmpty() || address.space.size() > 64 || address.space.contains(QChar::Null))
        fail(QStringLiteral("Espaço de endereço inválido."));
}

void valid_analysis_id(const QString& id) {
    const QUuid uuid(id);
    if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id)
        fail(QStringLiteral("Identificador de análise inválido."));
}

class Connection final {
public:
    explicit Connection(const QString& root, bool create = false)
        : name_(QStringLiteral("reworkbench-") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        const auto path = checked_database_path(root);
        if (!create && !QFileInfo(path).isFile()) fail(QStringLiteral("Índice SQLite do projeto ausente."));
        database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        database_.setDatabaseName(path);
        database_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!database_.open()) {
            const auto detail = database_.lastError().text();
            cleanup();
            fail(QStringLiteral("Não foi possível abrir o índice SQLite (driver Qt QSQLITE necessário): ") + detail);
        }
        try {
            QSqlQuery query(database_);
            if (!query.exec(QStringLiteral("PRAGMA foreign_keys=ON"))) fail(query.lastError().text());
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~Connection() { cleanup(); }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    QSqlDatabase& database() { return database_; }
private:
    void cleanup() {
        database_.close();
        database_ = QSqlDatabase{};
        QSqlDatabase::removeDatabase(name_);
    }
    QString name_;
    QSqlDatabase database_;
};

void execute(QSqlDatabase& database, const QString& statement) {
    QSqlQuery query(database);
    if (!query.exec(statement)) fail(QStringLiteral("Falha no índice SQLite: ") + query.lastError().text());
}

void prepare(QSqlQuery& query, const QString& statement) {
    if (!query.prepare(statement)) fail(QStringLiteral("Falha ao preparar acesso ao índice: ") + query.lastError().text());
}

void execute(QSqlQuery& query) {
    if (!query.exec()) fail(QStringLiteral("Falha no índice SQLite: ") + query.lastError().text());
}

class Transaction final {
public:
    explicit Transaction(QSqlDatabase& database) : database_(database) {
        if (!database_.transaction()) fail(QStringLiteral("Não foi possível iniciar uma transação: ") + database_.lastError().text());
    }
    ~Transaction() { if (!committed_) database_.rollback(); }
    void commit() {
        if (!database_.commit()) fail(QStringLiteral("Não foi possível confirmar a transação: ") + database_.lastError().text());
        committed_ = true;
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    QSqlDatabase& database_;
    bool committed_ = false;
};

void check_database_schema(QSqlDatabase& database) {
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) fail(QStringLiteral("Índice SQLite ilegível."));
    const auto version = query.value(0).toInt();
    query.finish();
    if (version > 1) fail(QStringLiteral("O índice foi criado por uma versão mais recente do Reworkbench."));
    if (version != 1) fail(QStringLiteral("Versão do índice SQLite não suportada."));
    if (!query.exec(QStringLiteral("PRAGMA application_id")) || !query.next() || query.value(0).toInt() != sqlite_application_id)
        fail(QStringLiteral("O índice SQLite não pertence ao Reworkbench nativo."));
    query.finish();
    // Force column/schema validation before exposing the project to the UI.
    execute(database, QStringLiteral("SELECT s.sha256,s.relative_path,s.size,o.path,o.original_name,a.id,a.name,a.original_path,a.state,a.revision,a.snapshot "
        "FROM samples s LEFT JOIN sample_origins o ON s.sha256=o.sha256 LEFT JOIN analyses a ON s.sha256=a.sha256 LIMIT 0"));
    execute(database, QStringLiteral("SELECT n.analysis_id,n.address_space,n.address,n.name,f.address FROM function_names n "
        "LEFT JOIN function_addresses f ON f.analysis_id=n.analysis_id LIMIT 0"));
    execute(database, QStringLiteral("SELECT analysis_id,address_space,address,text,bookmarked FROM notes LIMIT 0"));
}

void create_database_schema(QSqlDatabase& database) {
    Transaction transaction(database);
    execute(database, QStringLiteral("CREATE TABLE samples(sha256 TEXT PRIMARY KEY,relative_path TEXT NOT NULL UNIQUE,size TEXT NOT NULL,created_utc TEXT NOT NULL)"));
    execute(database, QStringLiteral("CREATE TABLE sample_origins(sha256 TEXT NOT NULL REFERENCES samples(sha256),path TEXT NOT NULL,original_name TEXT NOT NULL,"
        "PRIMARY KEY(sha256,path))"));
    execute(database, QStringLiteral("CREATE TABLE analyses(id TEXT PRIMARY KEY,name TEXT NOT NULL,sha256 TEXT NOT NULL REFERENCES samples(sha256),"
        "original_path TEXT NOT NULL,state TEXT NOT NULL,revision TEXT NOT NULL,snapshot BLOB NOT NULL,created_utc TEXT NOT NULL,"
        "FOREIGN KEY(sha256,original_path) REFERENCES sample_origins(sha256,path))"));
    execute(database, QStringLiteral("CREATE INDEX analyses_sample ON analyses(sha256)"));
    execute(database, QStringLiteral("CREATE TABLE function_addresses(analysis_id TEXT NOT NULL REFERENCES analyses(id) ON DELETE CASCADE,"
        "address_space TEXT NOT NULL,address TEXT NOT NULL,PRIMARY KEY(analysis_id,address_space,address))"));
    execute(database, QStringLiteral("CREATE TABLE function_names(analysis_id TEXT NOT NULL,address_space TEXT NOT NULL,address TEXT NOT NULL,name TEXT NOT NULL,"
        "PRIMARY KEY(analysis_id,address_space,address),FOREIGN KEY(analysis_id,address_space,address) REFERENCES function_addresses(analysis_id,address_space,address) ON DELETE CASCADE)"));
    execute(database, QStringLiteral("CREATE TABLE notes(analysis_id TEXT NOT NULL REFERENCES analyses(id) ON DELETE CASCADE,address_space TEXT NOT NULL,"
        "address TEXT NOT NULL,text TEXT NOT NULL,bookmarked INTEGER NOT NULL CHECK(bookmarked IN (0,1)),PRIMARY KEY(analysis_id,address_space,address))"));
    execute(database, QStringLiteral("PRAGMA application_id=") + QString::number(sqlite_application_id));
    execute(database, QStringLiteral("PRAGMA user_version=1"));
    transaction.commit();
}

QByteArray hash_file(QFile& input, std::stop_token stop, QSaveFile* output = nullptr, quint64* size = nullptr) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    quint64 count = 0;
    while (true) {
        check_cancelled(stop);
        const QByteArray chunk = input.read(copy_chunk);
        if (chunk.isEmpty()) {
            if (input.error() != QFileDevice::NoError) fail(QStringLiteral("Falha ao ler arquivo: ") + input.errorString());
            break;
        }
        hash.addData(chunk);
        count += quint64(chunk.size());
        if (output && output->write(chunk) != chunk.size()) fail(QStringLiteral("Falha ao gravar cópia da amostra: ") + output->errorString());
    }
    if (size) *size = count;
    return hash.result().toHex();
}

void atomic_bytes(const QString& destination, const QByteArray& bytes, std::stop_token stop = {}) {
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) fail(QStringLiteral("Não foi possível abrir a exportação: ") + output.errorString());
    for (qsizetype position = 0; position < bytes.size(); position += copy_chunk) {
        check_cancelled(stop);
        const auto length = qMin<qsizetype>(copy_chunk, bytes.size() - position);
        if (output.write(bytes.constData() + position, length) != length) fail(QStringLiteral("Falha ao gravar arquivo: ") + output.errorString());
    }
    check_cancelled(stop);
    if (!output.commit()) fail(QStringLiteral("Não foi possível confirmar o arquivo: ") + output.errorString());
}

void check_source_unchanged(const QString& path, const QFileInfo& before) {
    const QFileInfo after(path);
    if (!after.isFile() || after.size() != before.size() || after.lastModified() != before.lastModified() ||
        !same_path(after.canonicalFilePath(), before.canonicalFilePath()))
        fail(QStringLiteral("O arquivo mudou durante a leitura; a operação foi interrompida."));
}

void verify_sample(const QString& path, const QString& expected_hash, quint64 expected_size,
                   std::stop_token stop) {
    check_cancelled(stop);
    const QFileInfo before(path);
    reject_shared_file(path);
    if (!valid_hash(expected_hash) || !before.isFile() || before.size() < 0 || quint64(before.size()) != expected_size)
        fail(QStringLiteral("Cópia de amostra ausente ou com tamanho divergente."));
    QFile input(path);
    quint64 actual_size = 0;
    if (!input.open(QIODevice::ReadOnly)) fail(QStringLiteral("Não foi possível verificar a cópia da amostra."));
    const auto actual_hash = hash_file(input, stop, nullptr, &actual_size);
    input.close();
    check_source_unchanged(path, before);
    if (actual_size != expected_size || QString::fromLatin1(actual_hash) != expected_hash)
        fail(QStringLiteral("A cópia da amostra está corrompida: SHA-256 divergente. A análise não será carregada nem exportada."));
    check_cancelled(stop);
}

void require_analysis(QSqlDatabase& database, const QString& id) {
    valid_analysis_id(id);
    QSqlQuery query(database);
    prepare(query, QStringLiteral("SELECT 1 FROM analyses WHERE id=?"));
    query.addBindValue(id);
    execute(query);
    if (!query.next()) fail(QStringLiteral("Análise inexistente neste projeto."));
}

quint64 bump_revision(QSqlDatabase& database, const QString& id) {
    QSqlQuery read(database);
    prepare(read, QStringLiteral("SELECT revision FROM analyses WHERE id=?"));
    read.addBindValue(id);
    execute(read);
    if (!read.next()) fail(QStringLiteral("Análise inexistente neste projeto."));
    const auto previous = persistence::unsigned_hex(read.value(0).toString());
    read.finish();
    if (previous == std::numeric_limits<quint64>::max()) fail(QStringLiteral("Limite de revisões atingido."));
    QSqlQuery update(database);
    prepare(update, QStringLiteral("UPDATE analyses SET revision=? WHERE id=?"));
    update.addBindValue(hex(previous + 1));
    update.addBindValue(id);
    execute(update);
    return previous + 1;
}

void annotation_budget(QSqlDatabase& database, const QString& id, bool function_names,
                       std::stop_token stop = {}) {
    // Ask SQLite for lengths and types before the driver materializes any user
    // text. Individual and aggregate budgets also apply to our own writes.
    QSqlQuery query(database);
    query.setForwardOnly(true);
    const auto sql = function_names
        ? QStringLiteral("SELECT length(CAST(address_space AS BLOB)),length(CAST(address AS BLOB)),length(CAST(name AS BLOB)),typeof(address_space),typeof(address),typeof(name),1 FROM function_names WHERE analysis_id=?")
        : QStringLiteral("SELECT length(CAST(address_space AS BLOB)),length(CAST(address AS BLOB)),length(CAST(text AS BLOB)),typeof(address_space),typeof(address),typeof(text),CASE WHEN typeof(bookmarked)='integer' AND bookmarked IN(0,1) THEN 1 ELSE 0 END FROM notes WHERE analysis_id=?");
    prepare(query, sql);
    query.addBindValue(id);
    execute(query);
    const qint64 maximum_rows = function_names ? 10000 : 200000;
    const qint64 maximum_field = function_names ? 3 * 4096 : 3 * 1024 * 1024;
    const qint64 maximum_total = function_names ? 16 * 1024 * 1024 : 32 * 1024 * 1024;
    qint64 rows = 0, total = 0;
    while (query.next()) {
        check_cancelled(stop);
        if (++rows > maximum_rows) fail(QStringLiteral("O projeto excedeu o limite de anotações."));
        for (int field = 0; field < 3; ++field) {
            if (query.value(field + 3).toString() != QStringLiteral("text")) fail(QStringLiteral("Tipo de anotação inválido no índice."));
            bool valid = false;
            const auto size = query.value(field).toLongLong(&valid);
            const qint64 maximum = field == 0 ? 3 * 64 : field == 1 ? 18 : maximum_field;
            if (!valid || size < 0 || size > maximum || size > maximum_total - total)
                fail(QStringLiteral("Anotações excedem o limite individual ou total de texto."));
            total += size;
        }
        if (query.value(6).toInt() != 1) fail(QStringLiteral("Estado de marcador inválido no índice."));
    }
    if (query.lastError().isValid()) fail(query.lastError().text());
}

AnalysisSnapshot load_snapshot(const QString& root, QSqlDatabase& database, const QString& id, std::stop_token stop) {
    valid_analysis_id(id);
    check_cancelled(stop);
    QSqlQuery query(database);
    prepare(query, QStringLiteral("SELECT length(a.snapshot),a.revision,a.sha256,a.state,s.relative_path,s.size,o.original_name "
        "FROM analyses a JOIN samples s ON s.sha256=a.sha256 JOIN sample_origins o ON o.sha256=a.sha256 AND o.path=a.original_path WHERE a.id=?"));
    query.addBindValue(id);
    execute(query);
    if (!query.next()) fail(QStringLiteral("Análise inexistente neste projeto."));
    // SQLite can report BLOB length without transferring the BLOB to the Qt driver.
    const auto length = query.value(0).toLongLong();
    if (query.value(0).isNull() || length <= 0 || length > persistence::max_snapshot_bytes)
        fail(QStringLiteral("Snapshot vazio ou acima do limite de armazenamento."));
    const auto revision = persistence::unsigned_hex(query.value(1).toString());
    const auto sha256 = query.value(2).toString();
    const auto state = query.value(3).toString();
    const auto relative = query.value(4).toString();
    const auto sample_size = persistence::unsigned_hex(query.value(5).toString());
    const auto original_name = query.value(6).toString();
    if (!valid_hash(sha256) || relative != QStringLiteral("samples/") + sha256 + QStringLiteral(".bin"))
        fail(QStringLiteral("Caminho de amostra inválido no índice."));
    const auto sample_path = internal_path(root, relative);
    reject_shared_file(sample_path);
    if (!QFileInfo(sample_path).isFile() || quint64(QFileInfo(sample_path).size()) != sample_size)
        fail(QStringLiteral("Cópia de amostra ausente ou com tamanho divergente."));
    query.finish();
    verify_sample(sample_path, sha256, sample_size, stop);
    prepare(query, QStringLiteral("SELECT snapshot FROM analyses WHERE id=?"));
    query.addBindValue(id);
    execute(query);
    if (!query.next()) fail(QStringLiteral("Análise inexistente neste projeto."));
    const auto blob = query.value(0).toByteArray();
    query.finish();
    if (blob.size() != length) fail(QStringLiteral("Tamanho do snapshot diverge do índice."));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(blob, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) fail(QStringLiteral("Snapshot JSON inválido: ") + error.errorString());
    auto snapshot = persistence::snapshot_from_json(document.object(), stop);
    if (snapshot.metadata.sha256 != sha256 || snapshot.metadata.size != sample_size || snapshot.state != state)
        fail(QStringLiteral("Metadados do snapshot divergem do índice."));
    // source_path always names the actual bytes, rebased when a project folder moves.
    // Original provenance is persisted separately and never used to open the analyzed file.
    snapshot.metadata.source_path = sample_path;
    snapshot.metadata.original_name = original_name;
    snapshot.revision = revision;
    annotation_budget(database, id, true, stop);
    QSqlQuery names(database);
    prepare(names, QStringLiteral("SELECT address_space,address,name FROM function_names WHERE analysis_id=?"));
    names.addBindValue(id);
    execute(names);
    QHash<QString, QString> overlays;
    while (names.next()) {
        check_cancelled(stop);
        const Address address{names.value(0).toString(), persistence::unsigned_hex(names.value(1).toString())};
        valid_address(address);
        const auto name = names.value(2).toString();
        if (name.isEmpty() || name.size() > 4096 || name.contains(QChar::Null) || name.contains(QLatin1Char('\n')) || name.contains(QLatin1Char('\r')))
            fail(QStringLiteral("Nome de função inválido no índice."));
        overlays.insert(address.space + QChar::Null + hex(address.value), name);
    }
    for (auto& function : snapshot.functions) {
        check_cancelled(stop);
        const auto found = overlays.constFind(function.address.space + QChar::Null + hex(function.address.value));
        if (found != overlays.cend()) function.name = found.value();
    }
    return snapshot;
}

QVector<Note> load_notes(QSqlDatabase& database, const QString& id, std::stop_token stop = {}) {
    require_analysis(database, id);
    annotation_budget(database, id, false, stop);
    QSqlQuery query(database);
    prepare(query, QStringLiteral("SELECT address_space,address,text,bookmarked FROM notes WHERE analysis_id=? ORDER BY address_space,length(address),address"));
    query.addBindValue(id);
    execute(query);
    QVector<Note> result;
    while (query.next()) {
        check_cancelled(stop);
        if (result.size() >= 200000) fail(QStringLiteral("O projeto excedeu o limite de notas."));
        Note note{{query.value(0).toString(), persistence::unsigned_hex(query.value(1).toString())}, query.value(2).toString(), query.value(3).toBool()};
        valid_address(note.address);
        if (note.text.size() > 1024 * 1024 || note.text.contains(QChar::Null)) fail(QStringLiteral("Nota inválida ou acima do limite de texto."));
        result.append(std::move(note));
    }
    return result;
}

QString protected_export_path(const QString& root, QSqlDatabase& database, const QString& destination, std::stop_token stop = {}) {
    check_cancelled(stop);
    if (destination.isEmpty() || destination.contains(QChar::Null)) fail(QStringLiteral("Destino da exportação inválido."));
    const QFileInfo requested(QDir::cleanPath(QFileInfo(destination).absoluteFilePath()));
    if (requested.fileName().isEmpty() || requested.fileName().contains(QLatin1Char(':')) || requested.isDir() ||
        requested.isSymLink() || (requested.exists() && !requested.isFile()))
        fail(QStringLiteral("Escolha um arquivo regular para exportar."));
    const QFileInfo parent(requested.absolutePath());
    if (!parent.isDir() || parent.canonicalFilePath().isEmpty()) fail(QStringLiteral("A pasta de destino não existe."));
    const auto path = QDir(parent.canonicalFilePath()).filePath(requested.fileName());
    const auto resolved = requested.exists() ? requested.canonicalFilePath() : path;
    const auto exports = internal_path(root, QStringLiteral("exports"));
    if (resolved.isEmpty() || (within(resolved, root) && !within(resolved, exports)))
        fail(QStringLiteral("A exportação não pode substituir arquivos internos do projeto. Use a pasta exports."));
    // Canonical names detect aliases/symlinks; equivalent() also detects hard links.
    // Scan internal files outside exports so aliases of journals and imported copies are covered.
    if (requested.exists()) {
        QDirIterator files(root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            check_cancelled(stop);
            const auto protected_file = files.next();
            if (!within(protected_file, exports) && equivalent_files(path, protected_file))
                fail(QStringLiteral("O destino é um link para um arquivo interno protegido."));
        }
    }
    QSqlQuery origins(database);
    if (!origins.exec(QStringLiteral("SELECT path FROM sample_origins"))) fail(origins.lastError().text());
    while (origins.next()) {
        check_cancelled(stop);
        const auto original = origins.value(0).toString();
        const QFileInfo info(original);
        if (same_path(path, QDir::cleanPath(info.absoluteFilePath())) ||
            (info.exists() && (same_path(resolved, info.canonicalFilePath()) || (requested.exists() && equivalent_files(path, original)))))
            fail(QStringLiteral("A exportação não pode substituir a origem de uma amostra nem seus links."));
    }
    return path;
}

} // namespace

Project::Project(QString root) : root_(std::move(root)) {}
Project::~Project() = default;

std::shared_ptr<Project> Project::create(const QString& root) {
    auto project = std::shared_ptr<Project>(new Project(root));
    project->initialize(true);
    return project;
}

std::shared_ptr<Project> Project::open(const QString& root) {
    auto project = std::shared_ptr<Project>(new Project(root));
    project->initialize(false);
    return project;
}

void Project::initialize(bool create) {
    std::lock_guard guard(mutex_);
    if (root_.isEmpty() || root_.contains(QChar::Null)) fail(QStringLiteral("Pasta do projeto inválida."));
    root_ = QFileInfo(root_).absoluteFilePath();
    if (create && !QDir().mkpath(root_)) fail(QStringLiteral("Não foi possível criar a pasta do projeto."));
    const QFileInfo directory(root_);
    if (!directory.isDir() || directory.canonicalFilePath().isEmpty()) fail(QStringLiteral("A pasta do projeto não existe."));
    root_ = directory.canonicalFilePath();
    const auto manifest_path = internal_path(root_, QLatin1String(manifest_name));
    if (QFileInfo::exists(QDir(root_).filePath(QStringLiteral("project.json"))))
        fail(QStringLiteral("Projeto legado Python/Ghidra detectado (project.json). Ele não pode ser aberto nem convertido silenciosamente pelo motor nativo."));
    if (create && !QDir(root_).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
        fail(QStringLiteral("Crie o projeto nativo em uma pasta vazia."));
    if (!create && !QFileInfo(manifest_path).isFile()) fail(QStringLiteral("Manifesto project.rwb.json ausente; esta pasta não é um projeto nativo."));

    lock_ = std::make_unique<QLockFile>(internal_path(root_, QLatin1String(lock_name)));
    lock_->setStaleLockTime(0); // Do not expire a legitimate long-running analysis by age.
    if (!lock_->tryLock(0)) {
        qint64 pid = 0;
        QString host, application;
        if (lock_->getLockInfo(&pid, &host, &application))
            fail(QStringLiteral("Projeto já está aberto por %1 (PID %2, máquina %3). Feche a outra instância.").arg(application).arg(pid).arg(host));
        fail(QStringLiteral("Não foi possível bloquear o projeto; outra instância ou permissões da pasta impedem a abertura."));
    }

    if (create) {
        for (const auto* name : {"samples", "exports"})
            if (!QDir().mkpath(internal_path(root_, QLatin1String(name)))) fail(QStringLiteral("Não foi possível criar as pastas internas."));
        Connection connection(root_, true);
        create_database_schema(connection.database());
        const QJsonObject manifest{{"application", QLatin1String(application_tag)}, {"schema", 1},
            {"project_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"created_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"database", QLatin1String(database_name)}, {"samples", QStringLiteral("samples")}, {"exports", QStringLiteral("exports")}};
        atomic_bytes(manifest_path, QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    } else {
        reject_shared_file(manifest_path);
        QFile manifest_file(manifest_path);
        if (!manifest_file.open(QIODevice::ReadOnly) || manifest_file.size() > 64 * 1024) fail(QStringLiteral("Manifesto nativo ilegível ou excessivo."));
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(manifest_file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) fail(QStringLiteral("Manifesto nativo JSON inválido."));
        const auto manifest = document.object();
        const auto schema = manifest.value(QStringLiteral("schema"));
        if (manifest.value(QStringLiteral("application")).toString() != QLatin1String(application_tag)) fail(QStringLiteral("O manifesto não pertence ao Reworkbench independente."));
        if (schema.isDouble() && schema.toDouble() > 1) fail(QStringLiteral("Este projeto requer uma versão mais recente do Reworkbench."));
        if (!schema.isDouble() || schema.toDouble() != 1) fail(QStringLiteral("Versão de manifesto não suportada."));
        if (manifest.value(QStringLiteral("database")).toString() != QLatin1String(database_name) ||
            manifest.value(QStringLiteral("samples")).toString() != QStringLiteral("samples") ||
            manifest.value(QStringLiteral("exports")).toString() != QStringLiteral("exports") ||
            QUuid(manifest.value(QStringLiteral("project_id")).toString()).isNull())
            fail(QStringLiteral("Estrutura do manifesto nativo inválida."));
        for (const auto* name : {"samples", "exports"})
            if (!QFileInfo(internal_path(root_, QLatin1String(name))).isDir()) fail(QStringLiteral("Pasta interna do projeto ausente."));
        if (!QFileInfo(checked_database_path(root_)).isFile()) fail(QStringLiteral("Índice SQLite do projeto ausente."));
        Connection connection(root_);
        check_database_schema(connection.database());
        QSqlQuery samples(connection.database());
        if (!samples.exec(QStringLiteral("SELECT sha256,relative_path,size FROM samples"))) fail(samples.lastError().text());
        while (samples.next()) {
            const auto hash = samples.value(0).toString();
            const auto relative = samples.value(1).toString();
            if (!valid_hash(hash) || relative != QStringLiteral("samples/") + hash + QStringLiteral(".bin")) fail(QStringLiteral("Caminho de amostra inválido no índice."));
            const auto file = internal_path(root_, relative);
            if (!QFileInfo(file).isFile() || quint64(QFileInfo(file).size()) != persistence::unsigned_hex(samples.value(2).toString()))
                fail(QStringLiteral("Cópia de amostra ausente ou com tamanho divergente."));
            reject_shared_file(file);
        }
    }
}

Sample Project::import_sample(const QString& source, std::stop_token stop) {
    std::lock_guard guard(mutex_);
    check_cancelled(stop);
    if (source.isEmpty() || source.contains(QChar::Null)) fail(QStringLiteral("Caminho de origem inválido."));
    const auto source_path = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    const QFileInfo before(source_path);
    if (!before.isFile() || before.canonicalFilePath().isEmpty() || before.size() < 0)
        fail(QStringLiteral("A origem deve ser um arquivo regular legível."));
    if (within(before.canonicalFilePath(), root_) && !within(before.canonicalFilePath(), internal_path(root_, QStringLiteral("exports"))))
        fail(QStringLiteral("Arquivos internos do projeto não podem ser usados como origem. Selecione o binário original."));
    QFile first(source_path);
    if (!first.open(QIODevice::ReadOnly)) fail(QStringLiteral("Não foi possível abrir a origem: ") + first.errorString());
    quint64 size = 0;
    const auto digest = hash_file(first, stop, nullptr, &size);
    first.close();
    check_source_unchanged(source_path, before);
    if (size != quint64(before.size())) fail(QStringLiteral("Tamanho da origem mudou durante a importação."));
    Sample sample{QString::fromLatin1(digest), QStringLiteral("samples/") + QString::fromLatin1(digest) + QStringLiteral(".bin"),
                  source_path, before.fileName(), size};
    const auto target = internal_path(root_, sample.relative_path);
    reject_shared_file(target);
    const bool exists = QFileInfo::exists(target);
    if (exists) {
        QFile stored(target);
        quint64 stored_size = 0;
        if (!stored.open(QIODevice::ReadOnly) || hash_file(stored, stop, nullptr, &stored_size) != digest || stored_size != size)
            fail(QStringLiteral("A cópia existente da amostra está corrompida; ela não será substituída."));
    }
    // The first pass establishes the address; this second pass verifies exactly the bytes copied.
    QFile second(source_path);
    if (!second.open(QIODevice::ReadOnly)) fail(QStringLiteral("Não foi possível reler a origem."));
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!exists && !output.open(QIODevice::WriteOnly)) fail(QStringLiteral("Não foi possível criar cópia da amostra: ") + output.errorString());
    quint64 copied = 0;
    const auto second_digest = hash_file(second, stop, exists ? nullptr : &output, &copied);
    second.close();
    check_source_unchanged(source_path, before);
    if (second_digest != digest || copied != size) fail(QStringLiteral("O conteúdo de origem mudou durante a importação."));
    check_cancelled(stop);
    if (!exists && !output.commit()) fail(QStringLiteral("Não foi possível confirmar a cópia da amostra: ") + output.errorString());

    Connection connection(root_);
    auto& database = connection.database();
    Transaction transaction(database);
    QSqlQuery insert(database);
    prepare(insert, QStringLiteral("INSERT INTO samples(sha256,relative_path,size,created_utc) VALUES(?,?,?,?) ON CONFLICT(sha256) DO NOTHING"));
    insert.addBindValue(sample.sha256);
    insert.addBindValue(sample.relative_path);
    insert.addBindValue(hex(sample.size));
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    execute(insert);
    QSqlQuery check(database);
    prepare(check, QStringLiteral("SELECT relative_path,size FROM samples WHERE sha256=?"));
    check.addBindValue(sample.sha256);
    execute(check);
    if (!check.next() || check.value(0).toString() != sample.relative_path || persistence::unsigned_hex(check.value(1).toString()) != sample.size)
        fail(QStringLiteral("Metadados existentes da amostra divergem da cópia."));
    check.finish();
    QSqlQuery origin(database);
    prepare(origin, QStringLiteral("INSERT INTO sample_origins(sha256,path,original_name) VALUES(?,?,?) ON CONFLICT(sha256,path) DO NOTHING"));
    origin.addBindValue(sample.sha256);
    origin.addBindValue(sample.original_path);
    origin.addBindValue(sample.original_name);
    execute(origin);
    check_cancelled(stop);
    transaction.commit(); // Provenance survives a subsequent failed/cancelled analysis.
    return sample;
}

QVector<AnalysisSummary> Project::analyses() const {
    std::lock_guard guard(mutex_);
    Connection connection(root_);
    QSqlQuery query(connection.database());
    if (!query.exec(QStringLiteral("SELECT id,name,sha256,state,revision FROM analyses ORDER BY created_utc,id"))) fail(query.lastError().text());
    QVector<AnalysisSummary> result;
    while (query.next()) {
        const auto id = query.value(0).toString();
        valid_analysis_id(id);
        result.append(AnalysisSummary{id, query.value(1).toString(), query.value(2).toString(), query.value(3).toString(), persistence::unsigned_hex(query.value(4).toString())});
    }
    return result;
}

QString Project::save_new_analysis(const Sample& sample, const AnalysisSnapshot& snapshot, std::stop_token stop) {
    std::lock_guard guard(mutex_);
    check_cancelled(stop);
    if (!valid_hash(sample.sha256) || sample.relative_path != QStringLiteral("samples/") + sample.sha256 + QStringLiteral(".bin"))
        fail(QStringLiteral("Amostra inválida para este projeto."));
    const auto path = internal_path(root_, sample.relative_path);
    reject_shared_file(path);
    if (!QFileInfo(path).isFile() || quint64(QFileInfo(path).size()) != sample.size) fail(QStringLiteral("Cópia da amostra ausente ou alterada."));
    verify_sample(path, sample.sha256, sample.size, stop);
    Connection connection(root_);
    auto& database = connection.database();
    Transaction transaction(database);
    QSqlQuery provenance(database);
    prepare(provenance, QStringLiteral("SELECT s.relative_path,s.size,o.original_name FROM samples s JOIN sample_origins o ON o.sha256=s.sha256 WHERE s.sha256=? AND o.path=?"));
    provenance.addBindValue(sample.sha256);
    provenance.addBindValue(sample.original_path);
    execute(provenance);
    if (!provenance.next() || provenance.value(0).toString() != sample.relative_path ||
        persistence::unsigned_hex(provenance.value(1).toString()) != sample.size || provenance.value(2).toString() != sample.original_name)
        fail(QStringLiteral("Importe a amostra neste projeto antes de persistir a análise."));
    provenance.finish();
    auto stored = snapshot;
    stored.metadata.source_path = path;
    stored.metadata.original_name = sample.original_name;
    stored.metadata.sha256 = sample.sha256;
    stored.metadata.size = sample.size;
    stored.revision = 0;
    auto stored_json = persistence::snapshot_json(stored, stop);
    stored_json.insert(QStringLiteral("provenance"), QJsonObject{{"original_path", sample.original_path},
        {"original_name", sample.original_name}, {"stored_relative_path", sample.relative_path}, {"sha256", sample.sha256}, {"size", hex(sample.size)}});
    const auto blob = QJsonDocument(stored_json).toJson(QJsonDocument::Compact);
    if (blob.size() > persistence::max_snapshot_bytes) fail(QStringLiteral("Snapshot excede 512 MiB; reduza os limites de análise."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery insert(database);
    prepare(insert, QStringLiteral("INSERT INTO analyses(id,name,sha256,original_path,state,revision,snapshot,created_utc) VALUES(?,?,?,?,?,?,?,?)"));
    insert.addBindValue(id);
    insert.addBindValue(sample.original_name);
    insert.addBindValue(sample.sha256);
    insert.addBindValue(sample.original_path);
    insert.addBindValue(stored.state);
    insert.addBindValue(hex(0));
    insert.addBindValue(blob);
    insert.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    execute(insert);
    QSqlQuery function(database);
    prepare(function, QStringLiteral("INSERT INTO function_addresses(analysis_id,address_space,address) VALUES(?,?,?)"));
    for (const auto& value : stored.functions) {
        check_cancelled(stop);
        valid_address(value.address);
        function.bindValue(0, id);
        function.bindValue(1, value.address.space);
        function.bindValue(2, hex(value.address.value));
        execute(function);
    }
    check_cancelled(stop);
    transaction.commit();
    return id;
}

AnalysisSnapshot Project::load_analysis(const QString& analysis_id, std::stop_token stop) const {
    std::lock_guard guard(mutex_);
    Connection connection(root_);
    return load_snapshot(root_, connection.database(), analysis_id, stop);
}

void Project::rename_function(const QString& analysis_id, Address address, const QString& name) {
    std::lock_guard guard(mutex_);
    valid_analysis_id(analysis_id);
    valid_address(address);
    const auto clean_name = name.trimmed();
    if (clean_name.isEmpty() || clean_name.size() > 4096 || clean_name.contains(QChar::Null) || clean_name.contains(QLatin1Char('\n')) || clean_name.contains(QLatin1Char('\r')))
        fail(QStringLiteral("Nome de função inválido (até 4096 caracteres, em uma linha)."));
    Connection connection(root_);
    auto& database = connection.database();
    Transaction transaction(database);
    require_analysis(database, analysis_id);
    QSqlQuery query(database);
    prepare(query, QStringLiteral("INSERT INTO function_names(analysis_id,address_space,address,name) VALUES(?,?,?,?) "
        "ON CONFLICT(analysis_id,address_space,address) DO UPDATE SET name=excluded.name"));
    query.addBindValue(analysis_id);
    query.addBindValue(address.space);
    query.addBindValue(hex(address.value));
    query.addBindValue(clean_name);
    execute(query); // Composite FK rejects renaming a nonexistent function.
    query.finish();
    annotation_budget(database, analysis_id, true);
    bump_revision(database, analysis_id);
    transaction.commit();
}

void Project::set_note(const QString& analysis_id, const Note& note) {
    std::lock_guard guard(mutex_);
    valid_address(note.address);
    if (note.text.size() > 1024 * 1024 || note.text.contains(QChar::Null)) fail(QStringLiteral("Nota inválida ou maior que 1 MiB de caracteres."));
    Connection connection(root_);
    auto& database = connection.database();
    Transaction transaction(database);
    require_analysis(database, analysis_id);
    QSqlQuery query(database);
    if (note.text.isEmpty() && !note.bookmarked) {
        prepare(query, QStringLiteral("DELETE FROM notes WHERE analysis_id=? AND address_space=? AND address=?"));
        query.addBindValue(analysis_id);
        query.addBindValue(note.address.space);
        query.addBindValue(hex(note.address.value));
    } else {
        QSqlQuery count(database);
        prepare(count, QStringLiteral("SELECT count(*),coalesce(max(CASE WHEN address_space=? AND address=? THEN 1 ELSE 0 END),0) FROM notes WHERE analysis_id=?"));
        count.addBindValue(note.address.space);
        count.addBindValue(hex(note.address.value));
        count.addBindValue(analysis_id);
        execute(count);
        if (!count.next() || (count.value(0).toLongLong() >= 200000 && count.value(1).toInt() == 0)) fail(QStringLiteral("Limite de notas atingido."));
        count.finish();
        prepare(query, QStringLiteral("INSERT INTO notes(analysis_id,address_space,address,text,bookmarked) VALUES(?,?,?,?,?) "
            "ON CONFLICT(analysis_id,address_space,address) DO UPDATE SET text=excluded.text,bookmarked=excluded.bookmarked"));
        query.addBindValue(analysis_id);
        query.addBindValue(note.address.space);
        query.addBindValue(hex(note.address.value));
        query.addBindValue(note.text.isNull() ? QStringLiteral("") : note.text);
        query.addBindValue(note.bookmarked ? 1 : 0);
    }
    execute(query);
    query.finish();
    annotation_budget(database, analysis_id, false);
    bump_revision(database, analysis_id);
    transaction.commit();
}

QVector<Note> Project::notes(const QString& analysis_id) const {
    std::lock_guard guard(mutex_);
    Connection connection(root_);
    return load_notes(connection.database(), analysis_id);
}

void Project::export_report(const QString& analysis_id, const QString& destination, std::stop_token stop) const {
    std::lock_guard guard(mutex_);
    check_cancelled(stop);
    Connection connection(root_);
    auto& database = connection.database();
    const auto snapshot = load_snapshot(root_, database, analysis_id, stop);
    const auto notes = load_notes(database, analysis_id, stop);
    QJsonArray annotations;
    for (const auto& note : notes) {
        check_cancelled(stop);
        annotations.append(persistence::note_json(note));
    }
    const auto results = persistence::snapshot_json(snapshot, stop);
    QSqlQuery origin(database);
    prepare(origin, QStringLiteral("SELECT a.original_path,o.original_name,s.relative_path,s.size FROM analyses a "
        "JOIN samples s ON s.sha256=a.sha256 JOIN sample_origins o ON o.sha256=a.sha256 AND o.path=a.original_path WHERE a.id=?"));
    origin.addBindValue(analysis_id);
    execute(origin);
    if (!origin.next()) fail(QStringLiteral("Proveniência da análise ausente."));
    const QJsonObject provenance{{"original_path", origin.value(0).toString()}, {"original_name", origin.value(1).toString()},
        {"stored_relative_path", origin.value(2).toString()}, {"size", origin.value(3).toString()}, {"sha256", snapshot.metadata.sha256}};
    origin.finish();
    const QJsonObject report{{"application", QLatin1String(application_tag)}, {"schema", 1}, {"kind", QStringLiteral("analysis-report")},
        {"analysis_id", analysis_id}, {"exported_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"source_sha256", snapshot.metadata.sha256}, {"engine_version", snapshot.engine_version}, {"state", snapshot.state},
        {"provenance", provenance}, {"options", results.value(QStringLiteral("options"))}, {"results", results}, {"notes", annotations},
        {"limitations", QStringLiteral("Análise nativa limitada; funções e fluxo podem ser parciais. A IR não é código-fonte original nem prova de equivalência.")}};
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const auto path = protected_export_path(root_, database, destination, stop);
    atomic_bytes(path, bytes, stop);
}

void Project::export_ir(const QString& analysis_id, Address address, const QString& text, const QString& destination, std::stop_token stop) const {
    std::lock_guard guard(mutex_);
    check_cancelled(stop);
    valid_address(address);
    Connection connection(root_);
    auto& database = connection.database();
    require_analysis(database, analysis_id);
    QSqlQuery query(database);
    prepare(query, QStringLiteral("SELECT a.sha256,a.revision,s.relative_path,s.size FROM analyses a JOIN samples s ON s.sha256=a.sha256 JOIN function_addresses f ON f.analysis_id=a.id WHERE a.id=? AND f.address_space=? AND f.address=?"));
    query.addBindValue(analysis_id);
    query.addBindValue(address.space);
    query.addBindValue(hex(address.value));
    execute(query);
    if (!query.next()) fail(QStringLiteral("Função inexistente nesta análise."));
    const auto source_hash = query.value(0).toString();
    const auto revision = query.value(1).toString();
    const auto relative = query.value(2).toString();
    const auto size = persistence::unsigned_hex(query.value(3).toString());
    query.finish();
    if (relative != QStringLiteral("samples/") + source_hash + QStringLiteral(".bin")) fail(QStringLiteral("Caminho de amostra inválido no índice."));
    verify_sample(internal_path(root_, relative), source_hash, size, stop);
    if (text.size() > 64 * 1024 * 1024) fail(QStringLiteral("IR excede o limite de exportação."));
    const QString header = QStringLiteral("// Reworkbench: representação intermediária própria e parcial.\n"
        "// Não é código-fonte original, decompilação completa nem prova de equivalência.\n"
        "// Instruções não modeladas permanecem opacas; consulte assembly, avisos e cobertura.\n"
        "// analysis=%1 revision=%2 function=%3 source_sha256=%4\n\n").arg(analysis_id, revision, address.text(), source_hash);
    const auto path = protected_export_path(root_, database, destination, stop);
    atomic_bytes(path, (header + text).toUtf8(), stop);
}

} // namespace reworkbench
