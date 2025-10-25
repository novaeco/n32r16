"""Utilities to emulate OTA transfers with deterministic failures."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field


class OTAConnectionLost(ConnectionError):
    """Raised when the simulated OTA transport drops mid-transfer."""

    def __init__(self, offset: int, partial: bytes) -> None:
        super().__init__(f"Simulated OTA link drop at offset {offset}")
        self.offset = offset
        self.partial = partial


@dataclass
class MockOTASource:
    """Deterministic OTA stream with optional transient failures."""

    data: bytes
    chunk_size: int = 1024
    drop_after: int | None = None
    transient_failures: int = 1
    _failures_injected: int = field(default=0, init=False)

    async def fetch(self, start: int = 0) -> bytes:
        """Return payload bytes starting at *start* or raise on drop."""
        await asyncio.sleep(0)
        offset = start
        downloaded = bytearray()
        total = len(self.data)

        while offset < total:
            end = min(offset + self.chunk_size, total)
            downloaded.extend(self.data[offset:end])
            offset = end
            if (
                self.drop_after is not None
                and offset >= self.drop_after
                and self._failures_injected < self.transient_failures
            ):
                self._failures_injected += 1
                raise OTAConnectionLost(offset, bytes(downloaded))
        return bytes(downloaded)


@dataclass
class OTAResult:
    image: bytes
    attempts: int
    drops: int


async def resilient_download(
    source: MockOTASource,
    *,
    max_retries: int = 3,
    retry_delay: float = 0.0,
) -> OTAResult:
    """Download *source* while tolerating transient OTA failures."""

    offset = 0
    buffer = bytearray()
    attempts = 0
    drops = 0

    while attempts < max_retries:
        attempts += 1
        try:
            chunk = await source.fetch(offset)
        except OTAConnectionLost as exc:
            if exc.partial:
                buffer.extend(exc.partial)
                offset = len(buffer)
            drops += 1
            if retry_delay:
                await asyncio.sleep(retry_delay)
            continue

        if chunk:
            buffer.extend(chunk)
        offset = len(buffer)
        return OTAResult(image=bytes(buffer), attempts=attempts, drops=drops)

    raise RuntimeError(
        f"OTA failed after {attempts} attempts with {drops} drops and {len(buffer)} bytes buffered"
    )
