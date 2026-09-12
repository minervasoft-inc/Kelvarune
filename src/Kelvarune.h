// Kelvarune SDK
// Copyright (c) Minerva, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <Arduino.h>
// The stock ESP32 BLE library (BLEDevice.h) was found, on real hardware, to stop
// delivering notify() calls (hello_ack, etc.) starting with the second connection
// after a disconnect/reconnect cycle (confirmed not to be a heap leak; this is a
// widely reported issue with this Bluedroid-based library). We use the more
// reliable NimBLE-Arduino library instead (install it separately via the Library
// Manager; see the "Prerequisites" comment in each example .ino).
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <functional>
#include <utility>
#include <vector>

/** Severity level for log(). */
enum KVLogLevel {
    KV_LOG_DEBUG,
    KV_LOG_INFO,
    KV_LOG_WARN,
    KV_LOG_ERROR
};

/** Input type for a Command registered with command(). */
enum KVCommandType {
    KV_CMD_BUTTON,
    KV_CMD_INT,
    KV_CMD_FLOAT,
    KV_CMD_BOOL,
    KV_CMD_STRING,
    KV_CMD_ENUM
};

/**
 * Dashboard display type for a User Metric.
 * If configureMetric() is not called for a given id, it defaults to KV_DISPLAY_NUMBER.
 */
enum KVDisplayType {
    KV_DISPLAY_NUMBER,
    KV_DISPLAY_GAUGE,
    KV_DISPLAY_BAR,
    KV_DISPLAY_LED,
    KV_DISPLAY_LINE
};

/** Display configuration registered via configureMetric(). Kept per metric id internally by the SDK. */
struct KVMetricConfig {
    KVDisplayType display = KV_DISPLAY_NUMBER;
    float min = 0;
    float max = 0;
};

/**
 * Bit flags selecting which Basic System Metrics update() sends.
 * Combine with bitwise OR (e.g. `KV_SYS_UPTIME | KV_SYS_HEAP`).
 * Defaults to KV_SYS_ALL (send everything) when omitted.
 */
enum KVSystemMetric {
    KV_SYS_NONE = 0,
    KV_SYS_UPTIME = 1 << 0,
    KV_SYS_CPU_CLOCK = 1 << 1,
    KV_SYS_HEAP = 1 << 2,
    KV_SYS_MIN_FREE_HEAP = 1 << 3,
    KV_SYS_FLASH_SIZE = 1 << 4,
    KV_SYS_RESET_REASON = 1 << 5,
    KV_SYS_NETWORK_BYTES = 1 << 6,
    KV_SYS_ALL = KV_SYS_UPTIME | KV_SYS_CPU_CLOCK | KV_SYS_HEAP | KV_SYS_MIN_FREE_HEAP |
                 KV_SYS_FLASH_SIZE | KV_SYS_RESET_REASON | KV_SYS_NETWORK_BYTES
};

/**
 * Bit flags selecting which Advanced System Metrics update() sends.
 * These are shown locked in the app unless the user has purchased Kelvarune Pro, but the
 * SDK itself has no notion of that purchase state, so it is up to your firmware to opt in
 * explicitly. Unlike KV_SYS_ALL, the default when omitted is KV_ADV_NONE (send nothing).
 * On boards without PSRAM, passing KV_ADV_PSRAM has no effect (psram_total/used are not sent).
 */
enum KVAdvancedMetric {
    KV_ADV_NONE = 0,
    KV_ADV_LARGEST_FREE_HEAP_BLOCK = 1 << 0,
    KV_ADV_HEAP_FRAGMENTATION = 1 << 1,
    KV_ADV_PSRAM = 1 << 2,
    KV_ADV_FREERTOS_TASK_COUNT = 1 << 3,
    KV_ADV_BOOT_COUNT = 1 << 4,
    KV_ADV_ALL = KV_ADV_LARGEST_FREE_HEAP_BLOCK | KV_ADV_HEAP_FRAGMENTATION | KV_ADV_PSRAM |
                 KV_ADV_FREERTOS_TASK_COUNT | KV_ADV_BOOT_COUNT
};

class KelvaruneClass;

/**
 * A Command invocation request. Only valid inside the handler it was passed to.
 * It is delivered synchronously from code equivalent to update(), so do not store it
 * or reference it after the handler returns (the JsonVariant it wraps points into the
 * receive buffer).
 */
class KVCommandRequest {
public:
    KVCommandRequest(KelvaruneClass* owner, String reqId, JsonVariant value);

    int intValue() const;
    float floatValue() const;
    bool boolValue() const;
    String stringValue() const;

    /** Send back the Command's result as a cmd_res message. */
    void respond(bool ok, const char* error = nullptr);

private:
    KelvaruneClass* owner_;
    String reqId_;
    JsonVariant value_;
};

using KVCommandHandler = std::function<void(KVCommandRequest)>;

struct KVCommandRegistration {
    String id;
    String label;
    KVCommandType type;
    bool dangerous;
    KVCommandHandler handler;
};

/**
 * Kelvarune SDK for ESP32 Arduino Core.
 *
 * Advertises and runs a NUS (Nordic UART Service) compatible BLE GATT service (or a
 * Wi-Fi/WebSocket transport via beginWiFi()), completes the hello / hello_ack / device_info
 * handshake with the Kelvarune app, and lets your sketch report User Metrics (metric()),
 * send log lines and events (log() / event()), and register Commands the app can invoke
 * (command()). update() must be called from loop() to drive Basic/Advanced System Metrics
 * and (for Wi-Fi) the transport itself. See README.md for a quick start and full usage.
 */
class KelvaruneClass {
public:
    /** Initialize BLE and start advertising the NUS-compatible service. */
    void begin(const char* deviceName);

    /**
     * Connect to an existing Wi-Fi network, start a WebSocket server, and advertise over
     * mDNS as `_kelvarune._tcp`. Independent of BLE — call both begin() and beginWiFi() if
     * you want to support both transports at once.
     */
    void beginWiFi(const char* deviceName, const char* ssid, const char* password);

    /**
     * Call this on every loop() iteration. Periodically sends Basic/Advanced System Metrics
     * (does nothing while no client is connected).
     * `systemMetrics` selects which Basic System Metrics to send, as an OR of KVSystemMetric
     * values; defaults to KV_SYS_ALL (send everything).
     * `advancedMetrics` selects which Advanced System Metrics to send (a Pro-only feature in
     * the app), as an OR of KVAdvancedMetric values; defaults to KV_ADV_NONE (send nothing).
     */
    void update(int systemMetrics = KV_SYS_ALL, int advancedMetrics = KV_ADV_NONE);

    /** Whether the Kelvarune Protocol handshake (hello / hello_ack) has completed. */
    bool isConnected() const;

    /** Your firmware's version string, reported in device_info. */
    void setFirmwareVersion(const char* version);

    /** Your firmware's build identifier, reported in device_info. */
    void setBuildVersion(const char* version);

    /**
     * Register and report a User Metric. The first call for a given id sends one metric_def;
     * subsequent calls send only metric_val. Does nothing while not connected (before the
     * hello/hello_ack handshake completes).
     */
    void metric(const char* id, float value, const char* unit = "");
    void metric(const char* id, int value, const char* unit = "");
    /**
     * Sends the value with dataType="bool". The LED/status tile (KV_DISPLAY_LED) strictly
     * requires a boolean-typed value in the app, so use this overload — not the `int`
     * overload with 0/1 — for any Metric meant to drive an LED tile (an int 0/1 will not
     * light it up).
     */
    void metric(const char* id, bool value, const char* unit = "");

    /**
     * Set a User Metric's Dashboard display type (Gauge/Bar/LED/Line) and min/max range.
     * Must be called once in setup(), before the first metric() call for that id — metric_def
     * is only sent on an id's first metric() call, so configureMetric() has to run first.
     * If never called for an id, it is sent with the default KV_DISPLAY_NUMBER.
     */
    void configureMetric(const char* id, KVDisplayType display, float min = 0, float max = 0);

    /** Send a line to the Console. Does nothing while not connected. */
    void log(const char* message, KVLogLevel level = KV_LOG_INFO);

    /** Record a state change in Events. Does nothing while not connected. */
    void event(const char* category, const char* message);

    /**
     * Register a Command the app can invoke. If already connected, its cmd_def is sent
     * immediately; otherwise all registered Commands are sent together once a connection
     * is established.
     */
    void command(const char* id, const char* label, KVCommandType type,
                 KVCommandHandler handler, bool dangerous = false);

    // Internal — called from BLE/WebSocket callbacks or from KVCommandRequest.
    // Do not call these directly from your sketch.
    void handleBleConnect();
    void handleBleDisconnect();
    void handleRxWrite(const String& value);
    void handleWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload, size_t length);
    void sendCommandResponse(const String& reqId, bool ok, const char* error);

private:
    void sendMessage(const String& json);
    void handleIncomingPayload(const String& payload);
    void handleMessage(const String& json);
    void sendDeviceInfo();
    void sendMetricDefIfNeeded(const char* id, const char* scope, const char* dataType, const char* unit);
    void sendCommandDef(const KVCommandRegistration& registration);
    void handleCommandRequest(JsonDocument& doc);
    bool isMetricRegistered(const char* id) const;
    const KVMetricConfig* findMetricConfig(const char* id) const;

    void sendSystemMetricsIfDue(int systemMetrics, int advancedMetrics);
    void sendSystemMetricInt(const char* id, int value, const char* unit);
    void sendSystemMetricBarInt(const char* id, int value, const char* unit, int maxValue);
    void sendSystemMetricString(const char* id, const String& value);
    void sendAdvancedMetrics(int advancedMetrics);

    String loadOrCreateDeviceId();
    String generateDeviceId();
    String detectMcu();
    unsigned long loadAndIncrementBootCount();

    NimBLEServer* server_ = nullptr;
    NimBLECharacteristic* txCharacteristic_ = nullptr;
    NimBLECharacteristic* rxCharacteristic_ = nullptr;
    bool bleConnected_ = false;

    WebSocketsServer* webSocket_ = nullptr;
    int wifiClientNum_ = -1;

    bool protocolReady_ = false;

    String deviceId_;
    String deviceName_;
    String firmwareVersion_;
    String buildVersion_;
    std::vector<String> registeredMetricIds_;
    std::vector<std::pair<String, KVMetricConfig>> metricConfigs_;
    std::vector<KVCommandRegistration> commands_;
    // Handling an incoming message synchronously inside BLE's onWrite() callback (including
    // any notify() it triggers, such as sending hello_ack) can leave the NimBLE stack busy
    // and cause notifications to silently fail. So incoming data is queued here instead, and
    // processed later from update() (i.e. from the main loop context) — see handleRxWrite()/update().
    std::vector<String> pendingBleMessages_;
    unsigned long lastSystemMetricsMillis_ = 0;
    // The System/Advanced Metrics selection (bit flags) passed to the most recent update()
    // call. handleMessage() reads this to make sure System Metrics are sent right away when
    // replying to hello — see update().
    int lastSystemMetricsFlags_ = KV_SYS_ALL;
    int lastAdvancedMetricsFlags_ = KV_ADV_NONE;
    unsigned long totalRxBytes_ = 0;
    unsigned long totalTxBytes_ = 0;
    unsigned long bootCount_ = 0;

    static const int kProtocolVersion = 1;
    static constexpr const char* kSdkVersion = "1.0.1";
    static const unsigned long kSystemMetricsIntervalMillis = 2000;
    static const uint16_t kWebSocketPort = 8081;
};

extern KelvaruneClass Kelvarune;
