#include "signal_processor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

SignalProcessor::SignalProcessor()
    : _sampleBuf(nullptr), _sampleBufSize(0), _sampleBufIdx(0), _sampleBufValid(false),
      _avgBuf(nullptr), _avgBufSize(SP_MAX_AVG_BUF_SIZE), _avgFilterSize(16), _avgBufIdx(0), _avgBufValid(false),
      _varBuf(nullptr), _varBufSize(0), _varBufIdx(0), _varBufValid(false),
      _varIntegratorLimit(3), _rssiVariance(0), _rssiMotionLevel(0), _minimumRSSI(SP_ABSOLUTE_RSSI_MIN),
      _csiAvgBuf(nullptr), _csiBufSize(0), _csiBufIdx(0), _csiBufValid(false),
      _csiVarBuf(nullptr), _csiVarBufIdx(0), _csiVarBufValid(false),
      _csiMean(0), _csiVariance(0), _csiMotionLevel(0), _currentCsiAvg(0),
      _motionThreshold(15), _presenceThreshold(5), _presenceDetected(false),
      _motionDetected(false), _currentRSSI(0),
      _rssiMotionEma(0), _csiMotionEma(0), _emaAlpha(0.08f),
      _presenceOnCount(0), _presenceOffCount(0), _motionOnCount(0), _motionOffCount(0),
      _initialized(false)
{
}

SignalProcessor::~SignalProcessor() {
    deinit();
}

bool SignalProcessor::init(int sampleBufSize, int avgFilterSize, int varThreshold, int varIntegratorLimit) {
    deinit();

    if (sampleBufSize > SP_MAX_BUFFER_SIZE) sampleBufSize = SP_MAX_BUFFER_SIZE;
    if (avgFilterSize > sampleBufSize) avgFilterSize = sampleBufSize;
    if (varIntegratorLimit > sampleBufSize) varIntegratorLimit = sampleBufSize;

    _sampleBufSize = sampleBufSize;
    _avgFilterSize = avgFilterSize;
    _varBufSize = sampleBufSize;
    _varIntegratorLimit = varIntegratorLimit;
    _motionThreshold = varThreshold;

    // Allocate RSSI buffers
    _sampleBuf = (int*)calloc(_sampleBufSize, sizeof(int));
    _avgBuf    = (int*)calloc(_avgBufSize, sizeof(int));
    _varBuf    = (int*)calloc(_varBufSize, sizeof(int));

    if (!_sampleBuf || !_avgBuf || !_varBuf) {
        deinit();
        return false;
    }

    // Allocate CSI buffers (same sizes as RSSI for consistency)
    _csiBufSize = sampleBufSize;
    _csiAvgBuf = (float*)calloc(_csiBufSize, sizeof(float));
    _csiVarBuf = (float*)calloc(_csiBufSize, sizeof(float));

    if (!_csiAvgBuf || !_csiVarBuf) {
        deinit();
        return false;
    }

    _sampleBufIdx = 0; _sampleBufValid = false;
    _avgBufIdx = 0;    _avgBufValid = false;
    _varBufIdx = 0;    _varBufValid = false;
    _csiBufIdx = 0;    _csiBufValid = false;
    _csiVarBufIdx = 0; _csiVarBufValid = false;

    _rssiVariance = -1;
    _csiVariance = 0;
    _initialized = true;

    return true;
}

void SignalProcessor::deinit() {
    if (_sampleBuf) { free(_sampleBuf); _sampleBuf = nullptr; }
    if (_avgBuf)    { free(_avgBuf);    _avgBuf = nullptr; }
    if (_varBuf)    { free(_varBuf);    _varBuf = nullptr; }
    if (_csiAvgBuf) { free(_csiAvgBuf); _csiAvgBuf = nullptr; }
    if (_csiVarBuf) { free(_csiVarBuf); _csiVarBuf = nullptr; }
    _initialized = false;
}

int SignalProcessor::processRSSI(int rssiSample) {
    if (!_initialized) return SP_ERR_UNINITIALIZED;
    if (rssiSample < _minimumRSSI) return SP_ERR_RSSI_LOW;

    _currentRSSI = rssiSample;

    // Store sample in circular buffer
    _sampleBuf[_sampleBufIdx] = rssiSample;
    _sampleBufIdx++;
    if (_sampleBufIdx >= _sampleBufSize) {
        _sampleBufIdx = 0;
        _sampleBufValid = true;
    }

    if (!_sampleBufValid) return SP_ERR_BOOTING;

    // Calculate mobile average
    int avg = calcCircularAvg(_sampleBuf, _sampleBufSize, _sampleBufIdx, _avgFilterSize);

    _avgBuf[_avgBufIdx] = avg;
    _avgBufIdx++;
    if (_avgBufIdx >= _avgBufSize) {
        _avgBufIdx = 0;
        _avgBufValid = true;
    }

    // Calculate variance sample (squared deviation from mean)
    int deviation = rssiSample - avg;
    int varianceSample = deviation * deviation;

    _varBuf[_varBufIdx] = varianceSample;
    _varBufIdx++;
    if (_varBufIdx >= _varBufSize) {
        _varBufIdx = 0;
        _varBufValid = true;
    }

    // Integrate recent variance samples
    int varianceIntegral = 0;
    for (int i = 0; i < _varIntegratorLimit; i++) {
        int idx = _varBufIdx - 1 - i;
        if (idx < 0) idx += _varBufSize;
        varianceIntegral += _varBuf[idx];
    }

    _rssiVariance = varianceIntegral;

    // Smooth with exponential moving average to remove spikes
    _rssiMotionEma = _rssiMotionEma * (1.0f - _emaAlpha) + (float)varianceIntegral * _emaAlpha;
    _rssiMotionLevel = (int)_rssiMotionEma;

    return _rssiMotionLevel;
}

int SignalProcessor::processCSI(const float* amplitudes, int numSubcarriers) {
    if (!_initialized) return SP_ERR_UNINITIALIZED;
    if (!amplitudes || numSubcarriers <= 0) return SP_ERR_UNINITIALIZED;

    // Calculate average amplitude across all subcarriers
    float sum = 0;
    for (int i = 0; i < numSubcarriers; i++) {
        sum += amplitudes[i];
    }
    _currentCsiAvg = sum / numSubcarriers;

    // Store in circular buffer
    _csiAvgBuf[_csiBufIdx] = _currentCsiAvg;
    _csiBufIdx++;
    if (_csiBufIdx >= _csiBufSize) {
        _csiBufIdx = 0;
        _csiBufValid = true;
    }

    if (!_csiBufValid) return SP_ERR_BOOTING;

    // Calculate mean of recent CSI averages
    _csiMean = calcCircularAvgF(_csiAvgBuf, _csiBufSize, _csiBufIdx, _avgFilterSize);

    // Calculate variance sample
    float dev = _currentCsiAvg - _csiMean;
    float varianceSample = dev * dev;

    _csiVarBuf[_csiVarBufIdx] = varianceSample;
    _csiVarBufIdx++;
    if (_csiVarBufIdx >= _csiBufSize) {
        _csiVarBufIdx = 0;
        _csiVarBufValid = true;
    }

    // Integrate recent variance samples
    float varianceIntegral = 0;
    for (int i = 0; i < _varIntegratorLimit; i++) {
        int idx = _csiVarBufIdx - 1 - i;
        if (idx < 0) idx += _csiBufSize;
        varianceIntegral += _csiVarBuf[idx];
    }

    _csiVariance = varianceIntegral;

    // Smooth with EMA
    float scaledCsi = varianceIntegral * 10.0f;
    _csiMotionEma = _csiMotionEma * (1.0f - _emaAlpha) + scaledCsi * _emaAlpha;
    _csiMotionLevel = (int)_csiMotionEma;

    return _csiMotionLevel;
}

DetectionResult SignalProcessor::getResult() {
    DetectionResult r;
    r.rssi = _currentRSSI;
    r.csiAmplitudeAvg = _currentCsiAvg;
    r.motionLevel = _rssiMotionLevel;
    r.csiMotionLevel = _csiMotionLevel;
    r.rssiVariance = (float)_rssiVariance;
    r.csiVariance = _csiVariance;

    // Use CSI as primary if available, fall back to RSSI
    int combinedLevel = (_csiMotionLevel > 0) ? _csiMotionLevel : _rssiMotionLevel;

    // Hysteresis: require multiple consecutive samples to change state
    // This prevents rapid toggling from noise
    bool rawPresence = (combinedLevel > _presenceThreshold);
    bool rawMotion = (combinedLevel > _motionThreshold);

    if (rawPresence) {
        _presenceOnCount++;
        _presenceOffCount = 0;
        if (_presenceOnCount >= HYSTERESIS_ON) _presenceDetected = true;
    } else {
        _presenceOffCount++;
        _presenceOnCount = 0;
        if (_presenceOffCount >= HYSTERESIS_OFF) _presenceDetected = false;
    }

    if (rawMotion) {
        _motionOnCount++;
        _motionOffCount = 0;
        if (_motionOnCount >= HYSTERESIS_ON) _motionDetected = true;
    } else {
        _motionOffCount++;
        _motionOnCount = 0;
        if (_motionOffCount >= HYSTERESIS_OFF) _motionDetected = false;
    }

    r.presenceDetected = _presenceDetected;
    r.motionDetected = _motionDetected;

    return r;
}

void SignalProcessor::setMotionThreshold(int threshold) {
    if (threshold >= 0 && threshold <= SP_MAX_VARIANCE)
        _motionThreshold = threshold;
}

void SignalProcessor::setPresenceThreshold(int threshold) {
    if (threshold >= 0 && threshold <= SP_MAX_VARIANCE)
        _presenceThreshold = threshold;
}

void SignalProcessor::setMinRSSI(int minRssi) {
    if (minRssi > 0 || minRssi < SP_ABSOLUTE_RSSI_MIN)
        minRssi = SP_ABSOLUTE_RSSI_MIN;
    _minimumRSSI = minRssi;
}

int SignalProcessor::calcCircularAvg(const int* buf, int bufSize, int headIdx, int filterSize) {
    long sum = 0;
    for (int i = 0; i < filterSize; i++) {
        int idx = headIdx - 1 - i;
        if (idx < 0) idx += bufSize;
        sum += buf[idx];
    }
    return (int)(sum / filterSize);
}

float SignalProcessor::calcCircularAvgF(const float* buf, int bufSize, int headIdx, int filterSize) {
    float sum = 0;
    for (int i = 0; i < filterSize; i++) {
        int idx = headIdx - 1 - i;
        if (idx < 0) idx += bufSize;
        sum += buf[idx];
    }
    return sum / filterSize;
}
