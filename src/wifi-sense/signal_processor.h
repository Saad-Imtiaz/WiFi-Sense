#ifndef SIGNAL_PROCESSOR_H
#define SIGNAL_PROCESSOR_H

#include <Arduino.h>

// --- Configuration ---
#define SP_MAX_BUFFER_SIZE    256
#define SP_MAX_AVG_BUF_SIZE   64
#define SP_MAX_VARIANCE       65535
#define SP_ABSOLUTE_RSSI_MIN  -100
#define SP_CSI_SUBCARRIERS    64   // max subcarriers we track

// --- Detection states ---
#define DETECT_NONE       0
#define DETECT_PRESENCE   1
#define DETECT_MOTION     2

// --- Error codes ---
#define SP_OK                 0
#define SP_ERR_UNINITIALIZED -1
#define SP_ERR_BOOTING       -2
#define SP_ERR_RSSI_LOW      -3

// Holds the result of one processing cycle
struct DetectionResult {
    int   rssi;               // raw RSSI value
    float csiAmplitudeAvg;    // average CSI amplitude across subcarriers
    int   motionLevel;        // 0 = none, higher = more motion
    int   csiMotionLevel;     // CSI-based motion level (more sensitive)
    bool  presenceDetected;   // true if someone is in the room
    bool  motionDetected;     // true if active movement
    float csiVariance;        // raw CSI variance for diagnostics
    float rssiVariance;       // raw RSSI variance for diagnostics
};

class SignalProcessor {
public:
    SignalProcessor();
    ~SignalProcessor();

    // Initialize buffers. Call once in setup().
    bool init(int sampleBufSize = 64, int avgFilterSize = 16,
              int varThreshold = 3, int varIntegratorLimit = 3);

    // Free buffers
    void deinit();

    // Process one RSSI sample, returns motion level (< 0 = error)
    int processRSSI(int rssiSample);

    // Process CSI amplitude array (one value per subcarrier)
    // numSubcarriers = number of valid entries in amplitudes[]
    int processCSI(const float* amplitudes, int numSubcarriers);

    // Get combined detection result after processing both RSSI and CSI
    DetectionResult getResult();

    // Configuration
    void setMotionThreshold(int threshold);
    void setPresenceThreshold(int threshold);
    void setMinRSSI(int minRssi);

    int getMotionThreshold() const { return _motionThreshold; }
    int getPresenceThreshold() const { return _presenceThreshold; }

private:
    // --- RSSI processing ---
    int* _sampleBuf;
    int  _sampleBufSize;
    int  _sampleBufIdx;
    bool _sampleBufValid;

    int* _avgBuf;
    int  _avgBufSize;
    int  _avgFilterSize;
    int  _avgBufIdx;
    bool _avgBufValid;

    int* _varBuf;
    int  _varBufSize;
    int  _varBufIdx;
    bool _varBufValid;

    int  _varIntegratorLimit;
    int  _rssiVariance;
    int  _rssiMotionLevel;
    int  _minimumRSSI;

    // --- CSI processing ---
    float* _csiAvgBuf;       // circular buffer of CSI average amplitudes
    int    _csiBufSize;
    int    _csiBufIdx;
    bool   _csiBufValid;

    float* _csiVarBuf;       // circular buffer of CSI variance samples
    int    _csiVarBufIdx;
    bool   _csiVarBufValid;

    float  _csiMean;
    float  _csiVariance;
    int    _csiMotionLevel;
    float  _currentCsiAvg;

    // --- Detection ---
    int  _motionThreshold;
    int  _presenceThreshold;
    bool _presenceDetected;
    bool _motionDetected;
    int  _currentRSSI;

    // --- Smoothing (EMA) ---
    float _rssiMotionEma;       // exponential moving average of RSSI motion
    float _csiMotionEma;        // exponential moving average of CSI motion
    float _emaAlpha;            // EMA smoothing factor (0.05 = very smooth)

    // --- Hysteresis ---
    int   _presenceOnCount;     // consecutive samples above presence threshold
    int   _presenceOffCount;    // consecutive samples below presence threshold
    int   _motionOnCount;
    int   _motionOffCount;
    static const int HYSTERESIS_ON  = 5;   // samples needed to turn ON
    static const int HYSTERESIS_OFF = 15;  // samples needed to turn OFF (slower release)

    bool _initialized;

    // Helpers
    int  calcCircularAvg(const int* buf, int bufSize, int headIdx, int filterSize);
    float calcCircularAvgF(const float* buf, int bufSize, int headIdx, int filterSize);
};

#endif
