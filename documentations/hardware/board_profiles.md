# Board Profiles and Wiring Guidance

This document consolidates hardware-specific notes for the dual-node platform and enumerates the configuration overlays that
ship with the repository. All firmware is built on ESP-IDF v5.5.1.

## Overview

| Node          | Recommended module                            | Display            | Default overlay                  |
|---------------|------------------------------------------------|--------------------|----------------------------------|
| Sensor node   | ESP32-S3-WROOM-2-N32R16V (16 MB PSRAM / 32 MB) | —                  | `sdkconfig.defaults`             |
| Sensor (alt)  | ESP32-S2-WROVER (8 MB flash / 2 MB PSRAM)      | —                  | `../configs/esp32s2/sdkconfig.defaults` |
| Sensor (alt)  | ESP32-C3-MINI (4 MB flash)                     | —                  | `../configs/esp32c3/sdkconfig.defaults` |
| HMI node      | Waveshare ESP32-S3 Touch LCD 7B (1024×600)     | 7" RGB w/ GT911    | `sdkconfig.defaults`             |

The S2 and C3 overlays lower flash expectations while retaining dual OTA slots and SPIFFS storage for certificate overrides. OTA
and LVGL assets remain unchanged.

## Partition Layouts

The `partitions/` directory now contains profile-specific CSVs that can be selected via `CONFIG_PARTITION_TABLE_FILENAME` when
combining `SDKCONFIG_DEFAULTS`:

- `default_16MB_psram_32MB_flash_opi.csv` – primary layout for ESP32-S3 with 16 MB PSRAM and 32 MB OPI flash.
- `esp32s2_8MB.csv` – tuned for 8 MB QIO flash. OTA slots are reduced to ~1.94 MiB each while maintaining a 4 MiB SPIFFS region.
- `esp32c3_4MB.csv` – sized for 4 MB devices; OTA slots are 1.25 MiB and a 1.47 MiB SPIFFS partition provides certificate storage.

## Wiring Summary

| Signal                       | Sensor node (S3/S2/C3) | Notes                                                                 |
|------------------------------|------------------------|-----------------------------------------------------------------------|
| I²C SDA/SCL                  | GPIO3 / GPIO2          | Pull-ups 4.7 kΩ. S2 overlays remap SDA to GPIO8 to avoid strapping pins. |
| DS18B20 1-Wire               | GPIO8 (configurable)   | Requires 4.7 kΩ pull-up.                                               |
| PWM expander (PCA9685/TLC5947) | I²C @ 0x40 / SPI CS GPIO9 | Disable in config for ESP32-C3 to save RAM when actuators unused.       |
| GT911 touch (HMI)            | GPIO16 / GPIO17        | Always on HMI node; no change across profiles.                         |

The C3 profile is uni-core and omits PSRAM, so driver task stacks are reduced automatically when the overlay is applied.

## Security Feature Matrix

| Feature                         | Sensor toggle                                  | HMI toggle                                   | Secret source                                  |
|---------------------------------|------------------------------------------------|-----------------------------------------------|------------------------------------------------|
| Bearer token (default)          | `CONFIG_SENSOR_WS_AUTH_TOKEN`                  | `CONFIG_HMI_WS_AUTH_TOKEN`                    | `sdkconfig.defaults`                            |
| HMAC handshake                  | `CONFIG_SENSOR_WS_ENABLE_HANDSHAKE`            | `CONFIG_HMI_WS_ENABLE_HANDSHAKE`              | `*_WS_CRYPTO_SECRET_BASE64` (Base64 32-byte key) |
| AES-GCM payload encryption      | `CONFIG_SENSOR_WS_ENABLE_ENCRYPTION`           | `CONFIG_HMI_WS_ENABLE_ENCRYPTION`             | Same as above                                  |
| Handshake replay window         | `CONFIG_SENSOR_WS_HANDSHAKE_TTL_MS`            | N/A (client regenerates every reconnect)      | —                                              |
| Handshake cache size            | `CONFIG_SENSOR_WS_HANDSHAKE_CACHE_SIZE`        | N/A                                           | —                                              |

> **Key rotation:** Update both `*_WS_CRYPTO_SECRET_BASE64` options with a new 32-byte random value encoded as Base64. Rebuild and
> flash both nodes before flipping the handshake/encryption toggles to avoid asymmetric secrets.

## Build Matrix Examples

```bash
# ESP32-S3 baseline
idf.py set-target esp32s3 build

# ESP32-S2 sensor variant
idf.py set-target esp32s2 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;../configs/esp32s2/sdkconfig.defaults" build

# ESP32-C3 sensor variant
idf.py set-target esp32c3 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;../configs/esp32c3/sdkconfig.defaults" build
```

## OTA & Certificate Storage

All overlays keep a SPIFFS partition named `storage` for runtime certificate overrides. The helper scripts under `tools/` accept
the partition filename at runtime, so no changes are required when swapping profiles.

## Troubleshooting Notes

- **Handshake failures** – Ensure both nodes decode identical secrets and that the sensor cache size is large enough for the
  expected client concurrency. Check the log for `Nonce replay detected` messages.
- **Out-of-memory on ESP32-C3** – Disable unused drivers via Kconfig (`CONFIG_SENSOR_PWM_BACKEND_DRIVER_NONE`) and keep LVGL assets
  offloaded to the HMI node.
- **SPIRAM timing on ESP32-S2** – Modules without PSRAM should set `CONFIG_SPIRAM_SUPPORT=n` in an additional overlay or via
  `menuconfig` to prevent boot-time probe delays.
