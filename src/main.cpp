#include <Arduino.h>

// --- НАСТРОЙКА ПИНЫ (Header 1 на WeAct Black Pill V1.2) ---
const uint8_t PIN_SIGNAL_IN  = PA0;  // Вход управляющего сигнала

const uint8_t PIN_OUT_1      = PB0;  // Выход 1 (активен, когда сигнал ЕСТЬ)
const uint8_t PIN_OUT_2      = PB1;  // Выход 2 (активен, когда сигнала НЕТ)

const uint8_t PIN_LED_OUT_1  = PA6;  // Внешний светодиод Выхода 1
const uint8_t PIN_LED_OUT_2  = PA7;  // Внешний светодиод Выхода 2
const uint8_t PIN_BOARD_LED  = PC13; // Встроенный светодиод на плате (Active LOW)

// --- НАСТРОЙКА ЛОГИКИ И ТАЙМИНГОВ ---
// Поставьте false, если ваш модуль реле из набора Arduino включается низким уровнем (LOW)
const bool RELAY_ACTIVE_HIGH = true; 

const uint32_t DEBOUNCE_MS   = 25;   // Фильтр дребезга входного сигнала (25 мс)
const uint32_t DEAD_TIME_MS  = 5;    // Защитная пауза между переключением каналов (5 мс)

// Вспомогательные уровни для управления выходами
const uint8_t RELAY_ON  = RELAY_ACTIVE_HIGH ? HIGH : LOW;
const uint8_t RELAY_OFF = RELAY_ACTIVE_HIGH ? LOW  : HIGH;

// Переменные состояния
bool currentSignalState = false;
bool lastRawSignal      = false;
uint32_t lastDebounceTime = 0;

// Функция безопасного переключения каналов и светодиодов
void switchChannels(bool signalPresent) {
    // Шаг 1: Break-Before-Make — сначала гарантированно отключаем ОБА выхода и гасим оба светодиода
    digitalWrite(PIN_OUT_1, RELAY_OFF);
    digitalWrite(PIN_OUT_2, RELAY_OFF);
    digitalWrite(PIN_LED_OUT_1, LOW);
    digitalWrite(PIN_LED_OUT_2, LOW);
    
    // Ждём защитную паузу, чтобы контакты реле/транзисторы первого канала успели закрыться
    delay(DEAD_TIME_MS);

    // Шаг 2: Включаем нужный канал и его светодиод
    if (signalPresent) {
        // Сигнал ЕСТЬ -> работает Выход 1 и светится Светодиод 1
        digitalWrite(PIN_OUT_1, RELAY_ON);
        digitalWrite(PIN_LED_OUT_1, HIGH);
        digitalWrite(PIN_BOARD_LED, LOW);  // На PC13 LOW = светодиод включен
    } else {
        // Сигнала НЕТ -> работает Выход 2 и светится Светодиод 2
        digitalWrite(PIN_OUT_2, RELAY_ON);
        digitalWrite(PIN_LED_OUT_2, HIGH);
        digitalWrite(PIN_BOARD_LED, HIGH); // На PC13 HIGH = светодиод выключен
    }
}

void setup() {
    // Настраиваем вход с внутренней подтяжкой к земле (GND)
    pinMode(PIN_SIGNAL_IN, INPUT_PULLDOWN);

    // Настраиваем силовые выходы
    pinMode(PIN_OUT_1, OUTPUT);
    pinMode(PIN_OUT_2, OUTPUT);

    // Настраиваем светодиоды индикации
    pinMode(PIN_LED_OUT_1, OUTPUT);
    pinMode(PIN_LED_OUT_2, OUTPUT);
    pinMode(PIN_BOARD_LED, OUTPUT);

    // Считываем стартовое состояние при подаче питания и сразу выставляем нужный выход
    currentSignalState = (digitalRead(PIN_SIGNAL_IN) == HIGH);
    lastRawSignal      = currentSignalState;
    switchChannels(currentSignalState);
}

void loop() {
    // Читаем текущее состояние на пине PA0
    bool rawSignal = (digitalRead(PIN_SIGNAL_IN) == HIGH);

    // Если уровень сигнала изменился (начался переход или дребезг), сбрасываем таймер
    if (rawSignal != lastRawSignal) {
        lastDebounceTime = millis();
        lastRawSignal = rawSignal;
    }

    // Если сигнал держится стабильно дольше DEBOUNCE_MS и отличается от включенного канала
    if ((millis() - lastDebounceTime) >= DEBOUNCE_MS) {
        if (rawSignal != currentSignalState) {
            currentSignalState = rawSignal;
            switchChannels(currentSignalState);
        }
    }
}