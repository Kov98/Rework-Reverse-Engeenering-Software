from __future__ import annotations

import threading
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable, Literal

type JsonValue = str | int | float | bool | None | list[JsonValue] | dict[str, JsonValue]
type JsonObject = dict[str, JsonValue]
type RecordKind = Literal["functions", "symbols", "strings", "imports", "regions", "xrefs", "assembly"]
RECORD_KINDS = ("functions", "symbols", "strings", "imports", "regions", "xrefs", "assembly")
STATES = ("awaiting_configuration", "queued", "importing", "analyzing", "ready", "partial", "cancelled", "failed")


@dataclass(frozen=True)
class Address:
    space: str
    value: str

    def __post_init__(self):
        if not self.space or not isinstance(self.value, str):
            raise ValueError("Endereço requer espaço e hexadecimal em texto.")
        try:
            value = int(self.value, 16)
        except ValueError as exc:
            raise ValueError("Endereço hexadecimal inválido.") from exc
        if value < 0:
            raise ValueError("Endereço negativo.")
        object.__setattr__(self, "value", hex(value))

    def to_json(self) -> JsonObject:
        return {"space": self.space, "value": self.value}

    @classmethod
    def from_json(cls, data: JsonObject) -> Address:
        return cls(data["space"], data["value"])

    def __str__(self):
        return f"{self.space}:{self.value}"


@dataclass(frozen=True)
class EntryPoint:
    address: Address
    context: dict[str, str] = field(default_factory=dict)


@dataclass(frozen=True)
class ImportOptions:
    loader_id: str | None = None
    load_spec_id: str | None = None
    language_id: str | None = None
    compiler_id: str | None = None
    image_base: str | None = None
    entries: tuple[EntryPoint, ...] = ()
    analysis_timeout_s: int = 300
    decompile_timeout_s: int = 30

    def __post_init__(self):
        if self.analysis_timeout_s < 1 or self.decompile_timeout_s < 1:
            raise ValueError("Limites de tempo devem ser positivos.")
        if self.image_base is not None:
            Address("ram", self.image_base)

    def to_json(self) -> JsonObject:
        data = asdict(self)
        data["entries"] = [{"address": e.address.to_json(), "context": e.context} for e in self.entries]
        return data

    @classmethod
    def from_json(cls, data: JsonObject) -> ImportOptions:
        data = dict(data)
        data["entries"] = tuple(EntryPoint(Address.from_json(e["address"]), e.get("context", {})) for e in data.get("entries", []))
        return cls(**data)


@dataclass(frozen=True)
class EngineConfig:
    ghidra_home: Path | None = None
    java_home: Path | None = None


@dataclass(frozen=True)
class Sample:
    sha256: str
    size: int
    relative_path: str


@dataclass(frozen=True)
class Analysis:
    id: str
    sample_sha256: str
    options: ImportOptions
    state: str
    revision: int = 0


@dataclass(frozen=True)
class Page:
    items: tuple[JsonObject, ...]
    offset: int
    total: int


@dataclass(frozen=True)
class DecompileResult:
    address: Address
    code: str | None
    error: str | None
    revision: int = 0

    def to_json(self) -> JsonObject:
        return {"address": self.address.to_json(), "code": self.code, "error": self.error, "revision": self.revision}

    @classmethod
    def from_json(cls, data: JsonObject) -> DecompileResult:
        return cls(Address.from_json(data["address"]), data.get("code"), data.get("error"), data.get("revision", 0))


@dataclass(frozen=True)
class Envelope:
    version: int
    request_id: str
    analysis_id: str | None
    operation: str
    payload: JsonObject = field(default_factory=dict)
    error: JsonObject | None = None


class Cancellation:
    def __init__(self):
        self._event = threading.Event()
        self._lock = threading.Lock()
        self._callback: Callable[[], None] | None = None

    def cancel(self) -> None:
        self._event.set()
        with self._lock:
            if self._callback is not None:
                self._callback()

    def cancelled(self) -> bool:
        return self._event.is_set()

    def bind(self, cancel_callback: Callable[[], None]) -> None:
        with self._lock:
            self._callback = cancel_callback
            if self.cancelled():
                cancel_callback()
