from pathlib import Path

import pytest

from reworkbench.domain import Address
from reworkbench.project.bytes import read_range


def test_hex_reads_only_requested_range(tmp_path):
    path = tmp_path / "sample.bin"
    path.write_bytes(b"abcdef")
    assert read_range(path, 2, 3) == b"cde"
    assert read_range(path, 5, 30) == b"f"
    assert read_range(path, 10, 20) == b""
    with pytest.raises(ValueError):
        read_range(path, -1, 20)
    with pytest.raises(ValueError):
        read_range(path, 0, 65537)


def test_sparse_file_uses_bounded_window(tmp_path):
    path = tmp_path / "large.bin"
    with path.open("wb") as stream:
        stream.seek(128 * 1024 * 1024)
        stream.write(b"end")
    assert len(read_range(path, 0)) == 65536
    assert read_range(path, 128 * 1024 * 1024, 3) == b"end"


def test_addresses_do_not_collide():
    address = Address.from_json({"space": "ram", "value": "0xfffffffffffffff0"})
    assert address.value == "0xfffffffffffffff0"
    assert address != Address("overlay", address.value)
    assert Address("ram", "0x00001000") == Address("ram", "0x1000")
    with pytest.raises(ValueError):
        Address("ram", "-1")
