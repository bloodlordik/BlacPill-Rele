#pragma once
#include <Arduino.h>

/// @brief Управляет каналом реле (Open-Drain или Push-Pull) и опциональным светодиодом
class RelayChannel {
public:
    static constexpr uint8_t NO_LED = 0xFF;

    RelayChannel(uint8_t relayPin, uint8_t ledPin, bool activeHigh, uint8_t pinModeType = OUTPUT_OPEN_DRAIN)
        : _relayPin(relayPin), _ledPin(ledPin), _pinModeType(pinModeType),
          _onLevel(activeHigh ? HIGH : LOW),
          _offLevel(activeHigh ? LOW : HIGH) {}

    void begin() {
        pinMode(_relayPin, _pinModeType);
        if (_ledPin != NO_LED) {
            pinMode(_ledPin, OUTPUT);
        }
        off();
    }

    void on() {
        digitalWrite(_relayPin, _onLevel);
        if (_ledPin != NO_LED) {
            digitalWrite(_ledPin, HIGH);
        }
    }

    void off() {
        digitalWrite(_relayPin, _offLevel);
        if (_ledPin != NO_LED) {
            digitalWrite(_ledPin, LOW);
        }
    }

private:
    uint8_t _relayPin;
    uint8_t _ledPin;
    uint8_t _pinModeType;
    uint8_t _onLevel;
    uint8_t _offLevel;
};