from pathlib import Path

from reworkbench.application.session import Session
from reworkbench.domain import Address, ImportOptions
from reworkbench.engine.config import discover_config


def test_session_preserves_real_engine_names_and_notes(tmp_path):
    events = []
    session = Session(discover_config(), events.append)
    source = Path(__file__).resolve().parents[1] / "fixtures/bin/pe64.exe"
    try:
        session.create_project(tmp_path / "projeto com acentuação").result(10)
        analysis_id = session.start_analysis(source, ImportOptions()).result(240)
        functions = session.fetch_page(analysis_id, "functions").result(10).items
        function = next(f for f in functions if "compute" in f["name"])
        address = Address.from_json(function["address"])
        session.rename_function(analysis_id, address, "funcao_verificada").result(120)
        session.set_note(analysis_id, address, "Observação real persistida", True).result(10)
        session.open_project(session.project_root).result(240)
        assert any(f["name"] == "funcao_verificada" for f in session.fetch_page(analysis_id, "functions").result(10).items)
        assert session.notes(analysis_id).result(10)[0]["text"] == "Observação real persistida"
    finally:
        session.close().result(30)
