import json
from dataclasses import asdict

from reworkbench.domain import Envelope

MAX_MESSAGE = 8 * 1024 * 1024
OPERATIONS = {"hello", "catalog", "inspect", "open", "analyze", "page", "decompile", "rename", "save", "close", "cancel", "heartbeat", "progress", "failure"}


class ProtocolError(ValueError):
    pass


def decode(data: bytes) -> Envelope:
    if len(data) > MAX_MESSAGE:
        raise ProtocolError("Mensagem excede 8 MiB.")
    try:
        item = json.loads(data.decode("utf-8"))
        if not isinstance(item, dict) or type(item.get("version")) is not int or item["version"] != 1:
            raise ValueError("Versão de protocolo incompatível.")
        if not isinstance(item.get("request_id"), str) or not item["request_id"]:
            raise ValueError("Requisição inválida.")
        if item.get("analysis_id") is not None and not isinstance(item["analysis_id"], str):
            raise ValueError("Identificador de análise inválido.")
        if item.get("operation") not in OPERATIONS or not isinstance(item.get("payload"), dict):
            raise ValueError("Operação ou payload inválido.")
        if item.get("error") is not None and not isinstance(item["error"], dict):
            raise ValueError("Erro inválido.")
        return Envelope(**item)
    except (ValueError, TypeError, UnicodeError) as exc:
        raise ProtocolError(str(exc)) from exc


def encode(message: Envelope) -> bytes:
    data = json.dumps(asdict(message), ensure_ascii=False, allow_nan=False).encode("utf-8")
    decode(data)
    return data
