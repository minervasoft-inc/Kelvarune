// Copyright (c) Minerva, Inc.
// SPDX-License-Identifier: MIT
//
// Kelvarune SDK サンプル: WiFiHelloWorld（Wi-Fi版）
//
// 既存Wi-Fiに接続し、mDNS(_kelvarune._tcp)で広告、WebSocketサーバを起動する。
// 出力内容はexamples/HelloWorld（BLE版）と同一になるようにしている（接続確立部分のみ異なる）。
// 擬似的なセンサー値でDashboardの主要な表示形式（Number/Gauge/Bar/LED/Line Graph）を
// ひと通り確認でき、Console（DEBUG/INFO/WARN）、Events、6種類のCommand（Button/Bool/
// Int/Float/String/Enum）の動作も一通り含む。
//
// 事前準備:
// 1. Arduino IDEでESP32ボードサポートをインストール
// 2. ライブラリマネージャーで以下をインストール
//    - "ArduinoJson"（7.x以降）
//    - "WebSockets"（Links2004/arduinoWebSockets）
// 3. このKelvaruneフォルダをArduinoのlibrariesフォルダへコピー、
//    またはスケッチブックのlibrariesフォルダにシンボリックリンク
// 4. secrets.h.example をこのフォルダ内に secrets.h という名前でコピーし、
//    実際のWi-Fi SSID/パスワードに書き換える（secrets.h はgitignore対象）
// 5. 書き込み後、KelvaruneアプリのDevice接続画面でWi-Fi探索し、
//    "Kelvarune WiFi Example" という名前のデバイスに接続する

#include <Kelvarune.h>
#include "secrets.h"
#include <math.h>

// "motor_on" Commandで切り替えられるほか、一定間隔でも自動的に切り替わる。
static bool motorOn = true;
// "target_rpm" / "target_temperature" Commandで調整できるオフセット。
static int targetRpmOffset = 0;
static float targetTemperatureOffset = 0.0f;

void sendDemoData(unsigned long tick);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Kelvarune.beginWiFi("Kelvarune WiFi Example", WIFI_SSID, WIFI_PASSWORD);
  Kelvarune.setFirmwareVersion("1.0.0");
  Kelvarune.setBuildVersion(__DATE__ " " __TIME__);

  // Dashboardの表示形式を一通り確認できるよう、Metricごとに異なる表示形式を設定する
  // （configureMetric()は対象idの最初のmetric()呼び出しより前にsetup()内で呼んでおく
  // 必要がある。呼ばなかった"rpm"は既定のNumber表示になる）。
  Kelvarune.configureMetric("temperature", KV_DISPLAY_GAUGE, 0, 40);  // Gauge
  Kelvarune.configureMetric("humidity", KV_DISPLAY_BAR, 0, 100);      // Bar
  Kelvarune.configureMetric("voltage", KV_DISPLAY_LINE, 3.0, 4.2);    // Line Graph
  Kelvarune.configureMetric("motor", KV_DISPLAY_LED, 0, 1);           // LED

  // Commandは6種類（Button/Bool/Int/Float/String/Enum）を一通り登録する。
  Kelvarune.command("reset", "Reset", KV_CMD_BUTTON, [](KVCommandRequest req) {
    motorOn = true;
    targetRpmOffset = 0;
    targetTemperatureOffset = 0.0f;
    Kelvarune.event("reset", "Device state reset");
    Serial.println("Kelvarune: [reset] command executed");
    req.respond(true);
  }, /* dangerous = */ true);

  Kelvarune.command("motor_on", "Motor", KV_CMD_BOOL, [](KVCommandRequest req) {
    motorOn = req.boolValue();
    Kelvarune.event("motor", motorOn ? "Motor started" : "Motor stopped");
    Serial.print("Kelvarune: [motor_on] command value=");
    Serial.println(motorOn ? "true" : "false");
    req.respond(true);
  });

  Kelvarune.command("target_rpm", "Target RPM", KV_CMD_INT, [](KVCommandRequest req) {
    targetRpmOffset = req.intValue();
    Serial.print("Kelvarune: [target_rpm] command value=");
    Serial.println(targetRpmOffset);
    req.respond(true);
  });

  Kelvarune.command("target_temperature", "Target Temperature", KV_CMD_FLOAT, [](KVCommandRequest req) {
    targetTemperatureOffset = req.floatValue();
    Serial.print("Kelvarune: [target_temperature] command value=");
    Serial.println(targetTemperatureOffset);
    req.respond(true);
  });

  Kelvarune.command("note", "Note", KV_CMD_STRING, [](KVCommandRequest req) {
    String note = req.stringValue();
    Serial.print("Kelvarune: [note] command value=");
    Serial.println(note);
    req.respond(true);
  });

  // ENUMは選択肢モデルが未定義(docs/protocol.md TODO)のため、STRINGと同様に
  // stringValue()で受け取る（アプリ側もテキスト入力にフォールバックする）。
  Kelvarune.command("mode", "Mode", KV_CMD_ENUM, [](KVCommandRequest req) {
    String mode = req.stringValue();
    Serial.print("Kelvarune: [mode] command value=");
    Serial.println(mode);
    req.respond(true);
  });
}

void loop() {
  // 重要: Wi-Fi(WebSocketsServer)はイベント駆動ではなくポーリング方式のため、
  // Kelvarune.update()はできるだけ高頻度に(delay()で止めずに)呼ぶ必要がある。
  // ここでloop()をdelay()等でブロックすると、WebSocketのハンドシェイクが
  // 完了せず接続が確立しない。
  //
  // 送信するSystem Metricsを絞りたい場合は第1引数で指定できる（例）:
  //   Kelvarune.update(KV_SYS_UPTIME | KV_SYS_HEAP);
  // Advanced System Metrics（要件定義書14章、Pro限定機能）は第2引数で指定する
  // （省略時KV_ADV_NONE。ここではDashboardで一通り確認できるようKV_ADV_ALLを指定する）。
  Kelvarune.update(KV_SYS_ALL, KV_ADV_ALL);

  static bool lastConnected = false;
  static unsigned long lastTickMillis = 0;
  static unsigned long tick = 0;

  bool connected = Kelvarune.isConnected();
  if (connected != lastConnected) {
    Serial.println(connected ? "Kelvarune: hello handshake OK" : "Kelvarune: not connected");
    if (connected) {
      Kelvarune.event("connection", "Connected");
    }
    lastConnected = connected;
  }

  // delay()の代わりにmillis()ベースの非ブロッキングな間隔制御で1秒ごとに送信する。
  if (connected && millis() - lastTickMillis >= 1000) {
    lastTickMillis = millis();
    tick++;
    sendDemoData(tick);
  }
}

// HelloWorld / WiFiHelloWorld共通のデモ用データ生成。両サンプルで出力内容を揃えるため、
// 同じ内容をそれぞれのスケッチ内にそのままコピーしている。
void sendDemoData(unsigned long tick) {
  float phase = (float)tick * 0.15f;

  float temperature = 24.0f + sinf(phase) * 4.0f + targetTemperatureOffset;         // 約20〜28℃
  float humidity = 55.0f + sinf(phase * 0.7f + 1.0f) * 15.0f;                       // 約40〜70%
  float voltage = 3.7f + sinf(phase * 0.3f) * 0.25f;                                // 約3.45〜3.95V
  int rpm = 1000 + targetRpmOffset + (int)(sinf(phase * 0.9f) * 60.0f);             // 約940〜1060rpm

  Kelvarune.metric("temperature", temperature, "°C");
  Kelvarune.metric("humidity", humidity, "%");
  Kelvarune.metric("voltage", voltage, "V");
  Kelvarune.metric("rpm", rpm, "rpm");

  // Motorは10秒ごとに自動でトグルする（"motor_on" Commandでいつでも上書きできる）。
  if (tick % 10 == 0) {
    motorOn = !motorOn;
    Kelvarune.event("motor", motorOn ? "Motor started" : "Motor stopped");
  }
  // LED表示(KV_DISPLAY_LED)はBoolean型の値であることを要求するため、必ずbool版の
  // metric()を使う（int(0/1)で送るとアプリ側でON/OFFを判定できず常にOFF表示になる）。
  Kelvarune.metric("motor", motorOn, "");

  if (tick % 5 == 0) {
    Kelvarune.log("Sensor read complete", KV_LOG_DEBUG);
  }
  if (temperature > 27.0f) {
    Kelvarune.log("Temperature approaching upper bound", KV_LOG_WARN);
  }
  if (tick % 15 == 0) {
    Kelvarune.event("info", "Still running");
  }
}
