# WiFi-Sense - Person Detection & Localization

A WiFi-based passive radar system using **ESP32-S3** that detects human presence, motion, and position in a room using **RSSI** and **CSI (Channel State Information)** - no additional sensors required.


## How It Works

When a person moves through a room, they disturb the WiFi signal paths between the ESP32 and the router. This project measures two types of signal disturbance:

- **RSSI** (Received Signal Strength) - a single aggregate signal level value
- **CSI** (Channel State Information) - per-subcarrier amplitude/phase data (52+ data points per measurement), far more sensitive than RSSI alone

A digital signal processing pipeline extracts motion information:

```
Raw Signal -> Moving Average Filter -> Variance Calculation -> Integrator -> Detection Level
```


## Hardware

- **MCU**: ESP32-S3-WROOM-1
- **WiFi Router**: Any 2.4GHz WiFi access point
- **No additional sensors or hardware required**

## Software Requirements

- **Arduino IDE** 2.x with **ESP32 Arduino Core v3.x**
- Board: `ESP32S3 Dev Module`

**Required Libraries** (install via Arduino Library Manager):
- `ESPAsyncWebServer` by me-no-dev
- `AsyncTCP` by me-no-dev
- `ArduinoJson` by Benoit Blanchon

**Built-in** (no install needed):
- `LittleFS`
- `WiFi`
- `esp_wifi` (CSI API)

## Quick Start

### Standalone (1 ESP32-S3)

1. Open `wifi-sense/wifi_sense.ino` in Arduino IDE
2. Set `WIFI_SSID` and `WIFI_PASSWORD` to your router credentials
3. Select board: `ESP32S3 Dev Module`
4. Upload sketch
5. Upload `wifi-sense/data/` folder to LittleFS:
   - Install the [ESP32 LittleFS Upload Plugin](https://github.com/lorol/arduino-esp32littlefs-plugin)
   - Arduino IDE: **Tools > ESP32 Sketch Data Upload**
6. Open Serial Monitor (115200 baud) to see the device IP
7. Open browser to that IP address

Place the ESP32-S3 on one side of the room and the router on the other side.

## Web Interface

- Room view with presence/motion indicator, live RSSI/CSI metrics, motion history chart

Support:
- Adjustable detection thresholds via sliders
- Configurable room dimensions
- Real-time WebSocket updates (~10 Hz)

## Project Structure

```
wifi-sense/           # Single-node presence detector
  wifi_sense.ino      # Main sketch
  signal_processor.h/cpp # DSP: moving average, variance, detection
  csi_collector.h/cpp    # ESP32-S3 CSI data collection
  web_server.h/cpp       # HTTP server + WebSocket
  data/                  # Web GUI (upload to LittleFS)
    index.html
    style.css
    app.js
```

## Tuning

**Detection Sensitivity**: Adjust via web UI or in code:
- `motionThreshold` (default 15) - higher = less sensitive to motion
- `presenceThreshold` (default 5) - higher = less sensitive to presence
- `scanIntervalMs` (default 100) - sampling rate in milliseconds

**Signal Processor Config** (in `setup()`):
```cpp
processor.init(
    64,    // sample buffer size (more = smoother, slower response)
    16,    // moving average filter size
    15,    // variance threshold
    3      // variance integrator limit (samples to accumulate)
);
```

**For best results**:
- Place ESP32 and router with clear line-of-sight across the room
- Avoid placing nodes near metal objects or other 2.4GHz interference
- Allow 30-60 seconds for the signal processor to calibrate on startup
- Start with low thresholds and increase until false positives stop

## CSI vs RSSI

| Aspect | RSSI | CSI |
|--------|------|-----|
| Data points per reading | 1 | 52+ subcarriers |
| Sensitivity | Low | High (sub-meter) |
| Breathing detection | No | Possible |
| Computational cost | Minimal | Moderate |
| Works through walls | Limited | Yes |

This project uses both simultaneously. CSI is the primary detection source when available, with RSSI as fallback.

## License

MIT License. Based on concepts from the [MotionDetector](https://github.com/happytm/MotionDetector) project.
