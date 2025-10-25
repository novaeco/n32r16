"""Utilities to emulate the JSON protocol shared between sensor and HMI nodes."""
from __future__ import annotations

import json
import struct
import zlib
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, MutableMapping, Tuple


CRC32_POLY_INIT = 0xFFFFFFFF
CRC32_XOR_OUT = 0xFFFFFFFF


def compute_crc32(payload: bytes) -> int:
    """Compute the CRC32 used by the firmware protocol."""
    crc = zlib.crc32(payload, CRC32_POLY_INIT) ^ CRC32_XOR_OUT
    return crc & 0xFFFFFFFF


def frame_payload(payload: bytes) -> bytes:
    """Prefix the payload with its CRC32 checksum."""
    crc = compute_crc32(payload)
    return struct.pack("<I", crc) + payload


def split_frame(frame: bytes) -> Tuple[int, bytes]:
    """Return CRC and payload from a framed WebSocket binary packet."""
    if len(frame) < 4:
        raise ValueError("Frame shorter than CRC")
    crc = struct.unpack_from("<I", frame, 0)[0]
    return crc, frame[4:]


def _ensure_list(value: Iterable[Any]) -> List[Any]:
    return list(value) if not isinstance(value, list) else value


def encode_sensor_update(update: MutableMapping[str, Any], *, use_cbor: bool = False) -> bytes:
    """Serialize a sensor update message following the JSON schema."""
    if use_cbor:
        raise NotImplementedError("CBOR encoding is not required for the Python test bench")

    canonical: Dict[str, Any] = {
        "v": 1,
        "type": "sensor_update",
        "ts": int(update["timestamp_ms"]),
        "seq": int(update["sequence_id"]),
        "sht20": _ensure_list(update.get("sht20", [])),
        "ds18b20": _ensure_list(update.get("ds18b20", [])),
        "gpio": update.get("gpio", {}),
        "pwm": update.get("pwm", {}),
    }
    return json.dumps(canonical, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def encode_command(command: MutableMapping[str, Any], *, use_cbor: bool = False) -> bytes:
    if use_cbor:
        raise NotImplementedError("CBOR encoding is not required for the Python test bench")

    canonical: Dict[str, Any] = {
        "v": 1,
        "type": "cmd",
        "ts": int(command["timestamp_ms"]),
        "seq": int(command["sequence_id"]),
    }
    if command.get("set_pwm"):
        canonical["set_pwm"] = command["set_pwm"]
    if command.get("pwm_freq"):
        canonical["pwm_freq"] = command["pwm_freq"]
    if command.get("write_gpio"):
        canonical["write_gpio"] = command["write_gpio"]

    return json.dumps(canonical, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def decode_sensor_update(payload: bytes, *, use_cbor: bool = False) -> Dict[str, Any]:
    if use_cbor:
        raise NotImplementedError("CBOR decoding is not required for the Python test bench")
    data = json.loads(payload.decode("utf-8"))
    if data.get("type") != "sensor_update":
        raise ValueError("Unexpected message type")
    return data


def decode_command(payload: bytes, *, use_cbor: bool = False) -> Dict[str, Any]:
    if use_cbor:
        raise NotImplementedError("CBOR decoding is not required for the Python test bench")
    data = json.loads(payload.decode("utf-8"))
    if data.get("type") != "cmd":
        raise ValueError("Unexpected message type")
    return data


@dataclass
class CommandEffects:
    """In-memory representation of IO state mutations."""

    pwm_channels: Dict[int, int]
    pwm_frequency: int | None
    gpio: Dict[Tuple[int, str], Tuple[int, int]]

    @classmethod
    def empty(cls) -> "CommandEffects":
        return cls(pwm_channels={}, pwm_frequency=None, gpio={})

    def apply(self, command: Dict[str, Any]) -> None:
        if "set_pwm" in command:
            entry = command["set_pwm"]
            self.pwm_channels[int(entry["ch"])] = int(entry["duty"])
        if "pwm_freq" in command:
            entry = command["pwm_freq"]
            self.pwm_frequency = int(entry["freq"])
        if "write_gpio" in command:
            entry = command["write_gpio"]
            dev = 0 if entry.get("dev") == "mcp0" else 1
            port = str(entry.get("port", "A"))
            self.gpio[(dev, port)] = (int(entry.get("mask", 0)), int(entry.get("value", 0)))
