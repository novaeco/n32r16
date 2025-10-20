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

Each project consumes the shared partition table `partitions/default_16MB_psram_32MB_flash_opi.csv`, enabling 32 MB octal PSRAM and OPI flash at 80 MHz. Default SDK configurations enable PSRAM heap usage, disable Wi-Fi power-save, and turn on LVGL logging at WARN level. Wi-Fi credentials are configurable via `menuconfig` or by editing `sdkconfig.defaults`.

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

## Runtime Architecture

### Sensor Node

* **Tasks**
  * `t_sensors` — Manages SHT20 and DS18B20 acquisition (non-blocking 1-Wire sequence), applies exponential filtering, updates the shared data model, and pushes WebSocket updates every 200 ms (on change).
  * `t_io` — Initializes PCA9685/MCP23017, applies queued I/O commands (PWM duty & GPIO writes), periodically snapshots states into the data model.
  * `t_heartbeat` — Toggles the status LED once per second.
* **Networking**
  * Wi-Fi STA with automatic reconnect and modem power-save when configured.
  * mDNS advertiser `_hmi-sensor._tcp` on port **8443**.
  * TLS-enabled WebSocket server (`wss://<host>:8443/ws`) accepting multiple HMI clients, replaying the latest snapshot on join and validating send return codes.
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
  * Wi-Fi STA to the same AP as the sensor node with live status propagated to the data model.
  * mDNS query for `_hmi-sensor._tcp` resolves the TLS WebSocket endpoint.
  * Hardened WebSocket client with exponential backoff, TLS certificate pinning (shared self-signed bundle), and connection state reflected into the UI model.
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

1. Update `sdkconfig.defaults` (or `menuconfig`) with production Wi-Fi credentials.
2. Validate hardware connections against the tables above (check pull-ups for I²C/1-Wire).
3. Build and flash `sensor_node`, confirm mDNS announcement via `mdns_query` or monitor logs.
4. Build and flash `hmi_node`, verify automatic discovery and dashboard updates within 5 seconds.
5. Exercise GPIO toggles and PWM sliders to confirm bidirectional WebSocket command flow.
6. Observe heartbeat LEDs and monitor logs for CRC errors or reconnect attempts.

## Licensing

All source code in this repository is released under the **Apache License 2.0**. See the license headers embedded in each file (implicit by repository policy) or add the standard header if new files are introduced.

