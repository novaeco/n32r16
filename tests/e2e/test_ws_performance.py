"""High load regression tests for handshake and TOTP helpers."""
from __future__ import annotations

import hashlib
import hmac
import struct

import pytest


def _totp(secret: bytes, counter: int, digits: int = 8) -> int:
    msg = struct.pack(">Q", counter)
    digest = hmac.new(secret, msg, hashlib.sha1).digest()
    offset = digest[-1] & 0x0F
    code = ((digest[offset] & 0x7F) << 24) | ((digest[offset + 1] & 0xFF) << 16) | (
        (digest[offset + 2] & 0xFF) << 8
    ) | (digest[offset + 3] & 0xFF)
    return code % (10 ** digits)


@pytest.mark.parametrize("iterations", [128, 512])
def test_totp_bulk_uniqueness(iterations: int) -> None:
    secret = b"12345678901234567890"
    values = {_totp(secret, i) for i in range(iterations)}
    # No collisions expected within the evaluated window.
    assert len(values) == iterations


def test_handshake_hmac_under_load() -> None:
    secret = bytes(range(32))
    label = b"ws-handshake"
    base_key = hmac.new(secret, label, hashlib.sha256).digest()
    token = b"Bearer ExampleToken"
    signatures = set()
    for counter in range(256):
        nonce = struct.pack("<Q", counter) + struct.pack("<Q", 255 - counter)
        signatures.add(hmac.new(base_key, nonce + token, hashlib.sha256).digest())
    assert len(signatures) == 256
