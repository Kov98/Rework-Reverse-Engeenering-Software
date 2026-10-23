from pathlib import Path


def read_range(path: Path, offset: int, length: int = 65536) -> bytes:
    if offset < 0 or not 0 <= length <= 65536:
        raise ValueError("Intervalo inválido: offset positivo e até 64 KiB por leitura.")
    with path.open("rb") as stream:
        stream.seek(offset)
        return stream.read(length)
