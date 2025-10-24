# Dual-Board Environmental Monitoring System

Firmware for a two-node ESP32-S3 platform delivering real-time sensor acquisition, remote GPIO/PWM control, and a rich LVGL-based HMI. The project targets ESP-IDF v5.5 and adheres to Apache-2.0 licensing.

## Hardware Overview

### Sensor Node (ESP32-S3-WROOM-2-N32R16V)
- Dual SHT20 temperature/humidity sensors @ I²C 0x40.
- MCP23017 GPIO expanders @ 0x20 and 0x21 (inputs/outputs, 4.7 kΩ pull-ups on SDA/SCL).
- PCA9685 PWM controller @ 0x41 (500 Hz default, 12-bit duty).
- Four DS18B20 sensors on a shared 1-Wire bus (GPIO8, 4.7 kΩ pull-up).
- Wi-Fi STA, WebSocket server (`/ws`), mDNS advertiser `_hmi-sensor._tcp`.

### HMI Node (Waveshare ESP32-S3 Touch LCD 7B, 1024×600)
- RGB panel driven via `esp_lcd_rgb` helper and LVGL v9.
- GT911 capacitive touch via I²C (tries 0x5D then 0x14 with reset/INT sequence).
- Wi-Fi STA, WebSocket client with mDNS discovery and JSON/CBOR decoding.
- LVGL UI with live sensor dashboard, connection state, and extensible widget hooks.

## Directory Layout

```
├── AGENTS.md                – Repository contribution rules
├── README.md                – This document
├── partitions/              – Shared partition table (32 MB flash, 16 MB PSRAM)
├── components/              – Shared components (cJSON, LVGL, RGB panel helper, TLS cert store)
├── common/                  – Protocol, networking, and utility components
├── sensor_node/             – Firmware project for the acquisition/IO node
└── hmi_node/                – Firmware project for the HMI/touch node
```

## Build Requirements
- ESP-IDF v5.5 (container `espressif/idf:release-v5.5` recommended).
- CMake/Ninja toolchain automatically provided by ESP-IDF.
- Targets: `esp32s3` with PSRAM and 32 MB OPI flash.

## Quick Start

### 1. Configure Toolchain
```bash
. $IDF_PATH/export.sh
```

### 2. Build Sensor Node
```bash
cd sensor_node
idf.py set-target esp32s3
idf.py build
```

### 3. Build HMI Node
```bash
cd ../hmi_node
idf.py set-target esp32s3
idf.py build
```

### 4. Flash & Monitor
Replace `/dev/ttyUSBx` with your serial device.
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Wi-Fi & Networking
- **Provisioning** – Both firmwares leverage the ESP-IDF provisioning manager in secure SoftAP mode with proof-of-possession. At
  boot, the node exposes `SENSOR-XXYYZZ` / `HMI-XXYYZZ` (suffix configurable via `CONFIG_SENSOR_PROV_SERVICE_NAME` and
  `CONFIG_HMI_PROV_SERVICE_NAME`). Provide the POP values from `CONFIG_SENSOR_PROV_POP` / `CONFIG_HMI_PROV_POP` via the companion
  provisioning tool to inject Wi-Fi credentials, which are stored in the encrypted NVS partition.
- **Bearer-token authenticated TLS** – The sensor node now serves `wss://` on `CONFIG_SENSOR_WS_PORT` using the PEM materials
  embedded by `components/cert_store`. Clients must present the bearer token configured in `CONFIG_SENSOR_WS_AUTH_TOKEN`. The HMI
  node validates the server certificate against the CA bundle supplied by `cert_store` and injects its own bearer token via
  `CONFIG_HMI_WS_AUTH_TOKEN`.
- **Service discovery** – mDNS advertising remains on `_hmi-sensor._tcp` but now publishes TXT records describing the secure
  transport (`proto=wss`, `auth=bearer`). The HMI attempts discovery before falling back to
  `CONFIG_HMI_SENSOR_HOSTNAME:CONFIG_HMI_SENSOR_PORT`.
- **Payloads** – JSON remains the default payload format with optional TinyCBOR support toggled via `CONFIG_USE_CBOR`. All frames
  continue to prepend a CRC32 (little-endian) for integrity checks.
- **OTA updates** – Both firmwares schedule HTTPS OTA fetches on boot using `CONFIG_SENSOR_OTA_URL` / `CONFIG_HMI_OTA_URL` and the
  trust anchors bundled in `cert_store`. Bootloader rollback is enabled; ensure production images share matching security
  configuration.

## Wiring Summary

| Peripheral        | Sensor Node GPIO | Notes                                  |
|-------------------|------------------|----------------------------------------|
| I²C SDA/SCL       | 17 / 18          | 4.7 kΩ pull-ups, 400 kHz               |
| DS18B20 1-Wire    | 8                | 4.7 kΩ pull-up                         |
| Status LED        | 2                | Active high heartbeat                  |

| Display Signal | HMI GPIO | Description |
|----------------|----------|-------------|
| DE             | 5        | Data enable |
| PCLK           | 7        | Pixel clock (~50 MHz) |
| HSYNC / VSYNC  | 46 / 3   | Synchronization |
| DISP_EN        | 6        | Panel enable |
| RGB Data       | 16-bit bus mapped per `board_waveshare7b_pins.h` |
| Touch SDA/SCL  | 8 / 9    | GT911 I²C bus |
| Touch INT/RST  | 4 / 2    | Reset/interrupt sequencing |

## Firmware Modules

### Common Components
- `common/proto`: JSON/CBOR schema handling, CRC32 utilities, command/sensor serialization.
- `common/net`: Wi-Fi station helper, mDNS wrapper, WebSocket server/client abstractions.
- `common/util`: Monotonic timing, SNTP sync hook, lightweight ring buffer.

### Sensor Node Tasks
- `t_sensors`: Reads SHT20/DS18B20 with EMA-friendly cadence (200 ms), provides synthetic demo data when hardware is absent.
- `t_io`: Manages MCP23017 polling and PCA9685 PWM updates with FreeRTOS queue-based command handling.
- `t_heartbeat`: Visual watchdog on GPIO2.
- `net/ws_server`: Hosts WebSocket endpoint, validates CRC, applies remote commands to IO tasks.

### HMI Node Tasks
- `t_net_rx`: Establishes Wi-Fi, resolves mDNS, maintains WebSocket client with reconnection.
- `t_ui`: Initializes LVGL, renders dashboards, and applies incoming sensor data.
- `t_heartbeat`: HMI board status LED heartbeat.

## WebSocket Protocol

### Sensor → HMI (binary frame)
```
uint32_t crc32
<JSON or CBOR payload>
```
Payload fields follow schema `sensor_update` version 1 with timestamp (`ts`), sequence (`seq`), sensor arrays, GPIO bitmaps, and PCA9685 state.

### HMI → Sensor (binary frame)
```
uint32_t crc32
<JSON or CBOR command payload>
```
Supported commands: PWM duty updates, PWM frequency change, and MCP23017 GPIO writes with mask/value semantics. Future acknowledgments can leverage the `seq` field.

## Continuous Integration
GitHub Actions (`.github/workflows/ci.yml`) builds both projects inside an ESP-IDF 5.5 container with ccache acceleration. Ensure commits maintain `idf.py build` success for both applications.

## Testing Hooks
- Unit-test ready harness stubs reside in protocol components (CRC verification, JSON/CBOR decode). Extend via `idf.py -T` with custom tests as needed.
- Manual validation checklist:
  1. Sensor node boots, advertises `_hmi-sensor._tcp`, blinks heartbeat.
  2. HMI node discovers service, shows Wi-Fi connected, renders live telemetry ≤200 ms latency.
  3. MCP23017 state toggles reflect immediately when actuated from HMI.
  4. PCA9685 slider adjustments propagate to sensor node PWM outputs.
  5. Network loss surfaces UI banner (connection state label switches red) and auto-recovers in <5 s.

## License
Apache License 2.0. See `LICENSE` file if added (default ESP-IDF templates).
