#ifndef CSI_COLLECTOR_H
#define CSI_COLLECTOR_H

#include <Arduino.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#define CSI_MAX_SUBCARRIERS 64
#define CSI_BUF_QUEUE_SIZE  10

// Parsed CSI data from one received frame
struct CSIData {
    uint8_t  mac[6];           // source MAC
    int      rssi;             // RSSI from rx_ctrl
    float    amplitudes[CSI_MAX_SUBCARRIERS]; // amplitude per subcarrier
    int      numSubcarriers;   // how many valid subcarriers
    float    amplitudeAvg;     // average amplitude
    uint32_t timestamp;        // millis() when received
};

class CSICollector {
public:
    CSICollector();

    // Start CSI collection. Call after WiFi is connected.
    bool begin();

    // Stop CSI collection
    void end();

    // Check if new CSI data is available
    bool available();

    // Get the latest CSI data (copies into provided struct)
    bool read(CSIData& out);

    // Get raw RSSI from the last CSI frame
    int getLastRSSI() const { return _lastRSSI; }

private:
    static void _csiCallback(void* ctx, wifi_csi_info_t* info);
    static CSICollector* _instance;

    void _handleCSI(wifi_csi_info_t* info);

    CSIData  _latestData;
    volatile bool _newDataReady;
    int      _lastRSSI;
    bool     _running;

    // Mutex for thread-safe access since callback runs in WiFi task
    SemaphoreHandle_t _mutex;
};

#endif
