import json
from dataclasses import asdict

import pytest

from reworkbench.domain import Envelope
from reworkbench.engine.protocol import ProtocolError, decode, encode


def test_bad_auth_and_protocol_version():
    data = asdict(Envelope(2, "id", None, "hello"))
    with pytest.raises(ProtocolError):
        decode(json.dumps(data).encode())


def test_oversized_and_truncated_message():
    with pytest.raises(ProtocolError):
        decode(b'{')
    with pytest.raises(ProtocolError):
        decode(b' ' * (8 * 1024 * 1024 + 1))
    with pytest.raises(ProtocolError):
        decode(b'{"version":1,"request_id":4,"operation":"hello","payload":{}}')


def test_unicode_and_64bit_survive_transport():
    message = Envelope(1, "id", "analysis", "page", {"name": "função", "address": {"space": "ram", "value": "0xfffffffffffffff0"}})
    assert decode(encode(message)) == message
