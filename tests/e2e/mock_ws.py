"""Async primitives to emulate the secure WebSocket link between nodes."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from typing import Optional, Tuple

from . import proto


class ProvisioningError(RuntimeError):
    """Raised when the mock provisioning phase fails."""


@dataclass
class MockConnection:
    server_to_client: asyncio.Queue[bytes] = field(default_factory=asyncio.Queue)
    client_to_server: asyncio.Queue[bytes] = field(default_factory=asyncio.Queue)

    async def server_send(self, data: bytes) -> None:
        await self.server_to_client.put(data)

    async def client_send(self, data: bytes) -> None:
        await self.client_to_server.put(data)

    async def server_recv(self) -> bytes:
        return await self.client_to_server.get()

    async def client_recv(self) -> bytes:
        return await self.server_to_client.get()


class MockSensorServer:
    """Minimal emulation of the firmware WebSocket server."""

    def __init__(self, auth_token: Optional[str] = None) -> None:
        self._auth_token = auth_token
        self._connections: list[MockConnection] = []

    async def accept(self, presented_token: Optional[str]) -> MockConnection:
        if self._auth_token and self._auth_token != presented_token:
            raise ProvisioningError("invalid provisioning token")
        conn = MockConnection()
        self._connections.append(conn)
        return conn

    @property
    def connection_count(self) -> int:
        return len(self._connections)

    def iter_connections(self) -> Tuple[MockConnection, ...]:
        """Return a snapshot of active connections."""
        return tuple(self._connections)

    async def broadcast_frame(self, frame: bytes) -> None:
        """Push a preframed payload to every connected HMI client."""
        for conn in self._connections:
            await conn.server_send(frame)

    async def broadcast_update(self, update: dict, *, use_cbor: bool = False) -> bytes:
        """Encode a telemetry update and fan it out to all connections."""
        payload = proto.encode_sensor_update(update, use_cbor=use_cbor)
        frame = proto.frame_payload(payload)
        await self.broadcast_frame(frame)
        return frame


class MockHMIClient:
    """Minimal emulation of the firmware WebSocket client."""

    def __init__(self, token: Optional[str] = None) -> None:
        self._token = token
        self.connection: Optional[MockConnection] = None

    async def connect(self, server: MockSensorServer) -> MockConnection:
        conn = await server.accept(self._token)
        self.connection = conn
        return conn


class SensorNodeHarness:
    """Utility replicating the sensor node data pump."""

    def __init__(self, connection: MockConnection, *, use_cbor: bool = False) -> None:
        self._conn = connection
        self._use_cbor = use_cbor
        self.commands = proto.CommandEffects.empty()

    async def send_update(self, update: dict) -> bytes:
        payload = proto.encode_sensor_update(update, use_cbor=self._use_cbor)
        frame = proto.frame_payload(payload)
        await self._conn.server_send(frame)
        return frame

    async def send_raw_frame(self, frame: bytes) -> None:
        await self._conn.server_send(frame)

    async def receive_command(self, *, timeout: float = 1.0) -> tuple[Optional[dict], bool]:
        frame = await asyncio.wait_for(self._conn.server_recv(), timeout=timeout)
        crc, payload = proto.split_frame(frame)
        expected = proto.compute_crc32(payload)
        if crc != expected:
            return None, False
        command = proto.decode_command(payload, use_cbor=self._use_cbor)
        self.commands.apply(command)
        return command, True


class HMINodeHarness:
    """Utility replicating the HMI node behaviour."""

    def __init__(self, connection: MockConnection, *, use_cbor: bool = False) -> None:
        self._conn = connection
        self._use_cbor = use_cbor
        self.last_crc_ok: Optional[bool] = None
        self.updates: list[dict] = []

    async def receive_update(self, *, timeout: float = 1.0) -> Optional[dict]:
        frame = await asyncio.wait_for(self._conn.client_recv(), timeout=timeout)
        crc, payload = proto.split_frame(frame)
        valid = proto.compute_crc32(payload) == crc
        self.last_crc_ok = valid
        if not valid:
            return None
        update = proto.decode_sensor_update(payload, use_cbor=self._use_cbor)
        self.updates.append(update)
        return update

    async def send_command(self, command: dict) -> bytes:
        payload = proto.encode_command(command, use_cbor=self._use_cbor)
        frame = proto.frame_payload(payload)
        await self._conn.client_send(frame)
        return frame

    async def send_raw_frame(self, frame: bytes) -> None:
        await self._conn.client_send(frame)
