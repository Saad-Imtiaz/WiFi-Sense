#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "signal_processor.h"

class RadarWebServer {
public:
    RadarWebServer(uint16_t port = 80);

    // Start the web server. Call after WiFi and LittleFS are ready.
    void begin();

    // Send detection data to all connected WebSocket clients.
    // Call this from the main loop at your desired update rate.
    void sendUpdate(const DetectionResult& result);

    // Clean up stale WebSocket connections. Call periodically.
    void cleanup();

    // Callbacks for settings changes from the web UI
    typedef void (*SettingsCallback)(int motionThreshold, int presenceThreshold, int scanInterval);
    void onSettingsChange(SettingsCallback cb) { _settingsCb = cb; }

    // Callbacks for room config changes
    typedef void (*RoomCallback)(float roomWidth, float roomLength);
    void onRoomChange(RoomCallback cb) { _roomCb = cb; }

private:
    AsyncWebServer _server;
    AsyncWebSocket _ws;

    SettingsCallback _settingsCb;
    RoomCallback     _roomCb;

    void _setupRoutes();
    void _onWSEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                    AwsEventType type, void* arg, uint8_t* data, size_t len);
    void _handleWSMessage(uint8_t* data, size_t len);
};

#endif
