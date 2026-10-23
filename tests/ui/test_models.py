from concurrent.futures import Future

from PySide6.QtCore import Qt, QThread

from reworkbench.domain import Page
from reworkbench.ui.models import PagedTableModel


def test_large_model_fetches_200_rows(qtbot):
    calls = []

    def fetch(offset, query):
        calls.append((offset, query))
        future = Future()
        future.set_result(Page(tuple({"key": str(i), "name": f"func_{i}"} for i in range(offset, offset+200)), offset, 100000))
        return future

    model = PagedTableModel(fetch, ("name",))
    model.fetchMore()
    qtbot.waitUntil(lambda: model.rowCount() == 200)
    assert model.data(model.index(0, 0), Qt.DisplayRole) == "func_0"
    model.fetchMore()
    qtbot.waitUntil(lambda: model.rowCount() == 400)
    assert calls == [(0, ""), (200, "")]


def test_model_mutations_stay_on_gui_thread(qtbot):
    import threading
    future = Future()
    model = PagedTableModel(lambda offset, query: future, ("name",))
    threads = []
    model.rowsInserted.connect(lambda *args: threads.append(QThread.currentThread()))
    model.fetchMore()
    thread = threading.Thread(target=lambda: future.set_result(Page(({"key": "a", "name": "func"},), 0, 1)))
    thread.start()
    thread.join()
    qtbot.waitUntil(lambda: model.rowCount() == 1)
    assert threads == [model.thread()]


def test_outdated_search_page_is_ignored(qtbot):
    old, fresh = Future(), Future()
    model = PagedTableModel(lambda offset, query: old if not query else fresh, ("name",))
    model.fetchMore()
    model.set_query("new")
    old.set_result(Page(({"key": "old", "name": "old"},), 0, 1))
    fresh.set_result(Page(({"key": "new", "name": "new"},), 0, 1))
    qtbot.waitUntil(lambda: model.rowCount() == 1)
    assert model.data(model.index(0, 0), Qt.DisplayRole) == "new"
