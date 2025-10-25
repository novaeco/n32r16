from __future__ import annotations

import pytest

from .mock_ota import MockOTASource, resilient_download


@pytest.mark.asyncio
async def test_ota_download_recovers_after_transient_drop() -> None:
    payload = bytes((i % 251 for i in range(2048)))
    source = MockOTASource(
        data=payload,
        chunk_size=256,
        drop_after=768,
        transient_failures=1,
    )

    result = await resilient_download(source, max_retries=3, retry_delay=0.0)
    assert result.image == payload
    assert result.drops == 1
    assert result.attempts == 2


@pytest.mark.asyncio
async def test_ota_download_fails_when_retries_exhausted() -> None:
    payload = bytes((i % 199 for i in range(1024)))
    source = MockOTASource(
        data=payload,
        chunk_size=128,
        drop_after=256,
        transient_failures=5,
    )

    with pytest.raises(RuntimeError) as excinfo:
        await resilient_download(source, max_retries=3, retry_delay=0.0)

    message = str(excinfo.value)
    assert "OTA failed" in message
    assert "drops" in message
