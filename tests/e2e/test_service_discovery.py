from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Tuple

import pytest


@dataclass
class MdnsRecord:
    addresses: List[str]
    port: int
    txt: Dict[str, str]
    hostname: str | None = None


def _sanitize_path(path: str | None) -> str:
    if not path:
        return "/ws"
    if path.startswith("/"):
        return path
    return f"/{path}"


def build_uri(record: MdnsRecord) -> Tuple[str, str | None]:
    txt = record.txt
    proto = txt.get("proto", "wss")
    if txt.get("uri"):
        uri_value = txt["uri"]
        if "://" in uri_value:
            scheme, _, rest = uri_value.partition("://")
            proto = scheme
            if "/" in rest:
                _, _, path_override = rest.partition("/")
                txt = {**txt, "path": f"/{path_override}"}
    path = _sanitize_path(txt.get("path"))
    host_override = txt.get("host") or txt.get("sni") or record.hostname

    for addr in record.addresses:
        if ":" in addr:
            host = f"[{addr}]"
        else:
            host = addr
        uri = f"{proto}://{host}:{record.port}{path}"
        return uri, host_override
    raise ValueError("record has no addresses")


class DiscoveryCache:
    def __init__(self) -> None:
        self.uri: str | None = None
        self.sni: str | None = None

    def update(self, uri: str, sni: str | None) -> None:
        self.uri = uri
        self.sni = sni

    def recall(self) -> Tuple[str | None, str | None]:
        return self.uri, self.sni


@pytest.mark.parametrize(
    "record,expected_uri,expected_sni",
    [
        (
            MdnsRecord(
                addresses=["2001:db8::1"],
                port=7443,
                txt={"proto": "wss", "path": "telemetry"},
                hostname="sensor.lan",
            ),
            "wss://[2001:db8::1]:7443/telemetry",
            "sensor.lan",
        ),
        (
            MdnsRecord(
                addresses=["10.0.0.42"],
                port=8080,
                txt={"proto": "ws", "host": "sensor.example.com", "path": "/stream"},
                hostname="sensor.local",
            ),
            "ws://10.0.0.42:8080/stream",
            "sensor.example.com",
        ),
    ],
)
def test_build_uri_from_mdns(record: MdnsRecord, expected_uri: str, expected_sni: str | None) -> None:
    uri, sni = build_uri(record)
    assert uri == expected_uri
    assert sni == expected_sni


def test_discovery_cache_round_trip() -> None:
    cache = DiscoveryCache()
    cache.update("wss://[fe80::1]:8443/ws", "sensor.prod")
    recalled_uri, recalled_sni = cache.recall()
    assert recalled_uri == "wss://[fe80::1]:8443/ws"
    assert recalled_sni == "sensor.prod"
