#include "web_server.h"
#include <WiFi.h>
#include <ArduinoJson.h>

RadarWebServer::RadarWebServer(uint16_t port)
    : _server(port), _ws("/ws"), _settingsCb(nullptr), _roomCb(nullptr)
{
}

void RadarWebServer::begin() {
    // WebSocket event handler
    _ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                       AwsEventType type, void* arg, uint8_t* data, size_t len) {
        _onWSEvent(server, client, type, arg, data, len);
    });
    _server.addHandler(&_ws);

    _setupRoutes();

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    _server.begin();
    Serial.println("Web server started");
}

void RadarWebServer::_setupRoutes() {
    // Serve static files from LittleFS
    _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // API: Get current status
    _server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        doc["uptime"] = millis() / 1000;
        doc["heap"] = ESP.getFreeHeap();
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    // API: Update settings via POST
    _server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            request->send(200, "text/plain", "OK");
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len,
               size_t index, size_t total) {
            if (index == 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, data, len);
                if (err) {
                    Serial.printf("Settings JSON parse error: %s\n", err.c_str());
                    return;
                }
                if (_settingsCb && doc.containsKey("motionThreshold")) {
                    _settingsCb(
                        doc["motionThreshold"] | 15,
                        doc["presenceThreshold"] | 5,
                        doc["scanInterval"] | 100
                    );
                }
                if (_roomCb && doc.containsKey("roomWidth")) {
                    _roomCb(
                        doc["roomWidth"] | 5.0f,
                        doc["roomLength"] | 4.0f
                    );
                }
            }
        }
    );
}

void RadarWebServer::sendUpdate(const DetectionResult& result) {
    if (_ws.count() == 0) return;

    JsonDocument doc;
    doc["rssi"]           = result.rssi;
    doc["csiAvg"]         = result.csiAmplitudeAvg;
    doc["motionLevel"]    = result.motionLevel;
    doc["csiMotionLevel"] = result.csiMotionLevel;
    doc["presence"]       = result.presenceDetected;
    doc["motion"]         = result.motionDetected;
    doc["csiVar"]         = result.csiVariance;
    doc["rssiVar"]        = result.rssiVariance;
    doc["t"]              = millis();

    String json;
    serializeJson(doc, json);
    _ws.textAll(json);
}

void RadarWebServer::cleanup() {
    _ws.cleanupClients();
}

void RadarWebServer::_onWSEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                 AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WS client #%u connected from %s\n",
                          client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WS client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA: {
            AwsFrameInfo* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                _handleWSMessage(data, len);
            }
            break;
        }
        default:
            break;
    }
}

void RadarWebServer::_handleWSMessage(uint8_t* data, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) return;

    const char* action = doc["action"];
    if (!action) return;

    if (strcmp(action, "setSettings") == 0) {
        if (_settingsCb) {
            _settingsCb(
                doc["motionThreshold"] | 15,
                doc["presenceThreshold"] | 5,
                doc["scanInterval"] | 100
            );
        }
    } else if (strcmp(action, "setRoom") == 0) {
        if (_roomCb) {
            _roomCb(
                doc["roomWidth"] | 5.0f,
                doc["roomLength"] | 4.0f
            );
        }
    }
}
