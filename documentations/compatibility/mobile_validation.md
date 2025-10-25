# Mobile & Cross-Browser Validation Workflow

This procedure exercises the WebSocket control plane, LVGL-rendered dashboard, and OTA telemetry paths using mobile clients. It
assumes the repository has been cloned on a workstation with ESP-IDF v5.5 and Python ≥ 3.11.

## 1. Prepare the mock gateway

1. Create a virtual environment for the test harness:
   ```bash
   python -m venv .venv-mobile
   source .venv-mobile/bin/activate
   python -m pip install --upgrade pip
   pip install -r tests/e2e/requirements.txt
   ```
2. (Optional, recommended) Generate a local certificate authority and server certificate to avoid browser warnings:
   ```bash
   openssl req -x509 -nodes -newkey rsa:2048 -keyout mobile-gateway.key \
     -out mobile-gateway.crt -days 30 -subj "/CN=n32r16-mobile-gateway"
   ```
   Install the certificate on the mobile devices (Settings → General → About → Certificate Trust Settings on iOS, or
   Settings → Security → Credential storage on Android).
3. Launch the asyncio mock gateway that emulates the firmware WebSocket endpoint:
   ```bash
   python -m tests.e2e.mock_gateway --host 0.0.0.0 --port 8443 \
     --token demo-token --cert mobile-gateway.crt --key mobile-gateway.key
   ```
   Add `--insecure` if TLS offloading is handled by an upstream proxy or if development devices accept `ws://`.

## 2. Validate connectivity from mobile devices

1. Connect an iOS 17 device (Safari) and an Android 14 device (Chrome) to the same Wi-Fi network as the workstation.
2. Deploy the firmware build with `CONFIG_SENSOR_WS_AUTH_TOKEN="demo-token"` and point the HMI node to the gateway hostname via
   the Settings → Network tab (or override `CONFIG_HMI_DISCOVERY_STATIC_HOST`).
3. Observe the dashboard as the telemetry stream produced by the gateway animates the cards, charts, and GPIO controls. Ensure no
   overlap occurs in portrait orientation (minimum 360 px width) and that accessibility toggles take effect immediately.
4. Rotate each device to landscape mode and verify the tab bar and charts scale to 1024 px wide viewports without clipping.

## 3. Exercise WebSocket controls from mobile browsers

1. Enable Web Inspector (iOS) or Chrome DevTools (Android via USB debugging).
2. Issue a GPIO command by toggling a switch; the WebSocket frame should appear in the network inspector with CRC-matched
   acknowledgements from the mock gateway (`MockGateway` echoes the command payload).
3. Trigger a PWM frequency update and confirm the gateway logs show the decoded command structure (`{"pwm_freq": {"freq": …}}`).
4. Use the console to evaluate:
   ```js
   window.n32r16.ws.ping();
   ```
   A binary `pong` frame should return within 1 s, demonstrating keep-alive behaviour on mobile transports.

## 4. Simulate lossy LTE conditions

1. In Chrome DevTools, enable **Network → Throttling → Custom** with `Downlink: 1.5 Mbps`, `Uplink: 750 Kbps`, and `RTT: 120 ms`.
   On Safari, use the **Network Link Conditioner** profile (Medium 3G).
2. Repeat the GPIO and PWM tests; frames must remain under 256 bytes post-encryption, guaranteeing delivery under constrained MTU.
3. Monitor FPS on the chart tab; the LVGL canvas should maintain 30 fps thanks to adaptive draw buffers sized by
   `memory_profile_recommend_draw_buffer_px()`.

## 5. Regression automation hooks

- Execute `pytest tests/e2e/test_ws_performance.py::test_handshake_hmac_under_load` before and after mobile sessions to ensure no
  drift in handshake primitives.
- Run the dedicated smoke test:
  ```bash
  python tools/ws_diagnostic.py <gateway-host> --port 8443 --token demo-token --expect 3 --timeout 3
  ```
  The script mirrors the mobile browser flow and validates CRC/HMAC behaviour independently of the UI.

## 6. Reporting

Capture screenshots and console traces, then archive them under `documentations/media/mobile/` with filenames including the device
model and OS version (e.g. `iphone15pro-ios17-dashboard.png`). Reference these assets in `README.md` when updating the UI gallery.
