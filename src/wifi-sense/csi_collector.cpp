#include "csi_collector.h"
#include <math.h>
#include <string.h>

CSICollector* CSICollector::_instance = nullptr;

CSICollector::CSICollector()
    : _newDataReady(false), _lastRSSI(0), _running(false), _mutex(nullptr)
{
    memset(&_latestData, 0, sizeof(_latestData));
    _instance = this;
}

bool CSICollector::begin() {
    if (_running) return true;

    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) return false;

    // Configure CSI
    wifi_csi_config_t csi_config = {};
    csi_config.lltf_en           = true;   // Legacy Long Training Field
    csi_config.htltf_en          = true;   // HT-LTF
    csi_config.stbc_htltf2_en    = true;   // STBC HT-LTF2
    csi_config.ltf_merge_en      = true;   // Merge LTF data for cleaner output
    csi_config.channel_filter_en = false;  // Keep subcarrier independence
    csi_config.manu_scale        = false;  // Auto scaling

    esp_err_t err;

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        Serial.printf("CSI config failed: %s\n", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_csi_rx_cb(_csiCallback, this);
    if (err != ESP_OK) {
        Serial.printf("CSI callback failed: %s\n", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        Serial.printf("CSI enable failed: %s\n", esp_err_to_name(err));
        return false;
    }

    _running = true;
    Serial.println("CSI collection started");
    return true;
}

void CSICollector::end() {
    if (!_running) return;

    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);

    if (_mutex) {
        vSemaphoreDelete(_mutex);
        _mutex = nullptr;
    }

    _running = false;
}

bool CSICollector::available() {
    return _newDataReady;
}

bool CSICollector::read(CSIData& out) {
    if (!_newDataReady || !_mutex) return false;

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&out, &_latestData, sizeof(CSIData));
        _newDataReady = false;
        xSemaphoreGive(_mutex);
        return true;
    }
    return false;
}

void CSICollector::_csiCallback(void* ctx, wifi_csi_info_t* info) {
    if (_instance) {
        _instance->_handleCSI(info);
    }
}

void CSICollector::_handleCSI(wifi_csi_info_t* info) {
    if (!info || !info->buf || info->len == 0) return;

    // CSI data is I/Q pairs stored as int8_t: [I0, Q0, I1, Q1, ...]
    int8_t* buf = info->buf;
    int len = info->len;
    int numPairs = len / 2;

    if (numPairs > CSI_MAX_SUBCARRIERS) numPairs = CSI_MAX_SUBCARRIERS;

    // Skip first 4 bytes if marked invalid
    int startIdx = 0;
    if (info->first_word_invalid) {
        startIdx = 2; // skip 2 I/Q pairs (4 bytes)
    }

    CSIData data;
    memcpy(data.mac, info->mac, 6);
    data.rssi = info->rx_ctrl.rssi;
    data.timestamp = millis();

    // Extract amplitude = sqrt(I^2 + Q^2) for each subcarrier
    float sum = 0;
    int validCount = 0;
    for (int i = startIdx; i < numPairs; i++) {
        float imaginary = (float)buf[i * 2];
        float real      = (float)buf[i * 2 + 1];
        float amplitude = sqrtf(imaginary * imaginary + real * real);
        data.amplitudes[validCount] = amplitude;
        sum += amplitude;
        validCount++;
    }

    data.numSubcarriers = validCount;
    data.amplitudeAvg = (validCount > 0) ? (sum / validCount) : 0;

    // Thread-safe update
    if (_mutex && xSemaphoreTake(_mutex, 0) == pdTRUE) {
        memcpy(&_latestData, &data, sizeof(CSIData));
        _lastRSSI = data.rssi;
        _newDataReady = true;
        xSemaphoreGive(_mutex);
    }
}
