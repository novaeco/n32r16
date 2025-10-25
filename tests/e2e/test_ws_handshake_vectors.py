"""Regression tests for WebSocket handshake key derivation and signatures."""
from __future__ import annotations

import hmac
import hashlib

import pytest


def _derive_key(secret: bytes, label: bytes) -> bytes:
    if not secret or not label:
        raise ValueError("secret and label must be provided")
    return hmac.new(secret, label, hashlib.sha256).digest()


@pytest.mark.parametrize(
    "label,expected_hex",
    [
        (b"ws-handshake", "f9566d04e6a10e9f10da3ccfdb52ba96cb66cf1781275f8d0ecaadd8cf4a97c6"),
        (b"ws-frame", "b0c04d8ad4b63db364cb2941ee607366edcd8b99ec89b363840b324e16811382"),
    ],
)
def test_derivation_vectors(label: bytes, expected_hex: str) -> None:
    secret = bytes(range(32))
    assert _derive_key(secret, label) == bytes.fromhex(expected_hex)


def test_handshake_signature_reference_vector() -> None:
    secret = bytes(range(32))
    handshake_key = _derive_key(secret, b"ws-handshake")
    nonce = bytes(range(16))
    auth_token = b"Bearer ExampleToken"
    signature = hmac.new(handshake_key, nonce + auth_token, hashlib.sha256).digest()
    assert signature == bytes.fromhex("ea785047b0566d44f54d36c4d30e8e67b3a6b3f537b45c253da8a07ca125df98")


def test_handshake_rejects_replay_nonce() -> None:
    """Changing the nonce must yield a distinct signature (prevents replay)."""
    secret = bytes(range(32))
    handshake_key = _derive_key(secret, b"ws-handshake")
    nonce = bytes(range(16))
    auth_token = b"Bearer ExampleToken"
    signature = hmac.new(handshake_key, nonce + auth_token, hashlib.sha256).digest()

    mutated_nonce = bytearray(nonce)
    mutated_nonce[-1] ^= 0xFF
    mutated_signature = hmac.new(handshake_key, bytes(mutated_nonce) + auth_token, hashlib.sha256).digest()

    assert mutated_signature != signature
    assert hmac.compare_digest(signature, signature)
    assert not hmac.compare_digest(signature, mutated_signature)
