#include "pid_autotune.h"
#include <math.h>

PIDAutotuner::PIDAutotuner() {
    _state = IDLE;
    _outputMin = 0;
    _outputMax = 255;
    _outputStep = 127;
    _noiseBand = 0.5;
    _lookbackSamples = 30;
    _setpoint = 78.0;
    _maxCycles = 10;
    _sampleInterval = 1000; // 1 sec default
    _ku = 0; _tu = 0;
    _kp = 0; _ki = 0; _kd = 0;
    _cycleCount = 0;
    _historyIdx = 0;
    _historyCount = 0;
    _peakIndex = 0;
}

void PIDAutotuner::setOutputRange(double minOut, double maxOut) {
    _outputMin = minOut;
    _outputMax = maxOut;
}

void PIDAutotuner::setOutputStep(double step) { _outputStep = step; }
void PIDAutotuner::setNoiseBand(double band) { _noiseBand = band; }

void PIDAutotuner::setLookbackSec(int sec) {
    _lookbackSamples = max(1, sec * 1000 / _sampleInterval);
}

void PIDAutotuner::setTargetSetpoint(double sp) { _setpoint = sp; }
void PIDAutotuner::setMaxCycles(int cycles) { _maxCycles = cycles; }

void PIDAutotuner::start() {
    _state = RUNNING;
    _cycleCount = 0;
    _peakIndex = 0;
    _historyIdx = 0;
    _historyCount = 0;
    _outputHigh = true;
    _justChanged = false;
    _peakHigh = -1e10;
    _peakLow = 1e10;
    _lastPeakTime = millis();
    _lastUpdateTime = millis();
    _ku = 0; _tu = 0;
    _kp = 0; _ki = 0; _kd = 0;
}

void PIDAutotuner::cancel() {
    _state = IDLE;
}

double PIDAutotuner::update(double currentTemp) {
    if (_state != RUNNING) return 0;

    unsigned long now = millis();
    if ((now - _lastUpdateTime) < (unsigned long)_sampleInterval) {
        return _outputHigh ? _outputStep : 0;
    }
    _lastUpdateTime = now;

    // Store in rolling history
    _history[_historyIdx] = currentTemp;
    _historyIdx = (_historyIdx + 1) % HISTORY_SIZE;
    if (_historyCount < HISTORY_SIZE) _historyCount++;

    // Track peaks
    if (currentTemp > _peakHigh) _peakHigh = currentTemp;
    if (currentTemp < _peakLow)  _peakLow = currentTemp;

    // Relay switching logic with noise band
    if (_outputHigh && currentTemp > _setpoint + _noiseBand) {
        // Switch output LOW
        _outputHigh = false;
        _justChanged = true;

        // Record peak timestamp
        if (_peakIndex < 20) {
            _peakTimestamps[_peakIndex] = now;
            _peakValues[_peakIndex] = _peakHigh;
            _peakIndex++;
        }
        _peakHigh = currentTemp;
        _peakLow = currentTemp;

    } else if (!_outputHigh && currentTemp < _setpoint - _noiseBand) {
        // Switch output HIGH
        _outputHigh = true;
        _justChanged = true;

        // Record trough timestamp
        if (_peakIndex < 20) {
            _peakTimestamps[_peakIndex] = now;
            _peakValues[_peakIndex] = _peakLow;
            _peakIndex++;
        }

        _cycleCount++;
        _peakHigh = currentTemp;
        _peakLow = currentTemp;
    } else {
        _justChanged = false;
    }

    // Check if we have enough cycles to compute
    if (_cycleCount >= _maxCycles && _peakIndex >= 4) {
        // Compute ultimate period (average of full cycles)
        double totalPeriod = 0;
        int periodCount = 0;
        for (int i = 2; i < _peakIndex; i += 2) {
            totalPeriod += (_peakTimestamps[i] - _peakTimestamps[i - 2]);
            periodCount++;
        }

        if (periodCount > 0) {
            _tu = (totalPeriod / periodCount) / 1000.0; // Convert to seconds

            // Compute amplitude (average peak-to-trough)
            double totalAmplitude = 0;
            int ampCount = 0;
            for (int i = 1; i < _peakIndex; i += 2) {
                if (i + 1 < _peakIndex) {
                    // peak[i-1] is a high, peak[i] is a low (or vice versa)
                    double amp = fabs(_peakValues[i - 1] - _peakValues[i]);
                    totalAmplitude += amp;
                    ampCount++;
                }
            }

            double amplitude = (ampCount > 0) ? totalAmplitude / ampCount : 1.0;

            // Ultimate gain: Ku = 4 * d / (π * a)
            // where d = output step, a = amplitude
            _ku = (4.0 * _outputStep) / (M_PI * amplitude);

            // Ziegler-Nichols PID tuning (classic)
            _kp = 0.6 * _ku;
            _ki = 2.0 * _kp / _tu;  // Ki = Kp / (Tu/2)
            _kd = _kp * _tu / 8.0;  // Kd = Kp * Tu / 8

            // Sanity check
            if (isnan(_kp) || isinf(_kp) || _kp <= 0) {
                _state = FAILED;
            } else {
                _state = COMPLETE;
            }
        } else {
            _state = FAILED;
        }
    }

    // Timeout: if running for more than 30 minutes, fail
    if (now - _peakTimestamps[0] > 1800000UL && _peakIndex > 0) {
        if (_state == RUNNING) _state = FAILED;
    }

    return _outputHigh ? _outputStep : 0;
}
