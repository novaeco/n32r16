#!/usr/bin/env python3
"""
WebSocket diagnostic tool for the sensor/HMI nodes.

Connects to the sensor node WebSocket endpoint, sends a control command in JSON or CBOR,
and validates CRC32 envelopes returned by the firmware.
"""

import argparse
import asyncio
import json
import ssl
import struct
import time
import zlib
from typing import Any, Dict

try:
    import websockets
except ImportError as exc:  # pragma: no cover - dependency error surfaced to user
    raise SystemExit(
        "The 'websockets' package is required. Install with 'pip install websockets'."
    ) from exc

CBOR_AVAILABLE = False
try:  # pragma: no cover - optional dependency check
    import cbor2  # type: ignore

    CBOR_AVAILABLE = True
except ImportError:
    CBOR_AVAILABLE = False


def build_command(args: argparse.Namespace) -> Dict[str, Any]:
    now_ms = int(time.time() * 1000)
    envelope: Dict[str, Any] = {"v": 1, "type": "cmd", "ts": now_ms, "seq": args.sequence}
    if args.pwm_channel is not None and args.pwm_duty is not None:
        envelope["set_pwm"] = {"ch": args.pwm_channel, "duty": args.pwm_duty}
    if args.pwm_freq is not None:
        envelope["pwm_freq"] = {"freq": args.pwm_freq}
    if args.gpio_device is not None and args.gpio_mask is not None:
        port = "A" if args.gpio_port.upper() == "A" else "B"
        envelope["write_gpio"] = {
            "dev": f"mcp{args.gpio_device}",
            "port": port,
            "mask": args.gpio_mask,
            "value": args.gpio_value if args.gpio_value is not None else args.gpio_mask,
        }
    return envelope


def encode_payload(envelope: Dict[str, Any], use_cbor: bool) -> bytes:
    if use_cbor:
        if not CBOR_AVAILABLE:
            raise RuntimeError("CBOR encoding requested but the 'cbor2' package is not available")
        return cbor2.dumps(envelope)
    return json.dumps(envelope, separators=(",", ":")).encode("utf-8")


async def send_command(args: argparse.Namespace) -> None:
    ssl_ctx: ssl.SSLContext
    if args.insecure:
        ssl_ctx = ssl.create_default_context()
        ssl_ctx.check_hostname = False
        ssl_ctx.verify_mode = ssl.CERT_NONE
    else:
        ssl_ctx = ssl.create_default_context()
        if args.ca_cert:
            ssl_ctx.load_verify_locations(args.ca_cert)
    headers = {}
    if args.token:
        headers["Authorization"] = f"Bearer {args.token}"

    uri = f"wss://{args.host}:{args.port}{args.path}"
    command = build_command(args)
    payload = encode_payload(command, args.cbor)
    frame = struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF) + payload

    async with websockets.connect(uri, ssl=ssl_ctx, extra_headers=headers) as ws:
        await ws.send(frame)
        print(f"Command dispatched (seq={command['seq']})")
        remaining = args.expect
        while remaining > 0:
            try:
                message = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
            except asyncio.TimeoutError:
                print("Timed out while waiting for sensor update")
                break
            if isinstance(message, str):
                print(f"Text frame: {message}")
                continue
            if len(message) < 4:
                print("Received undersized binary frame")
                continue
            crc_recv, = struct.unpack_from("<I", message, 0)
            body = message[4:]
            crc_calc = zlib.crc32(body) & 0xFFFFFFFF
            status = "OK" if crc_recv == crc_calc else "MISMATCH"
            print(f"Update received (CRC {status}, recv=0x{crc_recv:08X}, calc=0x{crc_calc:08X})")
            try:
                if args.cbor:
                    decoded = cbor2.loads(body)
                else:
                    decoded = json.loads(body.decode("utf-8"))
                print(json.dumps(decoded, indent=2))
            except Exception as exc:  # pragma: no cover - diagnostic only
                print(f"Failed to decode payload: {exc}")
            remaining -= 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Diagnostic WebSocket client for the sensor node")
    parser.add_argument("host", help="Sensor node hostname or IP")
    parser.add_argument("--port", type=int, default=443, help="WebSocket port (default: 443)")
    parser.add_argument("--path", default="/ws", help="WebSocket path (default: /ws)")
    parser.add_argument("--token", help="Bearer token for authentication")
    parser.add_argument("--ca-cert", help="Custom CA certificate path")
    parser.add_argument("--insecure", action="store_true", help="Disable TLS verification (testing only)")
    parser.add_argument("--sequence", type=int, default=1, help="Sequence identifier for the command")
    parser.add_argument("--pwm-channel", type=int, help="PCA9685 channel to control")
    parser.add_argument("--pwm-duty", type=int, help="12-bit duty cycle value")
    parser.add_argument("--pwm-freq", type=int, help="New PCA9685 frequency in Hz")
    parser.add_argument("--gpio-device", type=int, choices=[0, 1], help="MCP23017 device index")
    parser.add_argument("--gpio-port", default="A", choices=["A", "B", "a", "b"], help="GPIO port selector")
    parser.add_argument("--gpio-mask", type=lambda x: int(x, 0), help="GPIO mask (e.g. 0x03)")
    parser.add_argument("--gpio-value", type=lambda x: int(x, 0), help="GPIO value (default = mask)")
    parser.add_argument("--expect", type=int, default=1, help="Number of sensor updates to display")
    parser.add_argument("--timeout", type=float, default=5.0, help="Receive timeout in seconds")
    parser.add_argument("--cbor", action="store_true", help="Use CBOR encoding instead of JSON")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.cbor and not CBOR_AVAILABLE:
        raise SystemExit("CBOR support requires the 'cbor2' package. Install it or omit --cbor.")
    if args.pwm_channel is not None and args.pwm_duty is None:
        raise SystemExit("--pwm-duty is required when --pwm-channel is specified")
    if args.gpio_device is None and (args.gpio_mask is not None or args.gpio_value is not None):
        raise SystemExit("--gpio-device must be provided for GPIO operations")
    asyncio.run(send_command(args))


if __name__ == "__main__":
    main()
