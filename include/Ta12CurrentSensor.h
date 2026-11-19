#pragma once
#include <Arduino.h>
#include "Config.h"

class Ta12CurrentSensor {
public:
    explicit Ta12CurrentSensor(uint8_t adcPin) : _pin(adcPin) {}

    void begin() {
        pinMode(_pin, INPUT_ANALOG);
        // Безопасный первичный прогрев АЦП без блокировки по millis()
        for (int i = 0; i < 200; ++i) {
            int val = analogRead(_pin);
            if (val > _maxAdc) {
                _maxAdc = val;
            }
            delayMicroseconds(500);
        }
        _windowStart = millis();
        calculateCurrent(_windowStart);
    }

    void update(uint32_t now) {
        int val = analogRead(_pin);
        if (val > _maxAdc) {
            _maxAdc = val;
        }

        if ((now - _windowStart) >= Config::CURRENT_WIN_MS) {
            calculateCurrent(now);
        }
    }

    bool  isMotorRunning() const { return _isRunning; }
    float getRmsCurrent()  const { return _rmsCurrent; }

private:
    uint8_t  _pin;
    uint32_t _windowStart = 0;
    int      _maxAdc      = 0;
    float    _rmsCurrent  = 0.0f;
    bool     _isRunning   = false;

    void calculateCurrent(uint32_t now) {
        constexpr float kScale = (Config::V_REF / Config::ADC_MAX) *
                                 (Config::TURNS_RATIO / Config::R_LOAD) /
                                 (1.4142f * Config::WIRE_TURNS);

        _rmsCurrent = static_cast<float>(_maxAdc) * kScale;
        if (_rmsCurrent < 0.05f) {
            _rmsCurrent = 0.0f;
        }

        if (!_isRunning && _rmsCurrent >= Config::CURRENT_ON_A) {
            _isRunning = true;
        } else if (_isRunning && _rmsCurrent <= Config::CURRENT_OFF_A) {
            _isRunning = false;
        }

        _maxAdc = 0;
        _windowStart = now;
    }
};