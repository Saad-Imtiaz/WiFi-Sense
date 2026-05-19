/*
 * WiFi-Sense - V1 Standalone Person Detector
 * ===========================================
 * Single ESP32-S3 + WiFi router for room presence & motion detection.
 * Uses both RSSI and CSI (Channel State Information) for high sensitivity.
 *
 * Setup:
 *   1. Set your WiFi credentials below
 *   2. Upload sketch to ESP32-S3
 *   3. Upload data/ folder to LittleFS (Arduino IDE: Tools > ESP32 Sketch Data Upload)
 *   4. Open Serial Monitor at 115200 baud to see the IP address
 *   5. Open browser to the IP address shown
 *
 * Hardware: ESP32-S3-WROOM-1
 * Board:    ESP32S3 Dev Module
 */

#include <WiFi.h>
#include <esp_wifi.h>
#include <LittleFS.h>

#include "signal_processor.h"
#include "csi_collector.h"
#include "web_server.h"

// ===================== USER CONFIGURATION =====================

const char* WIFI_SSID     = "xxxx";
const char* WIFI_PASSWORD = "xxxx";

// Detection tuning
int motionThreshold   = 15;   // Motion detection threshold (higher = less sensitive)
int presenceThreshold = 5;    // Presence detection threshold
int scanIntervalMs    = 100;  // How often to sample (ms)

// Room dimensions (meters) - also configurable from web UI
float roomWidth  = 5.0;
float roomLength = 4.0;

// ===================== END USER CONFIG ========================

SignalProcessor processor;
CSICollector    csiCollector;
RadarWebServer  webServer(80);

unsigned long lastScanTime = 0;
unsigned long lastCleanupTime = 0;

// Called when user changes settings from the web UI
void onSettingsChanged(int motionTh, int presenceTh, int scanInt) {
    motionThreshold = motionTh;
    presenceThreshold = presenceTh;
    scanIntervalMs = scanInt;

    processor.setMotionThreshold(motionThreshold);
    processor.setPresenceThreshold(presenceThreshold);

    Serial.printf("Settings updated: motion=%d, presence=%d, interval=%dms\n",
                  motionThreshold, presenceThreshold, scanIntervalMs);
}

void onRoomChanged(float width, float length) {
    roomWidth = width;
    roomLength = length;
    Serial.printf("Room updated: %.1f x %.1f m\n", roomWidth, roomLength);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("====================================");
    Serial.println("  WiFi-Sense - V1 Standalone");
    Serial.println("  ESP32-S3 Person Detector");
    Serial.println("====================================");

    // Initialize LittleFS for web assets
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed!");
    } else {
        Serial.println("LittleFS mounted");
    }

    // Connect to WiFi
    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.println(WiFi.RSSI());
    } else {
        Serial.println(" FAILED!");
        Serial.println("Starting AP mode: SSID='WiFi-Sense', connect and go to 192.168.4.1");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("WiFi-Sense", "radarpass");
        // Keep trying to connect in background
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    // Initialize signal processor
    if (!processor.init(64, 32, motionThreshold, 8)) {
        Serial.println("ERROR: Signal processor init failed!");
    } else {
        processor.setMotionThreshold(motionThreshold);
        processor.setPresenceThreshold(presenceThreshold);
        Serial.println("Signal processor initialized");
    }

    // Start CSI collection
    if (!csiCollector.begin()) {
        Serial.println("WARNING: CSI collection failed to start (will use RSSI only)");
    }

    // Start web server
    webServer.onSettingsChange(onSettingsChanged);
    webServer.onRoomChange(onRoomChanged);
    webServer.begin();

    Serial.println();
    Serial.println("System ready! Open browser to:");
    Serial.printf("  http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("====================================");
}

void loop() {
    unsigned long now = millis();

    // Process at configured interval
    if (now - lastScanTime >= (unsigned long)scanIntervalMs) {
        lastScanTime = now;

        // Get RSSI from WiFi connection
        int rssi = WiFi.RSSI();
        if (rssi != 0) {
            processor.processRSSI(rssi);
        }

        // Get CSI data if available
        CSIData csiData;
        if (csiCollector.available() && csiCollector.read(csiData)) {
            processor.processCSI(csiData.amplitudes, csiData.numSubcarriers);
        }

        // Get combined result and send to web clients
        DetectionResult result = processor.getResult();
        webServer.sendUpdate(result);

        // Serial output for debugging
        static unsigned long lastPrint = 0;
        if (now - lastPrint >= 1000) {
            lastPrint = now;
            Serial.printf("[%s] RSSI:%d CSI:%.1f Motion:%d/%d Presence:%s Motion:%s\n",
                          WiFi.localIP().toString().c_str(),
                          result.rssi, result.csiAmplitudeAvg,
                          result.motionLevel, result.csiMotionLevel,
                          result.presenceDetected ? "YES" : "no",
                          result.motionDetected ? "YES" : "no");
        }
    }

    // Periodic WebSocket cleanup
    if (now - lastCleanupTime >= 5000) {
        lastCleanupTime = now;
        webServer.cleanup();
    }

    // Reconnect WiFi if lost (require multiple consecutive failures to avoid false triggers)
    static unsigned long lastReconnect = 0;
    static int disconnectCount = 0;
    if (WiFi.status() != WL_CONNECTED) {
        disconnectCount++;
        if (disconnectCount >= 10 && now - lastReconnect >= 15000) {
            lastReconnect = now;
            disconnectCount = 0;
            Serial.println("WiFi lost, reconnecting...");
            WiFi.reconnect();
        }
    } else {
        disconnectCount = 0;
    }
}
