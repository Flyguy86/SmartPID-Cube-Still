#ifndef PID_AUTOTUNE_H
#define PID_AUTOTUNE_H

#include <Arduino.h>

// ─── PID Autotune (Relay Method / Ziegler-Nichols) ──────────────────────────
// This implements a relay-based autotuner that oscillates the output between
// outputStep and 0, measures the ultimate period (Tu) and ultimate gain (Ku),
// then computes Kp, Ki, Kd via Ziegler-Nichols.

class PIDAutotuner {
public:
    enum State { IDLE, RUNNING, COMPLETE, FAILED };

    PIDAutotuner();

    /// Configure before starting
    void setOutputRange(double minOut, double maxOut);
    void setOutputStep(double step);
    void setNoiseBand(double band);
    void setLookbackSec(int sec);
    void setTargetSetpoint(double sp);
    void setMaxCycles(int cycles);

    /// Start the autotune process
    void start();

    /// Cancel a running autotune
    void cancel();

    /// Call every loop iteration with current temperature. Returns suggested output.
    double update(double currentTemp);

    /// Get state
    State getState() const { return _state; }
    bool isRunning() const { return _state == RUNNING; }
    bool isComplete() const { return _state == COMPLETE; }

    /// Get computed PID parameters (valid after COMPLETE)
    double getKp() const { return _kp; }
    double getKi() const { return _ki; }
    double getKd() const { return _kd; }

    /// Get diagnostics
    double getUltimateGain() const { return _ku; }
    double getUltimatePeriod() const { return _tu; }
    int getCycleCount() const { return _cycleCount; }

private:
    State _state;

    // Config
    double _outputMin, _outputMax;
    double _outputStep;
    double _noiseBand;
    int    _lookbackSamples;
    double _setpoint;
    int    _maxCycles;

    // Runtime
    bool     _outputHigh;
    double   _peakHigh, _peakLow;
    unsigned long _lastPeakTime;
    unsigned long _peakTimestamps[20];
    double   _peakValues[20];
    int      _peakIndex;
    int      _cycleCount;
    bool     _justChanged;
    unsigned long _lastUpdateTime;
    int      _sampleInterval;

    // Rolling history for peak detection
    static const int HISTORY_SIZE = 100;
    double _history[HISTORY_SIZE];
    int    _historyIdx;
    int    _historyCount;

    // Results
    double _ku, _tu;
    double _kp, _ki, _kd;

    void _detectPeaks(double input);
    bool _checkCrossing(double input);
};

#endif // PID_AUTOTUNE_H
