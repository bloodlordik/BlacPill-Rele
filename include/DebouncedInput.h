#pragma once
#include <Arduino.h>
#include "Config.h"

/// @brief Отвечает за чтение любых цифровых входов с защитой от дребезга
class DebouncedInput {
public:
    DebouncedInput(uint8_t pin, uint8_t mode, bool activeLevel, uint32_t debounceMs = Config::DEBOUNCE_MS)
        : _pin(pin), _mode(mode), _activeLevel(activeLevel), _debounceMs(debounceMs) {}

    void begin() {
        pinMode(_pin, _mode);
        _stableState = readRawActive();
        _lastRawState = _stableState;
        _lastChangeTime = millis();
    }

    void update(uint32_t now) {
        _rose = false;
        _fell = false;

        bool raw = readRawActive();
        if (raw != _lastRawState) {
            _lastChangeTime = now;
            _lastRawState = raw;
        }

        if ((now - _lastChangeTime) >= _debounceMs && raw != _stableState) {
            _stableState = raw;
            if (_stableState) _rose = true;
            else              _fell = true;
        }
    }

    bool isActive() const { return _stableState; }
    bool rose() const     { return _rose; }
    bool fell() const     { return _fell; }

private:
    uint8_t  _pin;
    uint8_t  _mode;
    bool     _activeLevel;
    uint32_t _debounceMs;
    bool     _stableState    = false;
    bool     _lastRawState   = false;
    bool     _rose           = false;
    bool     _fell           = false;
    uint32_t _lastChangeTime = 0;

    bool readRawActive() const {
        return (digitalRead(_pin) == (_activeLevel ? HIGH : LOW));
    }
};