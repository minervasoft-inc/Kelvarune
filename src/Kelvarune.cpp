// Kelvarune SDK
// Copyright (c) Minerva, Inc.
// SPDX-License-Identifier: MIT

#include "Kelvarune.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
// docs/protocol.md 4.1章で決定したNUS UUID
const char kServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char kRxCharacteristicUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
const char kTxCharacteristicUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
const char kPreferencesNamespace[] = "kelvarune";
const char kPreferencesDeviceIdKey[] = "device_id";
const char kPreferencesBootCountKey[] = "boot_count";

const char* logLevelToString(KVLogLevel level) {
    switch (level) {
        case KV_LOG_DEBUG: return "debug";
        case KV_LOG_WARN: return "warn";
        case KV_LOG_ERROR: return "error";
        case KV_LOG_INFO:
        default: return "info";
    }
}

const char* commandTypeToString(KVCommandType type) {
    switch (type) {
        case KV_CMD_INT: return "int";
        case KV_CMD_FLOAT: return "float";
        case KV_CMD_BOOL: return "bool";
        case KV_CMD_STRING: return "string";
        case KV_CMD_ENUM: return "enum";
        case KV_CMD_BUTTON:
        default: return "button";
    }
}

String resetReasonToString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power_on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "int_watchdog";
        case ESP_RST_TASK_WDT: return "task_watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}
}  // namespace

KVCommandRequest::KVCommandRequest(KelvaruneClass* owner, String reqId, JsonVariant value)
    : owner_(owner), reqId_(reqId), value_(value) {}

int KVCommandRequest::intValue() const { return value_.as<int>(); }
float KVCommandRequest::floatValue() const { return value_.as<float>(); }
bool KVCommandRequest::boolValue() const { return value_.as<bool>(); }
String KVCommandRequest::stringValue() const { return value_.as<String>(); }

void KVCommandRequest::respond(bool ok, const char* error) {
    owner_->sendCommandResponse(reqId_, ok, error);
}

class KelvaruneServerCallbacks : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        Serial.println("Kelvarune: BLE client connected");
        Kelvarune.handleBleConnect();
    }
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        Serial.println("Kelvarune: BLE client disconnected");
        Kelvarune.handleBleDisconnect();
    }
};

class KelvaruneRxCallbacks : public NimBLECharacteristicCallbacks {
public:
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
        // NimBLEのgetValue()はArduinoのStringではなくstd::stringを返すため変換する。
        String value(characteristic->getValue().c_str());
        Serial.print("Kelvarune: RX received (");
        Serial.print(value.length());
        Serial.print(" bytes): ");
        Serial.println(value);
        Kelvarune.handleRxWrite(value);
    }
};

// WebSocketsServer::onEvent()にはキャプチャなしの関数を渡す
// (バージョンによってstd::function/生ポインタどちらの想定か不確実なため、
// どちらでも通る形にしている)。
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
    Kelvarune.handleWebSocketEvent(clientNum, type, payload, length);
}

void KelvaruneClass::begin(const char* deviceName) {
    deviceName_ = String(deviceName);
    deviceId_ = loadOrCreateDeviceId();
    bootCount_ = loadAndIncrementBootCount();

    NimBLEDevice::init(deviceName);

    server_ = NimBLEDevice::createServer();
    server_->setCallbacks(new KelvaruneServerCallbacks());

    NimBLEService* service = server_->createService(kServiceUuid);

    // NimBLEは、通知(NOTIFY)属性を持つ特性の通知有効化記述子(CCCD)を自動的に管理する
    // ため、標準BLEライブラリのようにBLE2902記述子を手動で追加する必要が無い。
    txCharacteristic_ = service->createCharacteristic(
        kTxCharacteristicUuid,
        NIMBLE_PROPERTY::NOTIFY
    );

    rxCharacteristic_ = service->createCharacteristic(
        kRxCharacteristicUuid,
        NIMBLE_PROPERTY::WRITE
    );
    rxCharacteristic_->setCallbacks(new KelvaruneRxCallbacks());

    service->start();

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->enableScanResponse(true);
    // NimBLEDevice::init()が設定するのはGATTの「Device Name」特性（接続後にしか読めない）
    // のみで、標準のBLEライブラリと異なりスキャン中に見えるアドバタイズ/スキャン応答
    // パケットには自動で名前が含まれない。ここで明示的に設定しないと、スキャン中の
    // デバイス名が空になる（実機で確認、Android側は「(名称不明)」表示になっていた）。
    advertising->setName(deviceName);
    NimBLEDevice::startAdvertising();
}

void KelvaruneClass::beginWiFi(const char* deviceName, const char* ssid, const char* password) {
    deviceName_ = String(deviceName);
    deviceId_ = loadOrCreateDeviceId();
    bootCount_ = loadAndIncrementBootCount();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Kelvarune: connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("Kelvarune: WiFi connected, IP=");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin(deviceName)) {
        Serial.println("Kelvarune: mDNS init failed");
    } else {
        MDNS.addService("kelvarune", "tcp", kWebSocketPort);
        Serial.println("Kelvarune: mDNS service advertised (_kelvarune._tcp)");
    }

    webSocket_ = new WebSocketsServer(kWebSocketPort);
    webSocket_->begin();
    webSocket_->onEvent(onWebSocketEvent);
    Serial.print("Kelvarune: WebSocket server started on port ");
    Serial.println(kWebSocketPort);
}

bool KelvaruneClass::isConnected() const {
    return protocolReady_;
}

void KelvaruneClass::setFirmwareVersion(const char* version) {
    firmwareVersion_ = String(version);
}

void KelvaruneClass::setBuildVersion(const char* version) {
    buildVersion_ = String(version);
}

void KelvaruneClass::handleBleConnect() {
    bleConnected_ = true;
}

void KelvaruneClass::handleBleDisconnect() {
    bleConnected_ = false;
    protocolReady_ = false;
    // 切断後も再度見つけてもらえるよう、アドバタイズを再開する。
    NimBLEDevice::startAdvertising();
}

void KelvaruneClass::handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            wifiClientNum_ = clientNum;
            Serial.println("Kelvarune: WebSocket client connected");
            break;

        case WStype_DISCONNECTED:
            if (wifiClientNum_ == clientNum) {
                wifiClientNum_ = -1;
                protocolReady_ = false;
            }
            Serial.println("Kelvarune: WebSocket client disconnected");
            break;

        case WStype_TEXT: {
            String message;
            message.reserve(length);
            for (size_t i = 0; i < length; i++) {
                message += static_cast<char>(payload[i]);
            }
            handleIncomingPayload(message);
            break;
        }

        default:
            break;
    }
}

void KelvaruneClass::handleIncomingPayload(const String& payload) {
    if (payload.length() == 0) {
        return;
    }
    totalRxBytes_ += payload.length();
    handleMessage(payload);
}

void KelvaruneClass::handleRxWrite(const String& value) {
    // ここはNimBLEのonWrite()コールバックから呼ばれる。このコールバックの中で
    // notify()等のGATT操作を同期的に行うと、NimBLEスタックがビジー状態になり
    // 通知が黙って失敗することがある（実機で確認。hello受信直後に送るhello_ackが
    // 一切届かなくなる不具合の原因）。そのため実際の処理(handleIncomingPayload)は
    // ここでは行わず、一旦キューに積むだけにして、update()側（メインループの
    // コンテキスト、コールバックの外）で処理する。
    pendingBleMessages_.push_back(value);
}

void KelvaruneClass::sendMessage(const String& json) {
    totalTxBytes_ += json.length();

    if (txCharacteristic_ != nullptr && bleConnected_) {
        txCharacteristic_->setValue(json.c_str());
        txCharacteristic_->notify();
        // notify()を間隔なく連続で呼ぶと、BLEスタック内部で後続の通知が
        // 取りこぼされることがあるため、送信のたびに短い間隔を空ける。
        // (hello直後にhello_ack/device_info/cmd_def...と連続送信するため必要)
        delay(20);
    }

    if (webSocket_ != nullptr && wifiClientNum_ >= 0) {
        // sendTXT()はString&（非const参照）を要求するため、constのjsonをそのまま渡せない。
        String payload = json;
        webSocket_->sendTXT(wifiClientNum_, payload);
    }
}

void KelvaruneClass::update(int systemMetrics, int advancedMetrics) {
    // hello応答時にSystem Metricsを確実に先出しするため、handleMessage()側で
    // 使えるように選択内容を控えておく（下のlastSystemMetricsFlags_参照）。
    lastSystemMetricsFlags_ = systemMetrics;
    lastAdvancedMetricsFlags_ = advancedMetrics;

    // BLEのonWrite()コールバックから溜めておいた受信メッセージを、メインループの
    // コンテキストで処理する（handleRxWriteのコメント参照。hello受信前も含め、
    // 未接続時のhello自体もここで処理されるため、下のprotocolReady_チェックより
    // 前に置く必要がある）。
    while (!pendingBleMessages_.empty()) {
        String message = pendingBleMessages_.front();
        pendingBleMessages_.erase(pendingBleMessages_.begin());
        handleIncomingPayload(message);
    }

    // WebSocketServerは同期ポーリング方式のため、hello受信前も含め毎回呼ぶ必要がある。
    if (webSocket_ != nullptr) {
        webSocket_->loop();
    }

    if (!protocolReady_) {
        return;
    }
    sendSystemMetricsIfDue(systemMetrics, advancedMetrics);
}

void KelvaruneClass::handleMessage(const String& json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        Serial.print("Kelvarune: JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }

    const char* type = doc["t"];
    if (type == nullptr) {
        Serial.println("Kelvarune: message has no \"t\" field");
        return;
    }

    Serial.print("Kelvarune: message type = ");
    Serial.println(type);

    if (strcmp(type, "cmd_req") == 0) {
        handleCommandRequest(doc);
        return;
    }

    if (strcmp(type, "hello") == 0) {
        // helloを受け取ったこの時点では、MTU（1回の通知で送れるデータサイズ）の交渉が
        // まだ完了していないことがある。Android版アプリは接続直後に明示的に大きな
        // MTUを要求するが、iOSはOS側が自動的に（アプリからは制御できないタイミングで）
        // 交渉するため、この直後に送るhello_ack/device_info等のJSON（既定MTUでは
        // 収まらない）が届かず、notify()が停止することがある(実機で確認)。応答を
        // 送り始める前に少し待ち、MTU交渉が完了する時間的な余裕を持たせる。
        // ここはBLEコールバックの外（update()経由）で呼ばれるためdelay()して安全。
        delay(300);

        int appProtocolVersion = doc["protocolVersion"] | 0;
        bool compatible = (appProtocolVersion == kProtocolVersion);
        protocolReady_ = compatible;

        if (compatible) {
            // Appは接続の都度Metric定義を再構築する前提(docs/domain-model.md)のため、
            // 新しい接続ごとにmetric_def送信済み状態をリセットし、必ず送り直す。
            registeredMetricIds_.clear();
        }

        JsonDocument ack;
        ack["t"] = "hello_ack";
        ack["protocolVersion"] = kProtocolVersion;
        ack["compatible"] = compatible;
        if (!compatible) {
            ack["reason"] = "protocol_version_mismatch";
        }

        String out;
        serializeJson(ack, out);
        sendMessage(out);

        if (compatible) {
            sendDeviceInfo();
            for (const auto& registration : commands_) {
                sendCommandDef(registration);
            }

            // System Metricsの初回送信を、通常の定期送信(update()内のタイミング判定)に
            // 任せず、ここでhello応答の一部として同期的に必ず行う。そうしないと、
            // System MetricsとUser Metrics(利用者の loop() が独自のタイミングで送る)の
            // どちらが先に届くかが実行タイミング次第で入れ替わり、Dashboardでの
            // タイル表示順が接続のたびに前後してしまう（ユーザーからの指摘に対応）。
            // ここで送っておけば、利用者のloop()が最初のUser Metricsを送るより必ず前に
            // System Metricsの定義(metric_def)が届く。
            lastSystemMetricsMillis_ = 0;
            sendSystemMetricsIfDue(lastSystemMetricsFlags_, lastAdvancedMetricsFlags_);
        }
    }
}

void KelvaruneClass::sendDeviceInfo() {
    JsonDocument doc;
    doc["t"] = "device_info";
    doc["deviceId"] = deviceId_;
    doc["deviceName"] = deviceName_;
    doc["mcu"] = detectMcu();
    doc["sdkVersion"] = kSdkVersion;
    doc["protocolVersion"] = kProtocolVersion;
    doc["firmwareVersion"] = firmwareVersion_;
    doc["buildVersion"] = buildVersion_;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::log(const char* message, KVLogLevel level) {
    if (!protocolReady_) {
        return;
    }
    JsonDocument doc;
    doc["t"] = "log";
    doc["level"] = logLevelToString(level);
    doc["msg"] = message;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::event(const char* category, const char* message) {
    if (!protocolReady_) {
        return;
    }
    JsonDocument doc;
    doc["t"] = "event";
    doc["category"] = category;
    doc["msg"] = message;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

bool KelvaruneClass::isMetricRegistered(const char* id) const {
    for (const auto& registered : registeredMetricIds_) {
        if (registered == id) {
            return true;
        }
    }
    return false;
}

const KVMetricConfig* KelvaruneClass::findMetricConfig(const char* id) const {
    for (const auto& entry : metricConfigs_) {
        if (entry.first == id) {
            return &entry.second;
        }
    }
    return nullptr;
}

static const char* kvDisplayTypeToString(KVDisplayType display) {
    switch (display) {
        case KV_DISPLAY_GAUGE: return "gauge";
        case KV_DISPLAY_BAR: return "bar";
        case KV_DISPLAY_LED: return "led";
        case KV_DISPLAY_LINE: return "line";
        case KV_DISPLAY_NUMBER:
        default: return "number";
    }
}

void KelvaruneClass::configureMetric(const char* id, KVDisplayType display, float min, float max) {
    KVMetricConfig config;
    config.display = display;
    config.min = min;
    config.max = max;
    for (auto& entry : metricConfigs_) {
        if (entry.first == id) {
            entry.second = config;
            return;
        }
    }
    metricConfigs_.push_back(std::make_pair(String(id), config));
}

void KelvaruneClass::sendMetricDefIfNeeded(const char* id, const char* scope, const char* dataType, const char* unit) {
    if (isMetricRegistered(id)) {
        return;
    }
    registeredMetricIds_.push_back(String(id));

    JsonDocument doc;
    doc["t"] = "metric_def";
    doc["scope"] = scope;
    doc["id"] = id;
    doc["label"] = id;
    doc["dataType"] = dataType;
    doc["unit"] = unit;

    const KVMetricConfig* config = findMetricConfig(id);
    KVDisplayType display = config != nullptr ? config->display : KV_DISPLAY_NUMBER;
    doc["display"] = kvDisplayTypeToString(display);
    if (config != nullptr && (display == KV_DISPLAY_GAUGE || display == KV_DISPLAY_BAR)) {
        doc["min"] = config->min;
        doc["max"] = config->max;
    }

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::metric(const char* id, float value, const char* unit) {
    if (!protocolReady_) {
        return;
    }
    sendMetricDefIfNeeded(id, "user", "float", unit);

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::metric(const char* id, int value, const char* unit) {
    if (!protocolReady_) {
        return;
    }
    sendMetricDefIfNeeded(id, "user", "int", unit);

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::metric(const char* id, bool value, const char* unit) {
    if (!protocolReady_) {
        return;
    }
    sendMetricDefIfNeeded(id, "user", "bool", unit);

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::sendSystemMetricInt(const char* id, int value, const char* unit) {
    sendMetricDefIfNeeded(id, "system", "int", unit);

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::sendSystemMetricBarInt(const char* id, int value, const char* unit, int maxValue) {
    if (!isMetricRegistered(id)) {
        registeredMetricIds_.push_back(String(id));

        JsonDocument doc;
        doc["t"] = "metric_def";
        doc["scope"] = "system";
        doc["id"] = id;
        doc["label"] = id;
        doc["dataType"] = "int";
        doc["unit"] = unit;
        doc["display"] = "bar";
        doc["min"] = 0;
        doc["max"] = maxValue;

        String out;
        serializeJson(doc, out);
        sendMessage(out);
    }

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::sendSystemMetricString(const char* id, const String& value) {
    sendMetricDefIfNeeded(id, "system", "string", "");

    JsonDocument doc;
    doc["t"] = "metric_val";
    JsonArray values = doc["values"].to<JsonArray>();
    JsonObject entry = values.add<JsonObject>();
    entry["id"] = id;
    entry["v"] = value;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::sendSystemMetricsIfDue(int systemMetrics, int advancedMetrics) {
    unsigned long now = millis();
    if (lastSystemMetricsMillis_ != 0 && (now - lastSystemMetricsMillis_) < kSystemMetricsIntervalMillis) {
        return;
    }
    lastSystemMetricsMillis_ = now;

    // 要件定義書14章 Basic System Metrics（取得容易なもののみ。BLE RSSI等はTODO）。
    // 引数systemMetricsのビットフラグで選択されたものだけ送信する。
    if (systemMetrics & KV_SYS_UPTIME) {
        sendSystemMetricInt("uptime", static_cast<int>(now / 1000), "s");
    }
    if (systemMetrics & KV_SYS_CPU_CLOCK) {
        sendSystemMetricInt("cpu_clock", static_cast<int>(ESP.getCpuFreqMHz()), "MHz");
    }
    if (systemMetrics & KV_SYS_HEAP) {
        // 「使用量 / 総容量」がひと目で分かるようBar表示（総容量を上限値として持たせる）。
        int heapTotal = static_cast<int>(ESP.getHeapSize());
        int heapUsed = heapTotal - static_cast<int>(ESP.getFreeHeap());
        sendSystemMetricBarInt("heap_used", heapUsed, "bytes", heapTotal);
    }
    if (systemMetrics & KV_SYS_MIN_FREE_HEAP) {
        sendSystemMetricInt("min_free_heap", static_cast<int>(ESP.getMinFreeHeap()), "bytes");
    }
    if (systemMetrics & KV_SYS_FLASH_SIZE) {
        sendSystemMetricInt("flash_size", static_cast<int>(ESP.getFlashChipSize()), "bytes");
    }
    if (systemMetrics & KV_SYS_RESET_REASON) {
        sendSystemMetricString("reset_reason", resetReasonToString());
    }
    if (systemMetrics & KV_SYS_NETWORK_BYTES) {
        sendSystemMetricInt("rx_bytes", static_cast<int>(totalRxBytes_), "bytes");
        sendSystemMetricInt("tx_bytes", static_cast<int>(totalTxBytes_), "bytes");
    }

    sendAdvancedMetrics(advancedMetrics);
}

void KelvaruneClass::sendAdvancedMetrics(int advancedMetrics) {
    // 要件定義書14章 Advanced System Metrics（Pro限定機能）。docs/system-metrics.md参照。
    // CPU Load/Core別Loadは「実装依存の参考値」と明記されており、誤解を招く数値を
    // 出さないため見送っている。FreeRTOS Task一覧はTask数のみの簡易版（詳細な一覧・解析は
    // 要件定義書34章「FreeRTOS高度解析」として将来拡張）。
    if (advancedMetrics & KV_ADV_LARGEST_FREE_HEAP_BLOCK) {
        sendSystemMetricInt("largest_free_heap_block", static_cast<int>(ESP.getMaxAllocHeap()), "bytes");
    }
    if (advancedMetrics & KV_ADV_HEAP_FRAGMENTATION) {
        int freeHeap = static_cast<int>(ESP.getFreeHeap());
        int largestBlock = static_cast<int>(ESP.getMaxAllocHeap());
        // 空きヒープのうち、最大の連続確保可能ブロックが占める割合の逆数（%）。
        // 空きが無い場合は断片化そのものが無意味なため0%として扱う。
        int fragmentationPercent = (freeHeap > 0) ? (100 - (largestBlock * 100 / freeHeap)) : 0;
        sendSystemMetricInt("heap_fragmentation", fragmentationPercent, "%");
    }
    if (advancedMetrics & KV_ADV_PSRAM) {
        // 非搭載ボードはgetPsramSize()が0を返すため、その場合は送信しない
        // （取得不可項目は送信しない、要件定義書14章の方針）。
        int psramTotal = static_cast<int>(ESP.getPsramSize());
        if (psramTotal > 0) {
            int psramUsed = psramTotal - static_cast<int>(ESP.getFreePsram());
            sendSystemMetricBarInt("psram_used", psramUsed, "bytes", psramTotal);
        }
    }
    if (advancedMetrics & KV_ADV_FREERTOS_TASK_COUNT) {
        sendSystemMetricInt("freertos_task_count", static_cast<int>(uxTaskGetNumberOfTasks()), "");
    }
    if (advancedMetrics & KV_ADV_BOOT_COUNT) {
        sendSystemMetricInt("boot_count", static_cast<int>(bootCount_), "");
    }
}

void KelvaruneClass::command(const char* id, const char* label, KVCommandType type,
                              KVCommandHandler handler, bool dangerous) {
    KVCommandRegistration registration;
    registration.id = String(id);
    registration.label = String(label);
    registration.type = type;
    registration.dangerous = dangerous;
    registration.handler = handler;
    commands_.push_back(registration);

    if (protocolReady_) {
        sendCommandDef(registration);
    }
}

void KelvaruneClass::sendCommandDef(const KVCommandRegistration& registration) {
    JsonDocument doc;
    doc["t"] = "cmd_def";
    doc["id"] = registration.id;
    doc["label"] = registration.label;
    doc["type"] = commandTypeToString(registration.type);
    doc["dangerous"] = registration.dangerous;

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

void KelvaruneClass::handleCommandRequest(JsonDocument& doc) {
    String id = doc["id"] | "";
    String reqId = doc["reqId"] | "";
    JsonVariant value = doc["value"];

    for (const auto& registration : commands_) {
        if (registration.id == id) {
            if (registration.handler) {
                registration.handler(KVCommandRequest(this, reqId, value));
            }
            return;
        }
    }

    sendCommandResponse(reqId, false, "unknown_command");
}

void KelvaruneClass::sendCommandResponse(const String& reqId, bool ok, const char* error) {
    JsonDocument doc;
    doc["t"] = "cmd_res";
    doc["reqId"] = reqId;
    doc["ok"] = ok;
    if (!ok && error != nullptr) {
        doc["error"] = error;
    }

    String out;
    serializeJson(doc, out);
    sendMessage(out);
}

String KelvaruneClass::loadOrCreateDeviceId() {
    Preferences preferences;
    preferences.begin(kPreferencesNamespace, false);
    String id = preferences.getString(kPreferencesDeviceIdKey, "");
    if (id.length() == 0) {
        id = generateDeviceId();
        preferences.putString(kPreferencesDeviceIdKey, id);
    }
    preferences.end();
    return id;
}

unsigned long KelvaruneClass::loadAndIncrementBootCount() {
    // KV_ADV_BOOT_COUNT用（要件定義書14章）。begin()/beginWiFi()の呼び出し毎、
    // つまり起動の都度1回だけ増分する。
    Preferences preferences;
    preferences.begin(kPreferencesNamespace, false);
    unsigned long count = preferences.getULong(kPreferencesBootCountKey, 0) + 1;
    preferences.putULong(kPreferencesBootCountKey, count);
    preferences.end();
    return count;
}

String KelvaruneClass::generateDeviceId() {
    // ESP32固有のMACアドレス由来の値からIDを生成する。
    // 要件定義書9章の通り、MACアドレス自体を主キーにはせず、
    // ここで生成した値をNVSに永続化して以後再利用する。
    uint64_t mac = ESP.getEfuseMac();
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "KV-%08X", static_cast<uint32_t>(mac & 0xFFFFFFFF));
    return String(buffer);
}

String KelvaruneClass::detectMcu() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    return "ESP32-S2";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "ESP32-C3";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    return "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32)
    return "ESP32";
#else
    return "ESP32 (unknown variant)";
#endif
}

KelvaruneClass Kelvarune;
