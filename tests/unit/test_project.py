import hashlib
import json
import sqlite3
from concurrent.futures import CancelledError

import pytest

from reworkbench.domain import Address, Cancellation, DecompileResult, ImportOptions, Page
from reworkbench.project.repository import ProjectError, ProjectRepository, migrate_database


@pytest.fixture
def repo(tmp_path):
    project = ProjectRepository.create(tmp_path / "investigação com espaços")
    yield project
    project.close()


def sample_analysis(repo, tmp_path):
    source = tmp_path / "amostra.bin"
    source.write_bytes(b"abcdef")
    sample = repo.import_sample(source, Cancellation())
    return repo.create_analysis(sample, ImportOptions())


def test_copy_is_atomic_and_source_unchanged(repo, tmp_path):
    source = tmp_path / "origem com acentuação.bin"
    source.write_bytes(b"binary-original")
    sample = repo.import_sample(source, Cancellation())
    assert sample.sha256 == hashlib.sha256(b"binary-original").hexdigest()
    assert (repo.root / sample.relative_path).read_bytes() == b"binary-original"
    assert source.read_bytes() == b"binary-original"
    assert not list((repo.root / "samples").glob("*.partial"))


def test_same_content_is_deduplicated(repo, tmp_path):
    source = tmp_path / "origem.bin"
    source.write_bytes(b"abcdef")
    first = repo.import_sample(source, Cancellation())
    second = repo.import_sample(source, Cancellation())
    assert first == second
    assert len(list((repo.root / "samples").glob("*.bin"))) == 1


def test_unicode_path_and_changed_source(repo, tmp_path):
    source = tmp_path / "móvel.bin"
    source.write_bytes(b"abcdef")

    class SourceChanges(Cancellation):
        def cancelled(self):
            source.write_bytes(b"a different binary")
            return False

    with pytest.raises(ProjectError, match="alterad"):
        repo.import_sample(source, SourceChanges())
    assert not list((repo.root / "samples").glob("*.bin"))
    assert not list((repo.root / "samples").glob("*.partial"))


def test_cancel_removes_only_partial_copy(repo, tmp_path):
    analysis = sample_analysis(repo, tmp_path)
    source = tmp_path / "other.bin"
    source.write_bytes(b"second")
    cancel = Cancellation()
    cancel.cancel()
    with pytest.raises(CancelledError):
        repo.import_sample(source, cancel)
    assert repo.get_analysis(analysis.id).sample_sha256 == analysis.sample_sha256
    assert len(list((repo.root / "samples").glob("*.bin"))) == 1


def test_project_lock_and_future_schema(repo):
    with pytest.raises(ProjectError, match="uso"):
        ProjectRepository.open(repo.root)
    root = repo.root
    repo.close()
    manifest = root / "project.json"
    data = json.loads(manifest.read_text(encoding="utf-8"))
    data["schema_version"] = 999
    manifest.write_text(json.dumps(data), encoding="utf-8")
    before = manifest.read_bytes()
    with pytest.raises(ProjectError, match="versão"):
        ProjectRepository.open(root)
    assert manifest.read_bytes() == before


def test_notes_survive_reopen(repo, tmp_path):
    analysis = sample_analysis(repo, tmp_path)
    repo.set_note(analysis.id, Address("ram", "0x1000"), "Investigar chamada", True)
    root = repo.root
    repo.close()
    reopened = ProjectRepository.open(root)
    try:
        assert reopened.notes(analysis.id)[0]["text"] == "Investigar chamada"
        assert reopened.notes(analysis.id)[0]["bookmarked"] is True
    finally:
        reopened.close()


def test_migration_rolls_back(tmp_path):
    conn = sqlite3.connect(tmp_path / "migration.sqlite")
    with pytest.raises(sqlite3.OperationalError):
        migrate_database(conn, (("CREATE TABLE sentinel(value TEXT)", "INVALID SQL"),))
    assert conn.execute("PRAGMA user_version").fetchone()[0] == 0
    assert conn.execute("SELECT name FROM sqlite_master WHERE name='sentinel'").fetchall() == []
    conn.close()


def test_literal_search_and_pagination(repo, tmp_path):
    analysis = sample_analysis(repo, tmp_path)
    rows = tuple({"key": str(i), "name": f"func_{i}", "address": {"space": "ram", "value": hex(0x1000+i)}} for i in range(450))
    repo.replace_records(analysis.id, "functions", [Page(rows, 0, len(rows))])
    assert len(repo.page(analysis.id, "functions").items) == 200
    assert repo.page(analysis.id, "functions", offset=200).items[0]["key"] == "200"
    assert repo.page(analysis.id, "functions", query="%").total == 0
    assert repo.page(analysis.id, "functions", query="func_1").total == 111


def test_cache_is_invalid_after_revision(repo, tmp_path):
    analysis = sample_analysis(repo, tmp_path)
    address = Address("ram", "0x1000")
    repo.put_cache(analysis.id, DecompileResult(address, "return 1;", None, 0))
    assert repo.get_cache(analysis.id, address, 0).code == "return 1;"
    repo.set_revision(analysis.id, 1)
    assert repo.get_cache(analysis.id, address, 0) is None
