# Kelvarune SDK

[English README is here](README.md)

**[Kelvarune](https://minervasoft.co.jp/kelvarune)** アプリ（iOS / Android）から、Bluetooth Low Energy または Wi-Fi 経由でデバイスを監視・デバッグ・操作できるようにする、ESP32向け Arduino ライブラリです。

スケッチに数行追加するだけで、UI コードを一切書かずにアプリ側で以下が使えるようになります:

- ファームウェアから送った値をリアルタイム表示する **Dashboard**（Number / Gauge / Bar / LED / Line Graph タイル）
- debug/info/warn/error ログと自由記述のイベントを表示する **Console**
- スケッチ内のコードを呼び出し、成功/失敗を返す **Commands**（ボタン、数値/文字列入力、トグル、Enum）
- 自動送信される **System Metrics**（稼働時間、CPUクロック、ヒープ使用量、Wi-Fi RSSI 等）

## 必要要件

- ESP32（Arduino core for ESP32）
- Arduino IDE 2.x または `arduino-cli`
- 依存ライブラリ（Arduino Library Manager 経由でインストールすれば自動的に解決されます。手動導入の場合は個別にインストールしてください）:
  - [ArduinoJson](https://arduinojson.org/) >= 7.0.0
  - [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) >= 2.0.0（BLE Transport用）
  - [WebSockets](https://github.com/Links2004/arduinoWebSockets) >= 2.7.0（Wi-Fi Transport用）

実際に使う Transport（`begin()` なら BLE、`beginWiFi()` なら Wi-Fi）に対応する依存ライブラリのみで構いません。

## インストール方法

### Arduino Library Manager（登録後はこちらを推奨）

スケッチ → ライブラリをインクルード → ライブラリを管理… → 「Kelvarune」を検索 → インストール。

### 手動インストール

1. 本リポジトリを ZIP でダウンロード（Code → Download ZIP）、または clone する
2. Arduino IDE: スケッチ → ライブラリをインクルード → .ZIP形式のライブラリをインストール、または `libraries/` フォルダへ直接コピーする
3. 上記の依存ライブラリを Library Manager からインストールする

## クイックスタート

```cpp
#include <Kelvarune.h>

void setup() {
  Kelvarune.begin("My Device");           // BLEでアドバタイズを開始
  Kelvarune.setFirmwareVersion("1.0.0");

  // Button Command。アプリでタップするとこのハンドラが呼ばれる。
  Kelvarune.command("reset", "Reset", KV_CMD_BUTTON, [](KVCommandRequest req) {
    // ... リセット処理 ...
    req.respond(true);
  }, /* dangerous = */ true);
}

void loop() {
  Kelvarune.update();                     // 必須: System Metricsの定期送信、
                                           // およびWi-Fi Transport使用時の駆動を行う。

  Kelvarune.metric("temperature", readTemperature(), "°C");
  delay(1000);
}
```

BLE の代わりに Wi-Fi を使う場合は、`begin()` の代わりに `Kelvarune.beginWiFi("My Device", ssid, password)` を呼びます。Wi-Fi はポーリング方式のため、`loop()` を長時間 `delay()` でブロックしないでください。代わりに `millis()` ベースの非ブロッキングな間隔制御を使ってください（`examples/WiFiHelloWorld` 参照）。

## サンプル

- [`examples/HelloWorld`](examples/HelloWorld) — BLE版。Dashboardの全表示形式、Console、Events、6種類全てのCommandを含む
- [`examples/WiFiHelloWorld`](examples/WiFiHelloWorld) — 同内容のWi-Fi/WebSocket版。ビルド前に `secrets.h.example` を `secrets.h` にコピーし、Wi-Fi の SSID/パスワードを設定してください

## アプリの入手

デバイスが公開する Dashboard/Console/Commands を見るには Kelvarune アプリが必要です: https://minervasoft.co.jp/kelvarune

## API 概要

| メソッド | 用途 |
|---|---|
| `Kelvarune.begin(deviceName)` | BLE（NUS互換GATTサービス）でデバイスを起動する |
| `Kelvarune.beginWiFi(deviceName, ssid, password)` | Wi-Fi（mDNS + WebSocket）でデバイスを起動する |
| `Kelvarune.update()` | `loop()` の毎回呼ぶ。両Transportで必須 |
| `Kelvarune.metric(id, value, unit)` | 値を送信する（`float`/`int`/`bool` のオーバーロード）。初回呼び出しでDashboardタイルが自動生成される |
| `Kelvarune.configureMetric(id, display, min, max)` | Dashboardタイルの表示形式（Gauge/Bar/LED/Line/Number）と範囲を指定する |
| `Kelvarune.log(message, level)` | Consoleへ1行送る（`KV_LOG_DEBUG/INFO/WARN/ERROR`） |
| `Kelvarune.event(category, message)` | 自由記述のイベントを送る |
| `Kelvarune.command(id, label, type, handler, dangerous)` | アプリから呼び出せるCommandを登録する |
| `Kelvarune.isConnected()` | 現在クライアントが接続中かどうか |
| `Kelvarune.setFirmwareVersion(version)` / `setBuildVersion(version)` | アプリのDevice情報欄に表示される |

詳細（各型、`KVCommandRequest`、表示形式等）は [`src/Kelvarune.h`](src/Kelvarune.h) のヘッダーコメントを参照してください。

## ライセンス

[MIT](LICENSE) © Minerva, Inc.

## サポート

不具合報告・お問い合わせ: https://github.com/minervasoft-inc/Kelvarune/issues、または info@minervasoft.co.jp
