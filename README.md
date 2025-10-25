# Dual-Board Environmental Monitoring System

Firmware for a two-node ESP32-S3 platform delivering real-time sensor acquisition, remote GPIO/PWM control, and a rich LVGL-based HMI. The project targets ESP-IDF v5.5 and adheres to Apache-2.0 licensing.

## Hardware Overview

### Sensor Node (ESP32-S3-WROOM-2-N32R16V)
- Dual ambient sensors on I²C with Kconfig-selectable driver (`CONFIG_SENSOR_AMBIENT_SENSOR_SHT20` default, `CONFIG_SENSOR_AMBIENT_SENSOR_BME280` optional).
- Optional TCA9548A I²C multiplexer (`CONFIG_SENSOR_AMBIENT_TCA9548A_ENABLE`) isolates dual SHT20 probes onto distinct downstream buses, preventing the fixed 0x40 address collision.
- MCP23017 GPIO expanders @ 0x20 and 0x21 (inputs/outputs, 4.7 kΩ pull-ups on SDA/SCL).
- External PWM backend selectable via `CONFIG_SENSOR_PWM_BACKEND` (`pca9685` default at 500 Hz, 12-bit duty) with software fallback when disabled.
- Four DS18B20 sensors on a shared 1-Wire bus (`CONFIG_SENSOR_ONEWIRE_GPIO` default GPIO8, 4.7 kΩ pull-up).
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
- **Tests E2E sécurité** – Valider la dérivation des clés WebSocket et les vecteurs HMAC via `pytest tests/e2e/test_ws_handshake_vectors.py`.

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

> 📎 En cas d'échec TLS lors de `./install.sh`, consultez la section « Dépannage des téléchargements ESP-IDF » du
> [guide d'installation](documentations/installation_guide.md#d%C3%A9pannage-des-t%C3%A9l%C3%A9chargements-esp-idf-erreurs-tlsca).

## Target Profiles

The repository ships tuned configuration overlays for memory-constrained ESP32 variants. Combine them with the default
`sdkconfig.defaults` via the `SDKCONFIG_DEFAULTS` CMake option:

| Module           | Flash size | PSRAM | Overlay                                      |
|------------------|------------|-------|----------------------------------------------|
| ESP32-S3         | 32 MB OPI  | 16 MB | _default_ (no overlay required)              |
| ESP32-S2-WROVER  | 8 MB QIO   | 2 MB  | `configs/esp32s2/sdkconfig.defaults`         |
| ESP32-C3 modules | 4 MB DIO   | —     | `configs/esp32c3/sdkconfig.defaults`         |

Example for the sensor node on ESP32-S2:

```bash
idf.py set-target esp32s2 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;configs/esp32s2/sdkconfig.defaults" build
```

The overlays adjust flash geometry, CPU frequency, and partition tables (`partitions/esp32s2_8MB.csv`,
`partitions/esp32c3_4MB.csv`) to maintain OTA headroom while honouring the reduced memory footprint. The default Wi-Fi and
provisioning credentials remain identical across profiles to ease mixed deployments.

## Adaptive Memory Management

Both applications initialise the shared `common/util/memory_profile` helper at boot to capture chip model, flash/PSRAM geometry,
and heap availability. The data is surfaced in the boot log and consumed by the HMI LVGL port to right-size the double-buffer:
PSRAM-rich boards receive full-frame buffers while constrained targets fall back to fractional screen slices (minimum 40 lines).
The helper is available to other components via `memory_profile_get()` for future dynamic allocations.

### Sensor Node Kconfig Highlights

- **Ambient sensor type** (`CONFIG_SENSOR_AMBIENT_SENSOR_*`) toggles between the legacy dual SHT20 stack and the Bosch BME280 backend with full calibration handling.
- **Ambient mux** (`CONFIG_SENSOR_AMBIENT_TCA9548A_ENABLE`, `CONFIG_SENSOR_TCA9548A_CH*`) drives the onboard TCA9548A to fan out the SHT20 pair across independent channels and exposes address/slot overrides.
- **1-Wire GPIO selector** (`CONFIG_SENSOR_ONEWIRE_GPIO`) adapts the DS18B20 bus to alternate ESP32-S3 pinouts without patching board headers.
- **PWM backend** (`CONFIG_SENSOR_PWM_BACKEND`, `CONFIG_SENSOR_PWM_BACKEND_DRIVER_*`) enables PCA9685 control, prepares for TLC5947 SPI expansion, or disables hardware outputs for bench simulation.

## Wi-Fi & Networking
- **Provisioning** – Both firmwares now enforce ESP-IDF Security 2 (SRP6a). Supply Base64-encoded salt/verifier material through
  `CONFIG_SENSOR_PROV_SEC2_SALT_BASE64` / `CONFIG_SENSOR_PROV_SEC2_VERIFIER_BASE64` and `CONFIG_HMI_PROV_SEC2_SALT_BASE64` /
  `CONFIG_HMI_PROV_SEC2_VERIFIER_BASE64`, and align the SRP username using `CONFIG_SENSOR_PROV_SEC2_USERNAME` /
  `CONFIG_HMI_PROV_SEC2_USERNAME`. The provisioning SSID broadcasts `SENSOR-XXYYZZ` / `HMI-XXYYZZ` (suffix configurable via
  `CONFIG_SENSOR_PROV_SERVICE_NAME` and `CONFIG_HMI_PROV_SERVICE_NAME`). Proof-of-possession secrets remain configurable for
  legacy tooling compatibility via `CONFIG_SENSOR_PROV_POP` / `CONFIG_HMI_PROV_POP`. All credentials are persisted in the
  encrypted NVS partition.
- **Bearer-token authenticated TLS** – The sensor node now serves `wss://` on `CONFIG_SENSOR_WS_PORT` using the PEM materials
  embedded by `components/cert_store`. Clients must present the bearer token configured in `CONFIG_SENSOR_WS_AUTH_TOKEN`. The HMI
  node validates the server certificate against the CA bundle supplied by `cert_store` and injects its own bearer token via
  `CONFIG_HMI_WS_AUTH_TOKEN`.
- **HMAC handshake hardening** – Optional second-factor authentication adds per-connection nonces (`X-WS-Nonce`) signed via
  `X-WS-Signature`. Enable `CONFIG_SENSOR_WS_ENABLE_HANDSHAKE` / `CONFIG_HMI_WS_ENABLE_HANDSHAKE` and populate
  `CONFIG_SENSOR_WS_CRYPTO_SECRET_BASE64` / `CONFIG_HMI_WS_CRYPTO_SECRET_BASE64`. Replay detection is governed by
  `CONFIG_SENSOR_WS_HANDSHAKE_TTL_MS` and `CONFIG_SENSOR_WS_HANDSHAKE_CACHE_SIZE`.
- **TOTP two-factor header** – Enabling `CONFIG_SENSOR_WS_ENABLE_TOTP` / `CONFIG_HMI_WS_ENABLE_TOTP` requires clients to present
  an `X-WS-TOTP` header generated from a shared Base32 secret (`*_WS_TOTP_SECRET_BASE32`). The default 30-second, 8-digit TOTP
  profile tolerates ±1 step drift. Detailed provisioning steps live in
  [`documentations/security_websocket_totp.md`](documentations/security_websocket_totp.md).
- **AES-GCM payload confidentiality** – Activate `CONFIG_SENSOR_WS_ENABLE_ENCRYPTION` and `CONFIG_HMI_WS_ENABLE_ENCRYPTION` to
  wrap CRC-framed telemetry/command payloads in 256-bit AES-GCM envelopes. Ciphertext length expands by
  `WS_SECURITY_HEADER_LEN + WS_SECURITY_TAG_LEN` (28 bytes) relative to the plaintext frame.
- **Service discovery** – mDNS advertising remains on `_hmi-sensor._tcp` but the HMI now consumes TXT metadata (`proto`,
  `path`, optional `host`/`sni`) and IPv6 A/AAAA answers to build the WebSocket URI. Successful discoveries persist the URI/SNI
  pair in encrypted NVS with an expiry governed by `CONFIG_HMI_DISCOVERY_CACHE_TTL_MINUTES`, so stale endpoints are purged
  automatically. The timeout before falling back to the static hostname is tuned via `CONFIG_HMI_DISCOVERY_TIMEOUT_MS`, and
  production deployments can pin the TLS Server Name Indication with `CONFIG_HMI_WS_TLS_SNI_OVERRIDE`.
- **Payloads** – JSON remains the default payload format with optional TinyCBOR support toggled via `CONFIG_USE_CBOR`. All frames
  continue to prepend a CRC32 (little-endian) for integrity checks, and the HMI dashboard surfaces CRC status for quick diagnostics.
- **OTA updates** – Both firmwares schedule HTTPS OTA fetches on boot using `CONFIG_SENSOR_OTA_URL` / `CONFIG_HMI_OTA_URL` and the
  trust anchors bundled in `cert_store`. Bootloader rollback is enabled; ensure production images share matching security
  configuration.

## Security & Credential Management

- **Compile-time credential enforcement** – Building either firmware now fails unless the bearer tokens, provisioning POPs and OTA
  manifest URLs have been replaced. Toggle `CONFIG_SENSOR_ALLOW_PLACEHOLDER_SECRETS` / `CONFIG_HMI_ALLOW_PLACEHOLDER_SECRETS`
  temporarily when developing with placeholders, otherwise update the string options under *Sensor Node Options* and *HMI Node
  Options*. Runtime assertions also guard against trivially short secrets.
- **Replay-resistant WebSocket handshakes** – When handshake HMAC is enabled the sensor caches recent nonces for
  `CONFIG_SENSOR_WS_HANDSHAKE_TTL_MS` and refuses duplicates, enforcing forward secrecy across reconnect attempts. The HMI client
  regenerates nonce/signature pairs on every reconnect via `regenerate_headers()`.
- **Certificate overrides** – `components/cert_store` first searches for PEM blobs in NVS (`CONFIG_CERT_STORE_OVERRIDE_FROM_NVS`)
  and then in a SPIFFS partition (`CONFIG_CERT_STORE_OVERRIDE_FROM_SPIFFS`). When no override is found the compiled-in assets in
  `components/cert_store/certs/` are used. The shared partition table defines the SPIFFS partition `storage` at offset
  `0x414000` (size 1 MiB).

### Provisioning new PEM blobs via NVS

1. Generate a CSV manifest with base64-encoded PEM payloads:

   ```bash
   python - <<'PY'
   import base64, csv

   def encode(path: str) -> str:
       with open(path, "rb") as fh:
           return base64.b64encode(fh.read()).decode()

   rows = [
       ("key", "type", "encoding", "value"),
       ("server_cert", "data", "base64", encode("server_cert.pem")),
       ("server_key", "data", "base64", encode("server_key.pem")),
       ("ca_cert", "data", "base64", encode("ca_cert.pem")),
   ]

   with open("cert_store.csv", "w", newline="") as fh:
       csv.writer(fh).writerows(rows)
   PY
   ```
2. Build the NVS image for the 0x6000-byte partition at `0xB000`:

   ```bash
   python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py \
       generate cert_store.csv cert_store_nvs.bin 0x6000
   ```
3. Flash the image to the ESP32-S3:

   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 write_flash 0xB000 cert_store_nvs.bin
   ```

## Demonstrations

Annotated UI walkthroughs and wiring diagrams live in `documentations/hardware/board_profiles.md`. To capture a fresh demo, the
workflow described in `documentations/demo_capture.md` explains how to grab LVGL frame buffers (`tools/lvgl_snapshot.py`) and
record synchronized WebSocket traffic using OBS/FFmpeg.

### Provisioning new PEM blobs via SPIFFS

1. Populate a staging directory with the replacement PEM files:

   ```bash
   mkdir -p cert_spiffs
   cp server_cert.pem cert_spiffs/
   cp server_key.pem cert_spiffs/
   cp ca_cert.pem cert_spiffs/
   ```
2. Build the SPIFFS image (partition size 1 MiB at `0x414000`):

   ```bash
   python $IDF_PATH/components/spiffs/spiffsgen.py 0x100000 cert_spiffs cert_store_spiffs.bin
   ```
3. Flash the image:

   ```bash
   esptool.py --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 write_flash 0x414000 cert_store_spiffs.bin
   ```

The firmware automatically reloads the overrides on the next boot. Delete the NVS keys (using `nvs_util.py`) or wipe the SPIFFS
partition to revert to the compiled certificates.

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
