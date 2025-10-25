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
- LVGL UI delivering a tabbed experience:
  - Dashboard with per-sensor cards, Wi-Fi/CRC badges, and sequence tracking.
  - GPIO pages (MCP0/MCP1) with 16 interactive toggles plus live state mirrors.
  - PWM controls covering frequency and 16 channel sliders.
  - Traces view charting 512-point histories for temperature and humidity (SHT20/DS18B20).
  - Settings panel for Wi-Fi credentials, mDNS target override, theme, and unit preference (°C/°F).

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

## Code Quality & Static Analysis
- **Clang-Format** – Use the repository root `.clang-format` profile: `clang-format -i $(git ls-files '*.c' '*.h')` prior to
  committing changes. Ensure you are running LLVM clang-format 16 or newer so that the `Standard: c17` directive is
  recognised; older releases will silently ignore the rule set.
- **Clang-Tidy** – After running `idf.py build` (to generate `build/compile_commands.json`), execute
  `clang-tidy -p build $(git ls-files '*.c')` from the corresponding project directory.
- **Unit Tests** – Execute `idf.py -T` within `sensor_node/` and `common/proto/` to run mocked driver (SHT20, DS18B20, MCP23017,
  PCA9685) and protocol CRC/serialization tests whenever functionality changes.
  committing changes.
- **Clang-Tidy** – After running `idf.py build` (to generate `build/compile_commands.json`), execute
  `clang-tidy -p build $(git ls-files '*.c')` from the corresponding project directory.
- **Unit Tests** – Execute `idf.py -T` to run the protocol and driver unit tests when relevant changes are made.

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

### 4. Run Driver/Protocol Unit Tests
```bash
idf.py -T
```

### 5. Flash & Monitor
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
  continue to prepend a CRC32 (little-endian) for integrity checks, and the HMI dashboard surfaces CRC status for quick diagnostics.
- **OTA updates** – Both firmwares schedule HTTPS OTA fetches on boot using `CONFIG_SENSOR_OTA_URL` / `CONFIG_HMI_OTA_URL` and the
  trust anchors bundled in `cert_store`. Bootloader rollback is enabled; ensure production images share matching security
  configuration.

## Wiring Summary

| Peripheral        | Sensor Node GPIO | Notes                                                                               |
|-------------------|------------------|-------------------------------------------------------------------------------------|
| I²C SDA/SCL       | 17 / 18          | 4.7 kΩ pull-ups, 400 kHz, automatic bus recovery and collision detection            |
| DS18B20 1-Wire    | 8                | 4.7 kΩ pull-up; non-blocking state machine with configurable scan interval          |
| Status LED        | 2                | Active high heartbeat                                                               |

| Display Signal | HMI GPIO | Description |
|----------------|----------|-------------|
| DE             | 5        | Data enable |
| PCLK           | 7        | Pixel clock (~50 MHz) |
| HSYNC / VSYNC  | 46 / 3   | Synchronization |
| DISP_EN        | 6        | Panel enable |
| RGB Data       | 16-bit bus mapped per `board_waveshare7b_pins.h` |
| Touch SDA/SCL  | 8 / 9    | GT911 I²C bus |

Detailed wiring matrices, PlantUML sequence diagrams, and hardware adaptation advice are consolidated in
[`documentations/hardware_wiring.md`](documentations/hardware_wiring.md).

## Release, QA, and Coverage Workflow

- Follow the end-to-end process captured in [`documentations/release_and_testing.md`](documentations/release_and_testing.md)
  covering git-tagged releases, issue triage, PR checklists, and CI requirements.
- Enable GCOV instrumentation across shared components and both applications via `idf.py menuconfig`:
  - *Component config* → *common_util* → *Common Components Options* → *Enable gcov instrumentation for shared components*
    (`CONFIG_COMMON_ENABLE_GCOV`).
  - *Sensor Node Options* → *Enable gcov instrumentation for unit tests* (`CONFIG_SENSOR_ENABLE_GCOV`).
  - *HMI Node Options* → *Enable gcov instrumentation for unit tests* (`CONFIG_HMI_ENABLE_GCOV`).
  Rebuild each project with `idf.py -T`, execute the Unity tests, then aggregate coverage:

  ```bash
  python tools/run_coverage.py --xml coverage.xml --html coverage.html --summary coverage.txt
  ```

- The script consumes all configured build directories (defaults to `sensor_node/build` and `hmi_node/build`), emits
  consolidated Cobertura/HTML/TXT artefacts, and refreshes `coverage_badge.svg` at the repository root for CI badges.

- The new Unity suite `tests/test_data_model.c` validates the publication heuristics and encoder buffer rotation to
  guarantee deterministic telemetry emission.
- API documentation is generated with Doxygen using `documentations/doxygen/Doxyfile`; the build artefacts are part of the
  release assets.
| Touch INT/RST  | 4 / 2    | Reset/interrupt sequencing |

## Firmware Modules

### Common Components
- `common/proto`: JSON/CBOR schema handling, CRC32 utilities, command/sensor serialization.
- `common/net`: Wi-Fi station helper, mDNS wrapper, WebSocket server/client abstractions.
- `common/util`: Monotonic timing, SNTP sync hook, lightweight ring buffer.

### Sensor Node Tasks
- `t_sensors`: Orchestrates asynchronous SHT20/DS18B20 cycles with EMA filtering, address scanning, and demo data fallback when hardware is absent.
- `t_io`: Manages MCP23017 polling and PCA9685 PWM updates with FreeRTOS queue-based command handling.
- `t_heartbeat`: Visual watchdog on GPIO2.
- `net/ws_server`: Hosts WebSocket endpoint, validates CRC, applies remote commands to IO tasks.

### HMI Node Tasks
- `t_net_rx`: Establishes Wi-Fi, resolves mDNS, maintains WebSocket client with reconnection.
- `t_ui`: Initializes LVGL, manages multi-tab UI state, dispatches GPIO/PWM commands, and applies incoming sensor data with CRC/status feedback.
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
- Automated: `idf.py -T` exercises driver shims (SHT20 retries/backoff, DS18B20 decoding, MCP23017 masking, PCA9685 prescaler) and protocol encode/decode CRC verification.
- Manual validation checklist:
  1. Sensor node boots, advertises `_hmi-sensor._tcp`, blinks heartbeat.
  2. HMI node discovers service, shows Wi-Fi connected, renders live telemetry ≤200 ms latency.
  3. MCP23017 state toggles reflect immediately when actuated from HMI.
  4. PCA9685 slider adjustments propagate to sensor node PWM outputs.
  5. Network loss surfaces UI banner (connection state label switches red) and auto-recovers in <5 s.

## Tooling
- `tools/ws_diagnostic.py` – Python 3 utility to publish WebSocket commands (JSON or CBOR) and validate CRC-tagged sensor updates.
  ```bash
  ./tools/ws_diagnostic.py SENSOR_HOST --token <auth> --pwm-channel 0 --pwm-duty 2048 --expect 2
  ```

## License
Apache License 2.0. See `LICENSE` file if added (default ESP-IDF templates).
