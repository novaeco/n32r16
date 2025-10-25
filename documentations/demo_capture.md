# Demo Capture Workflow

This guide explains how to record demonstrations of the HMI node and WebSocket telemetry.

## LVGL Framebuffer Snapshots

1. Enable LVGL snapshots:
   ```bash
   idf.py menuconfig
   # Component config → LVGL configuration → Enable lvgl snapshot support
   ```
2. Build and flash the HMI node.
3. Trigger a snapshot when the UI is in the desired state:
   ```bash
   idf.py -C hmi_node monitor
   # Press Ctrl+] to enter the monitor console
   esp32> lvgl_snapshot /spiffs/ui_frame.bin
   ```
4. Convert the raw frame into a PNG using the provided helper:
   ```bash
   python tools/lvgl_snapshot.py --input ui_frame.bin --width 1024 --height 600 --output ui_frame.png
   ```

## Video Capture

1. Connect the HMI node via USB and start `idf.py monitor` in a terminal to mirror logs.
2. Launch OBS Studio or FFmpeg:
   ```bash
   ffmpeg -f kmsgrab -i - -vf "hwdownload,format=bgr0" -c:v libx264 -preset slow -crf 18 demo.mp4
   ```
3. Overlay log output using the `tools/ws_trace.py` helper to visualise WebSocket exchanges:
   ```bash
   python tools/ws_trace.py --uri wss://sensor-node.local:8080/ws --token $CONFIG_HMI_WS_AUTH_TOKEN --export trace.json
   ```
4. Combine the screen recording with the trace overlay in post-production (e.g., DaVinci Resolve) or use OBS scenes.

## Checklist Before Publishing

- Regenerate handshake nonces by power-cycling both nodes to avoid replay warnings in logs.
- Mask bearer tokens and SRP credentials when sharing footage.
- Include a snapshot of the wiring harness referencing `documentations/hardware_wiring.md` for context.
