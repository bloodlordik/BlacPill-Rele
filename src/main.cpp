#include <Arduino.h>
#include "Config.h"
#include "DebouncedInput.h"
#include "RelayChannel.h"
#include "Ta12CurrentSensor.h"

enum class GateState {
    IDLE,
    ACTION_OPEN_IDLE,
    ACTION_PH_MOVING,
    PASS_RELAY_ON,      // Проход человека: реле пропуска включено на 2 сек
    PASS_WAIT_DELAY,    // Проход человека: пауза 14 сек перед закрытием
    PASS_CLOSE_ON       // Проход человека: импульс реле закрытия двери (2 сек)
};

// Инициализация входов
DebouncedInput    camInput(Config::PIN_CAMERA_IN,    INPUT_PULLDOWN, true);  // HIGH = сигнал с камеры
DebouncedInput    limitOpen(Config::PIN_LIMIT_OPEN,  INPUT_PULLUP,   true);  // HIGH = разомкнут (сработал)
DebouncedInput    limitClose(Config::PIN_LIMIT_CLOSE, INPUT_PULLUP,  true);  // HIGH = разомкнут (сработал), false = замкнут
DebouncedInput    btnReset(Config::PIN_RESET_BUTTON, INPUT_PULLUP,   false); // LOW  = кнопка нажата
DebouncedInput    doorInput(Config::PIN_DOOR_IN,     INPUT_PULLUP,   false); // LOW  = контакт PERCo замкнут на GND (NO)

Ta12CurrentSensor motorSensor(Config::PIN_TA12_ADC);

// Основные реле ворот (Open Drain)
RelayChannel      relayOp(Config::PIN_OUT_OP, Config::PIN_LED_OP, Config::RELAY_ACTIVE_HIGH, OUTPUT_OPEN_DRAIN);
RelayChannel      relayPh(Config::PIN_OUT_PH, Config::PIN_LED_PH, Config::RELAY_ACTIVE_HIGH, OUTPUT_OPEN_DRAIN);

// Реле режима прохода человека (Open Drain)
RelayChannel      relayPass(Config::PIN_OUT_PASS, RelayChannel::NO_LED, Config::RELAY_ACTIVE_HIGH, OUTPUT_OPEN_DRAIN);
RelayChannel      relayCloseDoor(Config::PIN_OUT_CLOSE_DR, RelayChannel::NO_LED, Config::RELAY_ACTIVE_HIGH, OUTPUT_OPEN_DRAIN);

GateState currentState   = GateState::IDLE;
uint32_t  stateStartTime = 0;

void stopPassRelays() {
    relayPass.off();
    relayCloseDoor.off();
}

void enterIdleState() {
    relayOp.off();
    stopPassRelays();
    delay(Config::DEAD_TIME_MS);
    relayPh.off();
    currentState = GateState::IDLE;
}

void enterActionOpenState(uint32_t now) {
    stopPassRelays();
    relayPh.off();
    delay(Config::DEAD_TIME_MS);
    relayOp.on();
    currentState   = GateState::ACTION_OPEN_IDLE;
    stateStartTime = now;
}

void enterActionPhState(uint32_t now) {
    stopPassRelays();
    relayOp.off();
    delay(Config::DEAD_TIME_MS);
    relayPh.on();
    currentState   = GateState::ACTION_PH_MOVING;
    stateStartTime = now;
}

void startHumanPass(uint32_t now) {
    relayCloseDoor.off();
    delay(Config::DEAD_TIME_MS);
    relayPass.on();
    currentState   = GateState::PASS_RELAY_ON;
    stateStartTime = now;
}

void updateBoardLed(uint32_t now) {
    if (currentState != GateState::IDLE) {
        bool blink = ((now / Config::BLINK_INTERVAL_MS) % 2) == 0;
        digitalWrite(Config::PIN_BOARD_LED, blink ? LOW : HIGH);
    } else {
        digitalWrite(Config::PIN_BOARD_LED, motorSensor.isMotorRunning() ? LOW : HIGH);
    }
}

void setup() {
    analogReadResolution(12);
    pinMode(Config::PIN_BOARD_LED, OUTPUT);
    digitalWrite(Config::PIN_BOARD_LED, HIGH);

    relayOp.begin();
    relayPh.begin();
    relayPass.begin();
    relayCloseDoor.begin();

    camInput.begin();
    limitOpen.begin();
    limitClose.begin();
    btnReset.begin();
    doorInput.begin();
    motorSensor.begin();

    enterIdleState();
}

void loop() {
    uint32_t now = millis();

    // 1. Опрос всех датчиков, концевиков и кнопок
    motorSensor.update(now);
    camInput.update(now);
    limitOpen.update(now);
    limitClose.update(now);
    btnReset.update(now);
    doorInput.update(now);

    // 2. ВЫСШИЙ ПРИОРИТЕТ: сигнал от камеры (PA0)
    // Срабатывает в IDLE и мгновенно прерывает любой этап прохода человека
    bool isIdleOrHumanPass = (currentState == GateState::IDLE ||
                              currentState == GateState::PASS_RELAY_ON ||
                              currentState == GateState::PASS_WAIT_DELAY ||
                              currentState == GateState::PASS_CLOSE_ON);

    if (isIdleOrHumanPass && camInput.rose() && !limitOpen.isActive()) {
        if (!motorSensor.isMotorRunning()) {
            enterActionOpenState(now);
        } else {
            enterActionPhState(now);
        }
        updateBoardLed(now);
        return;
    }

    // 3. Конечный автомат управления воротами и проходом человека
    switch (currentState) {
        case GateState::IDLE: {
            // Концевик закрытия замкнут (!limitClose.isActive()) + замыкание контакта PERCo (doorInput)
            bool isGateClosed = !limitClose.isActive();
            if (doorInput.rose() && isGateClosed) {
                startHumanPass(now);
            }
            break;
        }

        case GateState::PASS_RELAY_ON:
            if (btnReset.isActive()) {
                enterIdleState();
            } else if ((now - stateStartTime) >= Config::PASS_OPEN_MS) {
                // Прошло 2 сек -> выключаем реле прохода, начинаем отсчёт 14 сек
                relayPass.off();
                currentState   = GateState::PASS_WAIT_DELAY;
                stateStartTime = now;
            }
            break;

        case GateState::PASS_WAIT_DELAY:
            if (btnReset.isActive()) {
                enterIdleState();
            } else if ((now - stateStartTime) >= Config::PASS_WAIT_CLOSE_MS) {
                // Прошло 14 сек -> включаем реле закрытия двери
                relayCloseDoor.on();
                currentState   = GateState::PASS_CLOSE_ON;
                stateStartTime = now;
            }
            break;

        case GateState::PASS_CLOSE_ON:
            if (btnReset.isActive() || (now - stateStartTime) >= Config::PASS_CLOSE_MS) {
                // Завершили импульс закрытия двери -> возврат в дежурный режим
                enterIdleState();
            }
            break;

        case GateState::ACTION_OPEN_IDLE:
        case GateState::ACTION_PH_MOVING: {
            bool timeoutExpired = (now - stateStartTime) >= Config::LOCK_TIME_MS;

            // Досрочный выход по концевику открытия, кнопке сброса (PA4) или таймауту 25 сек
            if (limitOpen.isActive() || btnReset.isActive() || timeoutExpired) {
                enterIdleState();
            }
            break;
        }
    }

    // 4. Обновление светодиода PC13
    updateBoardLed(now);
}