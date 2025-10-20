# n32r16 Dual-Board Sensor & HMI Platform

This repository delivers production-ready firmware for a two-board ESP32-S3 system built with ESP-IDF v5.5. The **sensor node**
aggregates environmental data and exposes a WebSocket server, while the **HMI node** renders a LVGL dashboard on the Waveshare E
SP32-S3 Touch LCD 7B and sends actuation commands back to the sensor. Communication runs over Wi-Fi STA mode with mDNS-based dis
covery (`_hmi-sensor._tcp`). Messages use JSON envelopes with CRC32 integrity protection and optional CBOR framing (compile-time
switch `CONFIG_USE_CBOR`). Each envelope carries a monotonically increasing sequence number, allowing command acknowledgements an
d duplicate suppression.

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

Each project consumes the shared partition table `partitions/default_16MB_psram_32MB_flash_opi.csv`, enabling 32 MB octal PSRAM
and OPI flash at 80 MHz. Default SDK configurations enable PSRAM heap usage, disable Wi-Fi power-save, turn on LVGL logging at W
ARN level, and enable FreeRTOS runtime statistics for CPU load telemetry. Wi-Fi credentials are configurable via `menuconfig` or 
by editing `sdkconfig.defaults`.

## Hardware Overview & Wiring

### Sensor Node Peripherals

| Peripheral | Interface | Address / Pin | Notes |
|------------|-----------|----------------|-------|
| SHT20 x2   | I²C @ 400 kHz | 0x40 (fixed) | Temperature & RH (EMA filtered, retry with backoff) |
| PCA9685    | I²C @ 400 kHz | 0x41 | 16× PWM (12-bit, default 500 Hz, runtime frequency control) |
| MCP23017 x2| I²C @ 400 kHz | 0x20 / 0x21 | 16× GPIO each (polling, command queue) |
| DS18B20 x4 | 1-Wire        | `ONEWIRE_GPIO` + 4.7 kΩ pull-up | Multi-drop ROM search, non-blocking conversion |
| Heartbeat LED | GPIO       | GPIO2 | 1 Hz toggle + metric transmission |

Default I²C pins: `GPIO8` (SDA) / `GPIO9` (SCL) with 4.7 kΩ pull-ups. The 1-Wire bus uses `GPIO1` with a 4.7 kΩ pull-up.

### HMI Panel & Touch (Waveshare 7B)

* RGB 16-bit data lines mapped per `board_waveshare7b_pins.h`
* Control pins: `DE=GPIO5`, `HSYNC=GPIO46`, `VSYNC=GPIO3`, `PCLK=GPIO7`
* GT911 touch controller on I²C (`GPIO8` SDA, `GPIO9` SCL, `GPIO4` INT, `GPIO2` RST)
* LVGL partial refresh with double buffers allocated in PSRAM (40-line chunks)

Ensure the display backlight, power rails, and I²C pull-ups follow Waveshare recommendations.

## Runtime Architecture

### Sensor Node

* **Tasks**
  * `t_sensors` — Manages SHT20 and DS18B20 acquisition using an exponential moving average filter and a conversion state machi
ne for the multi-drop 1-Wire bus. Readings are retried with backoff on transient I²C errors. Updates trigger differential publis
hes every 200 ms (forced at ≥5 s).
  * `t_io` — Initializes PCA9685/MCP23017, applies queued I/O commands (PWM duty, PWM frequency, GPIO writes), snapshots state, a
nd emits command acknowledgements.
  * `t_heartbeat` — Samples CPU utilisation (via FreeRTOS runtime stats), Wi-Fi RSSI, bus error counters, and uptime; publishes a
 heartbeat packet over WebSocket once per second while driving the heartbeat LED.
* **Networking & Protocol**
  * Wi-Fi STA with automatic reconnect.
  * mDNS advertiser `_hmi-sensor._tcp` on port 8080.
  * WebSocket server (`/ws`) streaming JSON or CBOR envelopes. Each message embeds `v`, `type`, `ts`, `seq`, and a `crc` over the
 payload, enabling integrity checks on the HMI.
  * Command handling (`set_pwm`, `set_pwm_freq`, `write_gpio`) validates CRC and responds with a `cmd_ack` envelope.

### HMI Node

* **Tasks**
  * `t_ui` — Owns LVGL context, processes touch input (GT911), renders the multi-tab UI, and reacts to incoming data/ack events.
  * `t_net_rx` — Resolves the sensor via mDNS and establishes the WebSocket client with automatic retry.
  * `t_hb` — Toggles a diagnostic LED and emits a heartbeat packet (CPU load, uptime) toward the sensor node.
* **UI**
  * Dashboard tiles for each SHT20/DS18B20 plus live trend chart (128-point history) and status badges (link state, CRC, RSSI, C
PU load).
  * GPIO tab exposing 16 switches per MCP23017 device; state changes send `write_gpio` commands with bit masks.
  * PWM tab with global frequency slider (50–1600 Hz) and 16 duty sliders (0–4095) that dispatch `set_pwm_freq`/`set_pwm` envelop
es.
  * Settings tab summarises Wi-Fi/mDNS configuration.
* **Networking**
  * Wi-Fi STA joins the same AP as the sensor node.
  * mDNS query for `_hmi-sensor._tcp` resolves the WebSocket endpoint; binary/text frames are auto-detected.
  * Incoming envelopes update the local data snapshot, heartbeat metrics, and command acknowledgement indicators.

## Reliability & Telemetry

* Sensor-side buses track error counts (I²C recovery pulses and 1-Wire faults) and surface them in heartbeat payloads.
* Command responses include `cmd_ack` envelopes with success/failure reasons; the HMI displays the latest acknowledgement status.
* Heartbeat packets flow both directions at 1 Hz to monitor link health and feed UI indicators.

## Testing

Run Unity-based component tests via ESP-IDF’s `-T` option from either project directory:

```bash
idf.py -T tests
```

The suite validates the JSON/CBOR envelope encode/decode path, sequence numbering, and CRC verification logic.

## Continuous Integration

`.github/workflows/ci.yml` builds both sensor and HMI projects on every push/pull request and executes unit tests. Ccache is con
figured to accelerate successive builds.

## Deployment Checklist

1. Update `sdkconfig.defaults` (or `menuconfig`) with production Wi-Fi credentials.
2. Validate hardware connections against the tables above (check pull-ups for I²C/1-Wire).
3. Build and flash `sensor_node`, confirm mDNS announcement via `mdns_query` or monitor logs.
4. Build and flash `hmi_node`, verify automatic discovery and dashboard updates within 5 seconds.
5. Exercise GPIO toggles and PWM sliders to confirm bidirectional WebSocket command flow and acknowledgements.
6. Observe heartbeat LEDs and monitor logs for CRC errors, reconnect attempts, or bus-error counters.

## Licensing

All source code in this repository is released under the **Apache License 2.0**. See the license headers embedded in each file (
implicit by repository policy) or add the standard header if new files are introduced.

