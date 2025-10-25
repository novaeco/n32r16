# WebSocket TOTP Hardening

This document details the optional time-based one-time password (TOTP) guard for
WebSocket links between the sensor and HMI nodes. The mechanism supplements the
existing bearer token and HMAC handshake with an RFC 6238 compliant time
second-factor. Both firmwares must share the same secret and maintain a
reasonable time synchronisation (SNTP is enabled by default).

## Enabling TOTP

1. Generate a random Base32 secret (20–32 bytes recommended):

   ```bash
   python - <<'PY'
   import secrets, base64
   raw = secrets.token_bytes(20)
   print(base64.b32encode(raw).decode())
   PY
   ```

2. Populate `sdkconfig.defaults` (or project-specific overlays) with the
   resulting value:

   ```ini
   CONFIG_SENSOR_WS_ENABLE_TOTP=y
   CONFIG_SENSOR_WS_TOTP_SECRET_BASE32="JBSWY3DPEHPK3PXP..."
   CONFIG_HMI_WS_ENABLE_TOTP=y
   CONFIG_HMI_WS_TOTP_SECRET_BASE32="JBSWY3DPEHPK3PXP..."
   ```

3. Optionally adjust the period (`CONFIG_*_TOTP_PERIOD_S`), number of digits
   (`CONFIG_*_TOTP_DIGITS`), and allowed drift window
   (`CONFIG_*_TOTP_WINDOW`). The defaults mirror typical authenticator apps
   (30-second step, 8-digit codes, ±1 step window).

4. Rebuild both firmwares. The client automatically injects an `X-WS-TOTP`
   header on every connection attempt, and the server rejects handshakes when
   the header is missing, malformed, or outside the permitted time window.

## Monitoring and troubleshooting

- The server logs `TOTP verification failed: code mismatch` when the supplied
  code is outside the configured drift window.
- Ensure both ESP32 nodes obtain an SNTP fix during boot. The shared
  `common/util/time_sync` helper configures fallback NTP servers and holds the
  network stack until synchronisation succeeds.
- For staging environments without stable timekeeping, disable the feature or
  widen `CONFIG_*_TOTP_WINDOW` temporarily. Never ship production images with a
  drift window greater than ±1.

## Test coverage

The `common/net/tests/test_ws_security.c` suite exercises TOTP code generation
against the official RFC 6238 vectors, while
`common/net/tests/test_ws_client.c` validates that reconnects regenerate fresh
headers. Server-side configuration validation is asserted in
`common/net/tests/test_ws_server.c`.

