"""TLS-capable mock WebSocket gateway for mobile/browser validation."""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import math
import ssl
import time
from dataclasses import dataclass
from typing import Optional

import websockets
from websockets.server import WebSocketServerProtocol

from . import proto


@dataclass
class GatewayConfig:
    host: str
    port: int
    token: Optional[str]
    cbor: bool
    update_period: float
    cert: Optional[str]
    key: Optional[str]
    insecure: bool


class MockGateway:
    """Serve firmware-compatible WebSocket frames to browser/mobile clients."""

    def __init__(self, cfg: GatewayConfig) -> None:
        self._cfg = cfg
        self._sequence = 0

    def _build_update(self) -> bytes:
        """Generate a deterministic telemetry envelope for UI validation."""
        now_ms = int(time.time() * 1000)
        temp = 22.5 + 1.5 * math.sin(self._sequence / 10.0)
        hum = 48.0 + 6.0 * math.cos(self._sequence / 12.0)
        update = {
            "timestamp_ms": now_ms,
            "sequence_id": self._sequence,
            "sht20": [
                {"name": "SHT20-A", "temperature_c": temp, "humidity_pct": hum},
                {"name": "SHT20-B", "temperature_c": temp - 0.8, "humidity_pct": hum + 1.2},
            ],
            "ds18b20": [
                {"name": "Rack-1", "temperature_c": temp + 2.5},
                {"name": "Rack-2", "temperature_c": temp + 1.1},
            ],
            "gpio": {
                "mcp0": {"port_a": {"mask": 0x0F, "value": 0x05}, "port_b": {"mask": 0x03, "value": 0x02}},
                "mcp1": {"port_a": {"mask": 0x07, "value": 0x01}, "port_b": {"mask": 0x0C, "value": 0x08}},
            },
            "pwm": {"freq": 500, "channels": {"0": 1024, "1": 2048, "2": 3072}},
        }
        self._sequence += 1
        payload = proto.encode_sensor_update(update, use_cbor=self._cfg.cbor)
        return proto.frame_payload(payload)

    async def _push_updates(self, websocket: WebSocketServerProtocol) -> None:
        try:
            while True:
                frame = self._build_update()
                await websocket.send(frame)
                await asyncio.sleep(self._cfg.update_period)
        except asyncio.CancelledError:  # pragma: no cover - controlled shutdown
            raise
        except Exception as exc:  # pragma: no cover - surfaced via handler
            await websocket.close(code=1011, reason=f"telemetry failure: {exc}")

    async def handler(self, websocket: WebSocketServerProtocol) -> None:
        auth = websocket.request_headers.get("Authorization", "")
        expected = f"Bearer {self._cfg.token}" if self._cfg.token else None
        if expected and auth != expected:
            await websocket.close(code=4401, reason="invalid token")
            return

        telemetry = asyncio.create_task(self._push_updates(websocket))
        try:
            async for message in websocket:
                if isinstance(message, str):
                    await websocket.send("text frames are not supported")
                    continue
                if len(message) < 4:
                    await websocket.send(b"\x00\x00\x00\x00")
                    continue
                crc, payload = proto.split_frame(message)
                if crc != proto.compute_crc32(payload):
                    await websocket.send(b"\x00\x00\x00\x00")
                    continue
                command = proto.decode_command(payload, use_cbor=self._cfg.cbor)
                seq = int(command.get("seq", 0))
                freq_cmd = command.get("pwm_freq") or {}
                pwm_freq = int(freq_cmd.get("freq", 500))
                channels: dict[str, int] = {}
                pwm_cmd = command.get("set_pwm")
                if isinstance(pwm_cmd, dict) and "ch" in pwm_cmd and "duty" in pwm_cmd:
                    channels[str(pwm_cmd["ch"])] = int(pwm_cmd["duty"])
                # Echo the command state back as an acknowledgement frame.
                ack_payload = proto.encode_sensor_update(
                    {
                        "timestamp_ms": int(time.time() * 1000),
                        "sequence_id": seq,
                        "sht20": [],
                        "ds18b20": [],
                        "gpio": {},
                        "pwm": {"freq": pwm_freq, "channels": channels},
                    },
                    use_cbor=self._cfg.cbor,
                )
                await websocket.send(proto.frame_payload(ack_payload))
        finally:
            telemetry.cancel()
            with contextlib.suppress(Exception):
                await telemetry

    async def run(self) -> None:
        ssl_ctx: Optional[ssl.SSLContext] = None
        if not self._cfg.insecure:
            if not (self._cfg.cert and self._cfg.key):
                raise SystemExit("TLS certificates are required unless --insecure is set")
            ssl_ctx = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
            ssl_ctx.load_cert_chain(self._cfg.cert, self._cfg.key)
        elif self._cfg.cert or self._cfg.key:
            ssl_ctx = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
            ssl_ctx.load_cert_chain(self._cfg.cert, self._cfg.key)

        async with websockets.serve(
            self.handler,
            host=self._cfg.host,
            port=self._cfg.port,
            ssl=ssl_ctx,
            subprotocols=["binary"],
            max_size=2 ** 20,
        ) as server:
            await server.wait_closed()


def parse_args() -> GatewayConfig:
    parser = argparse.ArgumentParser(description="Mock WebSocket gateway for mobile validation")
    parser.add_argument("--host", default="127.0.0.1", help="Interface to bind (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8443, help="Port to listen on (default: 8443)")
    parser.add_argument("--token", help="Bearer token to enforce on incoming connections")
    parser.add_argument("--cbor", action="store_true", help="Enable CBOR framing for payloads")
    parser.add_argument("--update-period", type=float, default=1.0, help="Telemetry push interval in seconds")
    parser.add_argument("--cert", help="Server certificate (PEM)")
    parser.add_argument("--key", help="Server private key (PEM)")
    parser.add_argument("--insecure", action="store_true", help="Disable TLS (ws://) for local testing")
    args = parser.parse_args()
    return GatewayConfig(
        host=args.host,
        port=args.port,
        token=args.token,
        cbor=args.cbor,
        update_period=args.update_period,
        cert=args.cert,
        key=args.key,
        insecure=args.insecure,
    )


def main() -> None:
    cfg = parse_args()
    gateway = MockGateway(cfg)
    try:
        asyncio.run(gateway.run())
    except KeyboardInterrupt:  # pragma: no cover - interactive use
        pass


if __name__ == "__main__":
    main()
