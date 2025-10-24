# n32r16 Dual-Board Sensor & HMI Platform

This repository delivers production-ready firmware for a two-board ESP32-S3 system built with ESP-IDF v5.5. The **sensor node** aggregates environmental data and exposes a WebSocket server, while the **HMI node** renders a LVGL dashboard on the Waveshare ESP32-S3 Touch LCD 7B and sends actuation commands back to the sensor. Communication runs over Wi-Fi STA mode with mDNS-based discovery (`_hmi-sensor._tcp`).

## Repository Layout

```
/
├── AGENTS.md                    # Contribution guidelines for agents
├── README.md                    # Project documentation (this file)
├── partitions/
│   └── default_16MB_psram_32MB_flash_opi.csv
├── common/
│   ├── proto/                   # JSON/CBOR schema helpers & CRC32
│   ├── net/                     # Wi-Fi STA, mDNS, WS client/server
│   └── util/                    # Time sync, ring buffer, monotonic clock
├── components/
│   ├── cjson/                   # cJSON helper wrapper
│   ├── esp_lcd_panel_rgb/       # Thin RGB panel creator for esp_lcd
│   └── lvgl/                    # LVGL v9 component manager metadata
├── sensor_node/                 # Sensor firmware (ESP32-S3-WROOM-2-N32R16V)
│   ├── main/                    # Application sources
│   ├── sdkconfig.defaults       # Target defaults (32 MB OPI flash + 16 MB PSRAM)
│   └── CMakeLists.txt
├── hmi_node/                    # HMI firmware (Waveshare ESP32-S3 Touch LCD 7B)
│   ├── main/
│   ├── sdkconfig.defaults
│   └── CMakeLists.txt
├── components/
├── tests/                       # Unity-based host tests (`idf.py -T`)
└── .github/workflows/ci.yml     # GitHub Actions building both targets + tests
```

## Toolchain Requirements

* ESP-IDF **v5.5** (or newer) with the ESP32-S3 toolchain (`idf.py --version` should report ≥ 5.5)
* CMake + Ninja (installed automatically with ESP-IDF tools)
* Python 3.8+

## Building & Flashing

Both applications are standalone ESP-IDF projects. Always set the target first.

### Sensor Node (ESP32-S3-WROOM-2-N32R16V)

```bash
cd sensor_node
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### HMI Node (Waveshare ESP32-S3 Touch LCD 7B)

```bash
cd hmi_node
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Each project consumes the shared partition table `partitions/default_16MB_psram_32MB_flash_opi.csv`, enabling 32 MB octal PSRAM and OPI flash at 80 MHz. Default SDK configurations enable PSRAM heap usage, disable Wi-Fi power-save, and turn on LVGL logging at WARN level. Wi-Fi onboarding is performed at runtime through the integrated provisioning portal (SoftAP + HTTP/HTTPS) rather than compile-time credentials.

## Hardware Overview & Wiring

### Sensor Node Peripherals

| Peripheral | Interface | Address / Pin | Notes |
|------------|-----------|----------------|-------|
| SHT20 x2   | I²C @ 400 kHz | 0x40 (fixed) | Temperature & RH (EMA filtered) |
| PCA9685    | I²C @ 400 kHz | 0x41 | 16× PWM (12-bit, default 500 Hz) |
| MCP23017 x2| I²C @ 400 kHz | 0x20 / 0x21 | 16× GPIO each (polling) |
| DS18B20 x4 | 1-Wire        | `ONEWIRE_GPIO` + 4.7 kΩ pull-up | Multi-drop ROM search |
| Heartbeat LED | GPIO       | GPIO2 | 1 Hz toggle |

Default I²C pins: `GPIO8` (SDA) / `GPIO9` (SCL) with 4.7 kΩ pull-ups. The 1-Wire bus uses `GPIO1` with a 4.7 kΩ pull-up.

### HMI Panel & Touch (Waveshare 7B)

* RGB 16-bit data lines mapped per `board_waveshare7b_pins.h`
* Control pins: `DE=GPIO5`, `HSYNC=GPIO46`, `VSYNC=GPIO3`, `PCLK=GPIO7`
* GT911 touch controller on I²C (`GPIO8` SDA, `GPIO9` SCL, `GPIO4` INT, `GPIO2` RST)
* LVGL double-buffer in PSRAM (line buffers = 40 rows)

Ensure the display backlight, power rails, and I²C pull-ups follow Waveshare recommendations.

## Provisioning, OTA & Secrets

### Dual-OTA Partitioning

The shared partition table now exposes a `factory` slot plus two OTA slots (`ota_0`, `ota_1`) governed by `otadata`, enabling fail-safe firmware upgrades and rollbacks. The SPIFFS `storage` partition retains calibration assets, while `certstore` (type `0x40`, encrypted) is reserved for the TLS server certificate/private key bundle consumed through the ESP Secure Cert API. The `secrets` partition (type `nvs_keys`) stores encrypted NVS keys and command-authentication material.

Flash the custom table automatically through `idf.py flash`; OTA updates can subsequently be delivered with `esp_ota_ops` or `idf.py ota` once signed images are hosted.

### Wi-Fi Provisioning Workflow

1. On first boot (or after credential erase), both nodes broadcast a SoftAP named `<PREFIX><MAC>`, where the prefix defaults to `SENSOR` (sensor) or `HMI` (HMI). The WPA2 key defaults to `SensorSetup!` / `HMISetup!` and should be changed in production via `menuconfig`.
2. Connect to the SoftAP and browse to `http://192.168.4.1` to enter the production SSID/password and, optionally, regenerate the setup key/PoP.
3. After successful association the provisioning manager stops automatically and credentials persist in encrypted NVS; subsequent boots skip SoftAP bring-up unless credentials are cleared (`idf.py erase_flash`).

### TLS Material

* Generate an X.509 certificate + private key and inject them into the `certstore` partition using `espsecure-cert-tool.py` or the secure manufacturing flow. Example:

  ```bash
  espsecure-cert-tool.py burn --port /dev/ttyUSB0 --keyfile sensor_ws_server.key --certfile sensor_ws_server.crt
  ```

* The firmware loads keys at runtime via `esp_secure_cert_read` and refuses to start the WebSocket service if the bundle is missing.

### Command Authentication Key

* The sensor node enforces HMAC-SHA256 signatures (`HS256`) on every command (`cmd` envelopes). Provision a 32-byte secret into the `secrets` partition namespace `command` / key `hmac_key` using the ESP-IDF NVS partition tool:

  ```bash
  python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate \
      command_keys.csv sensor_secrets.bin 4096
  esptool.py --chip esp32s3 --port /dev/ttyUSB0 write_flash 0x???????? sensor_secrets.bin
  ```

  Where `command_keys.csv` contains:

  ```csv
  key,type,encoding,value
  hmac_key,data,binary,00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff
  ```

* The HMI (or any authorized client) must share the same key to generate valid HMACs via the exposed `command_auth_generate_mac()` helper.

### Resetting Secrets

To revoke credentials, erase only the affected partitions:

```bash
esptool.py --port /dev/ttyUSB0 erase_region 0x9000 0x7000       # NVS Wi-Fi credentials
esptool.py --port /dev/ttyUSB0 erase_region 0x920000 0x4000     # TLS cert/private key bundle
esptool.py --port /dev/ttyUSB0 erase_region 0x924000 0x3000     # Command-auth HMAC key partition
```

Follow with secure re-provisioning as described above.

## Runtime Architecture

### Sensor Node

* **Tasks**
  * `t_sensors` — Manages SHT20 and DS18B20 acquisition (non-blocking 1-Wire sequence), applies exponential filtering, updates the shared data model, and pushes WebSocket updates every 200 ms (on change).
  * `t_io` — Initializes PCA9685/MCP23017, applies queued I/O commands (PWM duty & GPIO writes), periodically snapshots states into the data model.
  * `t_heartbeat` — Toggles the status LED once per second.
* **Networking**
  * Wi-Fi provisioning manager (SoftAP + PoP) persists credentials to encrypted NVS and seamlessly transitions into STA mode with autoreconnect + modem power-save.
  * mDNS advertiser `_hmi-sensor._tcp` on port **8443**.
  * TLS-enabled WebSocket server (`wss://<host>:8443/ws`) sourcing its X.509 identity from the secure cert partition; latest snapshot replay on join and full send error telemetry.
  * Application-layer command HMAC (HS256) validation prior to queueing I/O jobs, replay-window enforcement, and nonce tracking.
  * Periodic SNTP synchronization updates snapshot timestamps when UTC is available.
* **Protocol**
  * JSON payload schema v1 with optional CBOR (compile-time switch `CONFIG_USE_CBOR`).
  * CRC32 appended at envelope level (`proto_crc32` component).
  * Command handling (`set_pwm`, `write_gpio`) validated and queued for `t_io`.

### HMI Node

* **Tasks**
  * `t_ui` — Owns LVGL context, processes touch input (GT911), renders dashboard, updates widgets when notified.
  * `t_net_rx` — Starts the WebSocket client after mDNS discovery, maintains connection health.
  * `t_hmi_heartbeat` — Toggles diagnostic GPIO.
* **UI**
  * LVGL v9 tabbed dashboard with: sensor tiles, live chart (128-point history), GPIO status page, PWM sliders, and settings overview.
  * Dark theme styling with 1024×600 layout tuned to 160 DPI.
* **Networking**
  * Shared SoftAP provisioning flow identical to the sensor node; credentials stored in encrypted NVS, with Wi-Fi health mirrored into the UI model.
  * mDNS query for `_hmi-sensor._tcp` resolves the TLS WebSocket endpoint.
  * Hardened WebSocket client with exponential backoff and certificate verification against the trusted CA bundle; connection state reflected into the UI model.
  * Incoming payloads decoded into `hmi_data_model`, UI notified via semaphore.

## Testing

Run Unity-based component tests via ESP-IDF’s `-T` option from either project directory:

```bash
idf.py -T tests
```

The suite currently covers both protocol encode/decode logic and the shared ring-buffer utility used by network backpressure paths.

## Continuous Integration

`.github/workflows/ci.yml` builds both sensor and HMI projects on every push/pull request and executes unit tests. Ccache is configured to accelerate successive builds.

## Deployment Checklist

1. Inject the production TLS certificate/private key bundle into the `certstore` partition and burn the 32-byte command-auth HMAC key into the `secrets` partition.
2. Validate hardware connections against the tables above (check pull-ups for I²C/1-Wire).
3. Build and flash `sensor_node`, complete Wi-Fi provisioning via the SoftAP portal, and confirm mDNS advertisement with `mdns_query` or log output.
4. Build and flash `hmi_node`, provision it onto the same infrastructure Wi-Fi and verify automatic discovery plus dashboard population within 5 seconds.
5. Exercise GPIO toggles and PWM sliders to confirm bidirectional, authenticated WebSocket command flow.
6. Observe heartbeat LEDs and monitor logs for CRC/HMAC failures or reconnect attempts.

## Licensing

All source code in this repository is released under the **Apache License 2.0**. See the license headers embedded in each file (implicit by repository policy) or add the standard header if new files are introduced.

