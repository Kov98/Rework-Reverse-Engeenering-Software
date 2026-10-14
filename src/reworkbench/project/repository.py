from __future__ import annotations

import json
import sqlite3
import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from pathlib import Path
from typing import Iterable

from reworkbench.domain import Address, Analysis, Cancellation, DecompileResult, ImportOptions, Page, RECORD_KINDS, STATES, Sample
from reworkbench.project.storage import ProjectError, ProjectLock, contained_path, copy_sample

MIGRATIONS = ((
    "CREATE TABLE samples(sha256 TEXT PRIMARY KEY, size INTEGER NOT NULL, relative_path TEXT NOT NULL)",
    "CREATE TABLE analyses(id TEXT PRIMARY KEY, sample_sha256 TEXT NOT NULL REFERENCES samples(sha256), options TEXT NOT NULL, state TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 0, metadata TEXT NOT NULL DEFAULT '{}')",
    "CREATE TABLE records(analysis_id TEXT NOT NULL REFERENCES analyses(id), kind TEXT NOT NULL, key TEXT NOT NULL, ordinal INTEGER NOT NULL, payload TEXT NOT NULL, search TEXT NOT NULL, PRIMARY KEY(analysis_id,kind,key))",
    "CREATE INDEX record_page ON records(analysis_id,kind,ordinal)",
    "CREATE TABLE notes(analysis_id TEXT NOT NULL REFERENCES analyses(id), space TEXT NOT NULL, address TEXT NOT NULL, text TEXT NOT NULL, bookmarked INTEGER NOT NULL, PRIMARY KEY(analysis_id,space,address))",
    "CREATE TABLE decompile_cache(analysis_id TEXT NOT NULL REFERENCES analyses(id), space TEXT NOT NULL, address TEXT NOT NULL, revision INTEGER NOT NULL, payload TEXT NOT NULL, PRIMARY KEY(analysis_id,space,address))",
),)


def migrate_database(conn: sqlite3.Connection, migrations=MIGRATIONS) -> None:
    version = conn.execute("PRAGMA user_version").fetchone()[0]
    if version > len(migrations):
        raise ProjectError("Banco de uma versão mais recente; escrita recusada.")
    try:
        conn.execute("BEGIN IMMEDIATE")
        for index in range(version, len(migrations)):
            for statement in migrations[index]:
                conn.execute(statement)
            conn.execute(f"PRAGMA user_version={index + 1}")
        conn.commit()
    except Exception:
        conn.rollback()
        raise


class ProjectRepository:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self._closed = False
        self._lock = ProjectLock(self.root)
        self._executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="project-db")
        try:
            self._executor.submit(self._connect).result()
        except Exception:
            self._executor.shutdown()
            self._lock.close()
            raise

    def _connect(self):
        self._connection = sqlite3.connect(contained_path(self.root, "index.sqlite"))
        self._connection.row_factory = sqlite3.Row
        self._connection.execute("PRAGMA foreign_keys=ON")
        migrate_database(self._connection)
        self._connection.execute("PRAGMA journal_mode=WAL")

    def _run(self, operation):
        if self._closed:
            raise ProjectError("Projeto fechado.")
        return self._executor.submit(operation, self._connection).result()

    @classmethod
    def create(cls, root: Path):
        root = root.resolve()
        if root.exists() and any(root.iterdir()):
            raise ProjectError("Escolha uma pasta vazia para o novo projeto.")
        root.mkdir(parents=True, exist_ok=True)
        for name in ("samples", "analyses", "logs"):
            (root / name).mkdir()
        (root / "project.json").write_text(json.dumps({"schema_version": 1, "id": uuid.uuid4().hex}, indent=2), encoding="utf-8")
        return cls(root)

    @classmethod
    def open(cls, root: Path):
        root = root.resolve()
        try:
            data = json.loads((root / "project.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise ProjectError("Pasta não contém um projeto válido.") from exc
        if data.get("schema_version") != 1:
            raise ProjectError("Projeto de uma versão incompatível.")
        return cls(root)

    def close(self):
        if self._closed:
            return
        self._run(lambda conn: conn.close())
        self._closed = True
        self._executor.shutdown(wait=True)
        self._lock.close()

    def import_sample(self, source: Path, cancel: Cancellation) -> Sample:
        sample = copy_sample(self.root, source, cancel)
        def insert(conn):
            with conn:
                conn.execute("INSERT OR IGNORE INTO samples VALUES(?,?,?)", (sample.sha256, sample.size, sample.relative_path))
        self._run(insert)
        return sample

    def get_sample(self, sha256: str) -> Sample:
        row = self._run(lambda c: c.execute("SELECT * FROM samples WHERE sha256=?", (sha256,)).fetchone())
        if row is None:
            raise ProjectError("Amostra não encontrada.")
        return Sample(**dict(row))

    def create_analysis(self, sample: Sample, options: ImportOptions) -> Analysis:
        analysis = Analysis(uuid.uuid4().hex, sample.sha256, options, "queued")
        contained_path(self.root, f"analyses/{analysis.id}").mkdir()
        def insert(conn):
            with conn:
                conn.execute("INSERT INTO analyses(id,sample_sha256,options,state) VALUES(?,?,?,?)", (analysis.id, sample.sha256, json.dumps(options.to_json()), analysis.state))
        self._run(insert)
        return analysis

    @staticmethod
    def _analysis(row):
        if row is None:
            raise ProjectError("Análise não encontrada.")
        return Analysis(row["id"], row["sample_sha256"], ImportOptions.from_json(json.loads(row["options"])), row["state"], row["revision"])

    def get_analysis(self, analysis_id: str) -> Analysis:
        return self._analysis(self._run(lambda c: c.execute("SELECT * FROM analyses WHERE id=?", (analysis_id,)).fetchone()))

    def analyses(self):
        return tuple(self._analysis(r) for r in self._run(lambda c: c.execute("SELECT * FROM analyses ORDER BY rowid").fetchall()))

    def set_state(self, analysis_id: str, state: str):
        if state not in STATES:
            raise ValueError("Estado inválido.")
        self._update("UPDATE analyses SET state=? WHERE id=?", (state, analysis_id))

    def _update(self, sql, args):
        def update(conn):
            with conn:
                conn.execute(sql, args)
        self._run(update)

    def set_metadata(self, analysis_id, metadata):
        self._update("UPDATE analyses SET metadata=? WHERE id=?", (json.dumps(metadata, ensure_ascii=False), analysis_id))

    def get_metadata(self, analysis_id):
        row = self._run(lambda c: c.execute("SELECT metadata FROM analyses WHERE id=?", (analysis_id,)).fetchone())
        return json.loads(row[0]) if row else {}

    def replace_records(self, analysis_id: str, kind: str, pages: Iterable[Page]):
        if kind not in RECORD_KINDS:
            raise ValueError("Tipo de registro inválido.")
        def replace(conn):
            with conn:
                conn.execute("DELETE FROM records WHERE analysis_id=? AND kind=?", (analysis_id, kind))
                ordinal = 0
                for page in pages:
                    for item in page.items:
                        payload = json.dumps(item, ensure_ascii=False)
                        conn.execute("INSERT INTO records VALUES(?,?,?,?,?,?)", (analysis_id, kind, item["key"], ordinal, payload, payload.casefold()))
                        ordinal += 1
        self._run(replace)

    def page(self, analysis_id, kind, offset=0, limit=200, query="") -> Page:
        if kind not in RECORD_KINDS or offset < 0 or not 1 <= limit <= 200:
            raise ValueError("Página inválida.")
        escaped = query.casefold().replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
        where = "analysis_id=? AND kind=? AND search LIKE ? ESCAPE '\\'"
        args = (analysis_id, kind, f"%{escaped}%")
        def fetch(conn):
            total = conn.execute(f"SELECT COUNT(*) FROM records WHERE {where}", args).fetchone()[0]
            rows = conn.execute(f"SELECT payload FROM records WHERE {where} ORDER BY ordinal LIMIT ? OFFSET ?", (*args, limit, offset)).fetchall()
            return Page(tuple(json.loads(r[0]) for r in rows), offset, total)
        return self._run(fetch)

    def set_note(self, analysis_id, address: Address, text, bookmarked):
        self._update("INSERT INTO notes VALUES(?,?,?,?,?) ON CONFLICT(analysis_id,space,address) DO UPDATE SET text=excluded.text,bookmarked=excluded.bookmarked", (analysis_id, address.space, address.value, text, int(bookmarked)))

    def notes(self, analysis_id):
        rows = self._run(lambda c: c.execute("SELECT * FROM notes WHERE analysis_id=? ORDER BY rowid", (analysis_id,)).fetchall())
        return tuple({"address": Address(r["space"], r["address"]).to_json(), "text": r["text"], "bookmarked": bool(r["bookmarked"])} for r in rows)

    def set_revision(self, analysis_id, revision):
        def update(conn):
            with conn:
                conn.execute("UPDATE analyses SET revision=? WHERE id=?", (revision, analysis_id))
                conn.execute("DELETE FROM decompile_cache WHERE analysis_id=?", (analysis_id,))
        self._run(update)

    def get_cache(self, analysis_id, address, revision):
        row = self._run(lambda c: c.execute("SELECT payload FROM decompile_cache WHERE analysis_id=? AND space=? AND address=? AND revision=?", (analysis_id, address.space, address.value, revision)).fetchone())
        return DecompileResult.from_json(json.loads(row[0])) if row else None

    def put_cache(self, analysis_id, result):
        self._update("INSERT OR REPLACE INTO decompile_cache VALUES(?,?,?,?,?)", (analysis_id, result.address.space, result.address.value, result.revision, json.dumps(result.to_json())))
