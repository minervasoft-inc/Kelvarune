# Kelvarune SDK

[日本語版 README はこちら](README.ja.md)

Arduino library for ESP32 that lets your device be monitored, debugged, and controlled from the **[Kelvarune](https://minervasoft.co.jp/kelvarune)** mobile app (iOS / Android) over Bluetooth Low Energy or Wi-Fi.

Add a few lines to your sketch and get, in the app, without writing any UI code:

- A live **Dashboard** (Number / Gauge / Bar / LED / Line Graph tiles) for values you report from your firmware
- A **Console** for debug/info/warn/error logs and free-form events
- **Commands** (buttons, numeric/text inputs, toggles, enums) that trigger code in your sketch and report back success/failure
- Automatic **System Metrics** (uptime, CPU clock, heap usage, Wi-Fi RSSI, etc.)

## Requirements

- ESP32 (Arduino core for ESP32)
- Arduino IDE 2.x or `arduino-cli`
- Dependencies (installed automatically if you use the Arduino Library Manager, otherwise install manually):
  - [ArduinoJson](https://arduinojson.org/) >= 7.0.0
  - [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) >= 2.0.0 (for BLE transport)
  - [WebSockets](https://github.com/Links2004/arduinoWebSockets) >= 2.7.0 (for Wi-Fi transport)

You only need the transport-specific dependency for the transport you actually use (`begin()` for BLE, or `beginWiFi()` for Wi-Fi) — see below.

## Installation

### Arduino Library Manager

Sketch → Include Library → Manage Libraries… → search "Kelvarune" → Install.

### Manual

1. Download this repository as a ZIP (Code → Download ZIP), or clone it.
2. Arduino IDE: Sketch → Include Library → Add .ZIP Library…, or copy the folder into your `libraries/` directory.
3. Install the dependencies listed above via the Library Manager.

## Quick start

```cpp
#include <Kelvarune.h>

void setup() {
  Kelvarune.begin("My Device");           // Start advertising over BLE
  Kelvarune.setFirmwareVersion("1.0.0");

  // A Button command. Tapping it in the app calls this handler.
  Kelvarune.command("reset", "Reset", KV_CMD_BUTTON, [](KVCommandRequest req) {
    // ... do the reset ...
    req.respond(true);
  }, /* dangerous = */ true);
}

void loop() {
  Kelvarune.update();                     // Required: sends periodic System Metrics,
                                           // and drives the Wi-Fi transport if used.

  Kelvarune.metric("temperature", readTemperature(), "°C");
  delay(1000);
}
```

To use Wi-Fi instead of BLE, call `Kelvarune.beginWiFi("My Device", ssid, password)` instead of `begin()`. Because Wi-Fi is polling-based, avoid blocking `loop()` with `delay()` for long periods — use non-blocking, `millis()`-based timing instead (see `examples/WiFiHelloWorld`).

## Examples

- [`examples/HelloWorld`](examples/HelloWorld) — BLE, all Dashboard tile types, Console, Events, and all 6 Command types.
- [`examples/WiFiHelloWorld`](examples/WiFiHelloWorld) — the same content over Wi-Fi/WebSocket. Copy `secrets.h.example` to `secrets.h` and fill in your Wi-Fi credentials before building.

## Getting the app

The Kelvarune app is required to view the Dashboard/Console/Commands your device exposes: https://minervasoft.co.jp/kelvarune

- [App Store](https://apps.apple.com/us/app/id6810004105) (iOS)
- [Google Play](https://play.google.com/store/apps/details?id=jp.co.minervasoft.kelvarune&hl=en) (Android)

## API overview

| Method | Purpose |
|---|---|
| `Kelvarune.begin(deviceName)` | Start the device over BLE (NUS-compatible GATT service) |
| `Kelvarune.beginWiFi(deviceName, ssid, password)` | Start the device over Wi-Fi (mDNS + WebSocket) |
| `Kelvarune.update()` | Call every `loop()` iteration; required for both transports |
| `Kelvarune.metric(id, value, unit)` | Report a value (`float`/`int`/`bool` overloads); creates a Dashboard tile on first call |
| `Kelvarune.configureMetric(id, display, min, max)` | Choose a Dashboard tile's display type (Gauge/Bar/LED/Line/Number) and range |
| `Kelvarune.log(message, level)` | Send a line to the Console (`KV_LOG_DEBUG/INFO/WARN/ERROR`) |
| `Kelvarune.event(category, message)` | Send a free-form event |
| `Kelvarune.command(id, label, type, handler, dangerous)` | Register a Command the app can invoke |
| `Kelvarune.isConnected()` | Whether a client is currently connected |
| `Kelvarune.setFirmwareVersion(version)` / `setBuildVersion(version)` | Shown in the app's Device info |

See the header comments in [`src/Kelvarune.h`](src/Kelvarune.h) for full details (types, `KVCommandRequest`, display types, etc.).

## License

[MIT](LICENSE) © Minerva, Inc.

## Support

Issues and questions: https://github.com/minervasoft-inc/Kelvarune/issues, or info@minervasoft.co.jp
