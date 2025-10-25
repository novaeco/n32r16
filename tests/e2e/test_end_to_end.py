from __future__ import annotations

import asyncio
import copy
from typing import Dict

import pytest

from . import proto
from .mock_ws import (
    HMINodeHarness,
    MockHMIClient,
    MockSensorServer,
    ProvisioningError,
    SensorNodeHarness,
)


@pytest.fixture
def sample_update() -> Dict[str, object]:
    duty_cycle = [i * 100 for i in range(16)]
    return {
        "timestamp_ms": 123456,
        "sequence_id": 42,
        "sht20": [{"id": "rack-top", "t": 24.5, "rh": 55.2}],
        "ds18b20": [{"rom": "0011223344556677", "t": 21.75}],
        "gpio": {
            "mcp0": {"A": 0xAAAA, "B": 0x5555},
            "mcp1": {"A": 0x0F0F, "B": 0xF0F0},
        },
        "pwm": {"pca9685": {"freq": 1200, "duty": duty_cycle}},
    }


@pytest.fixture
def sample_command() -> Dict[str, object]:
    return {
        "timestamp_ms": 654321,
        "sequence_id": 7,
        "set_pwm": {"ch": 4, "duty": 16384},
        "pwm_freq": {"freq": 3200},
        "write_gpio": {"dev": "mcp0", "port": "B", "mask": 0x0003, "value": 0x0001},
    }


@pytest.mark.asyncio
async def test_provisioning_success() -> None:
    server = MockSensorServer(auth_token="super-secret")
    client = MockHMIClient(token="super-secret")
    conn = await client.connect(server)
    assert conn is not None
    assert server.connection_count == 1


@pytest.mark.asyncio
async def test_provisioning_rejects_invalid_token() -> None:
    server = MockSensorServer(auth_token="expected-token")
    client = MockHMIClient(token="wrong-token")
    with pytest.raises(ProvisioningError):
        await client.connect(server)
    assert server.connection_count == 0


@pytest.mark.asyncio
async def test_sensor_update_crc_and_state(sample_update: Dict[str, object]) -> None:
    server = MockSensorServer(auth_token="crc-token")
    client = MockHMIClient(token="crc-token")
    conn = await client.connect(server)
    sensor = SensorNodeHarness(conn)
    hmi = HMINodeHarness(conn)

    await sensor.send_update(sample_update)
    update = await hmi.receive_update()
    assert update is not None
    assert update["seq"] == sample_update["sequence_id"]
    assert hmi.last_crc_ok is True

    payload = proto.encode_sensor_update(sample_update, use_cbor=False)
    frame = proto.frame_payload(payload)
    tampered = bytearray(frame)
    tampered[0] ^= 0xFF
    await sensor.send_raw_frame(bytes(tampered))
    corrupted = await hmi.receive_update()
    assert corrupted is None
    assert hmi.last_crc_ok is False


@pytest.mark.asyncio
async def test_command_round_trip(sample_command: Dict[str, object]) -> None:
    server = MockSensorServer(auth_token="cmd-token")
    client = MockHMIClient(token="cmd-token")
    conn = await client.connect(server)
    sensor = SensorNodeHarness(conn)
    hmi = HMINodeHarness(conn)

    await hmi.send_command(sample_command)
    command, ok = await sensor.receive_command()
    assert ok is True
    assert command is not None
    assert command["set_pwm"]["ch"] == sample_command["set_pwm"]["ch"]
    assert sensor.commands.pwm_channels[4] == sample_command["set_pwm"]["duty"]
    assert sensor.commands.pwm_frequency == sample_command["pwm_freq"]["freq"]
    assert sensor.commands.gpio[(0, "B")] == (
        sample_command["write_gpio"]["mask"],
        sample_command["write_gpio"]["value"],
    )

    payload = proto.encode_command(sample_command, use_cbor=False)
    frame = proto.frame_payload(payload)
    tampered = bytearray(frame)
    tampered[-1] ^= 0xAA
    await hmi.send_raw_frame(bytes(tampered))
    command, ok = await sensor.receive_command()
    assert ok is False
    assert command is None


@pytest.mark.asyncio
async def test_multi_client_broadcast_and_commands(
    sample_update: Dict[str, object], sample_command: Dict[str, object]
) -> None:
    server = MockSensorServer(auth_token="multi-token")
    clients = [MockHMIClient(token="multi-token") for _ in range(3)]
    connections = [await client.connect(server) for client in clients]

    assert server.connection_count == 3

    hmis = [HMINodeHarness(conn) for conn in connections]
    sensors = [SensorNodeHarness(conn) for conn in connections]

    await server.broadcast_update(sample_update)
    updates = await asyncio.gather(*(hmi.receive_update() for hmi in hmis))
    for update in updates:
        assert update is not None
        assert update["seq"] == sample_update["sequence_id"]

    for index, hmi in enumerate(hmis):
        command = copy.deepcopy(sample_command)
        command["sequence_id"] = index + 100
        command["set_pwm"]["ch"] = index
        command["set_pwm"]["duty"] = 1024 * (index + 1)
        await hmi.send_command(command)

    results = await asyncio.gather(*(sensor.receive_command() for sensor in sensors))
    for index, (command, ok) in enumerate(results):
        assert ok is True
        assert command is not None
        assert command["seq"] == index + 100
        assert sensors[index].commands.pwm_channels[index] == 1024 * (index + 1)


def test_split_frame_rejects_short_frame() -> None:
    with pytest.raises(ValueError):
        proto.split_frame(b"\x01\x02")
