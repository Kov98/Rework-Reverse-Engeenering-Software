import threading
import time

import pytest

from reworkbench.application.session import Session
from reworkbench.domain import Address, EngineConfig, ImportOptions
from tests.helpers.session_worker import SessionWorker


def eventually(predicate, timeout=5):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(0.01)
    assert predicate()


@pytest.fixture
def session(tmp_path):
    events = []
    workers = []

    def factory(*args, **kwargs):
        worker = SessionWorker(*args, **kwargs)
        workers.append(worker)
        return worker

    session = Session(EngineConfig(), events.append, supervisor_factory=factory)
    session.create_project(tmp_path / "project").result(10)
    source = tmp_path / "source.bin"
    source.write_bytes(b"known sample")
    yield session, events, workers, source
    session.close().result(10)


def test_only_one_analysis_runs(session):
    application, events, workers, source = session
    application.catalog().result(5)
    workers[0].analysis_gate.clear()
    first = application.start_analysis(source, ImportOptions())
    eventually(lambda: bool(workers[0].opens))
    second = application.start_analysis(source, ImportOptions())
    assert len(workers) == 1
    assert not second.done()
    workers[0].analysis_gate.set()
    assert first.result(10) != second.result(10)


def test_semantic_requests_wait_for_stable_snapshot(session):
    application, events, workers, source = session
    analysis_id = application.start_analysis(source, ImportOptions()).result(10)
    workers[0].decompile_gate.clear()
    application.select_function(analysis_id, Address("ram", "0x1000"))
    # Cached navigation must remain readable while a semantic request is in flight.
    assert application.fetch_page(analysis_id, "functions").result(3).total == 2
    workers[0].decompile_gate.set()


def test_stale_function_and_project_response_is_not_displayed(session):
    application, events, workers, source = session
    analysis_id = application.start_analysis(source, ImportOptions()).result(10)
    workers[0].decompile_gate.clear()
    application.select_function(analysis_id, Address("ram", "0x1000"))
    newest = application.select_function(analysis_id, Address("ram", "0x2000"))
    workers[0].decompile_gate.set()
    eventually(lambda: any(e["kind"] == "selection_result" for e in events))
    shown = [e for e in events if e["kind"] == "selection_result"]
    assert shown[-1]["request_id"] == newest
    assert shown[-1]["payload"]["address"]["value"] == "0x2000"


def test_reopen_rebuilds_index_after_worker_crash(session):
    application, events, workers, source = session
    analysis_id = application.start_analysis(source, ImportOptions()).result(10)
    application.set_note(analysis_id, Address("ram", "0x1000"), "Preservar observação", True).result(5)
    application.rename_function(analysis_id, Address("ram", "0x1000"), "investigada").result(10)
    root = application.project_root
    application.open_project(root).result(10)
    assert application.fetch_page(analysis_id, "functions").result(5).items[0]["name"] == "investigada"
    assert application.notes(analysis_id).result(5)[0]["text"] == "Preservar observação"


def test_cancelled_copy_never_starts_engine(session):
    application, events, workers, source = session
    # Cancellation is set while work is queued, before it is allowed to start.
    gate = threading.Event()
    application._operations.submit(lambda: gate.wait(5))
    pending = application.start_analysis(source, ImportOptions())
    application.cancel_active()
    gate.set()
    pending.result(10)
    assert workers == []


def test_rename_invalidates_cache(session):
    application, events, workers, source = session
    analysis_id = application.start_analysis(source, ImportOptions()).result(10)
    application.select_function(analysis_id, Address("ram", "0x1000"))
    eventually(lambda: any(e["kind"] == "selection_result" for e in events))
    application.rename_function(analysis_id, Address("ram", "0x1000"), "renamed").result(10)
    assert application._repo.get_cache(analysis_id, Address("ram", "0x1000"), 0) is None


def test_failure_does_not_erase_notes(session):
    application, events, workers, source = session
    analysis_id = application.start_analysis(source, ImportOptions()).result(10)
    application.set_note(analysis_id, Address("ram", "0x1000"), "Achado", False).result(5)
    with pytest.raises(Exception):
        application.start_analysis(source.with_name("missing.bin"), ImportOptions()).result(5)
    assert application.notes(analysis_id).result(5)[0]["text"] == "Achado"
