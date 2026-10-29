#include <Arduino.h>

// --- НАСТРОЙКА ПИНОВ (Header 1 на WeAct Black Pill V1.2) ---
const uint8_t PIN_SIGNAL_IN  = PA0;  // Вход управляющего сигнала

const uint8_t PIN_OUT_1      = PB0;  // Выход 1 (активен, когда сигнал ЕСТЬ)
const uint8_t PIN_OUT_2      = PB1;  // Выход 2 (активен, когда сигнала НЕТ)

const uint8_t PIN_LED_OUT_1  = PA6;  // Внешний светодиод Выхода 1
const uint8_t PIN_LED_OUT_2  = PA7;  // Внешний светодиод Выхода 2
const uint8_t PIN_BOARD_LED  = PC13; // Встроенный светодиод на плате (Active LOW)

// --- НАСТРОЙКА ЛОГИКИ И ТАЙМИНГОВ ---
// Поставьте false, если ваш модуль реле из набора Arduino включается низким уровнем (LOW)
const bool RELAY_ACTIVE_HIGH = true; 

const uint32_t DEBOUNCE_MS   = 25;    // Фильтр дребезга входного сигнала (25 мс)
const uint32_t DEAD_TIME_MS  = 5;     // Защитная пауза между переключением каналов (5 мс)
const uint32_t LOCK_TIME_MS  = 25000; // Минимальное время работы канала: 25 секунд (25 000 мс)

// Вспомогательные уровни для управления выходами
const uint8_t RELAY_ON  = RELAY_ACTIVE_HIGH ? HIGH : LOW;
const uint8_t RELAY_OFF = RELAY_ACTIVE_HIGH ? LOW  : HIGH;

// Переменные состояния
bool currentSignalState   = false; // Какой канал сейчас включен
bool lastRawSignal        = false; // Предыдущее сырое чтение для антидребезга
uint32_t lastDebounceTime = 0;

bool isLocked             = false; // Флаг 25-секундной блокировки
uint32_t lockStartTime    = 0;     // Время старта 25-секундного таймера

// Функция безопасного переключения каналов и запуска блокировки на 25 секунд
void switchChannels(bool signalPresent) {
    // Шаг 1: Break-Before-Make — отключаем ОБА выхода и гасим оба внешних светодиода
    digitalWrite(PIN_OUT_1, RELAY_OFF);
    digitalWrite(PIN_OUT_2, RELAY_OFF);
    digitalWrite(PIN_LED_OUT_1, LOW);
    digitalWrite(PIN_LED_OUT_2, LOW);
    
    // Ждём защитную паузу (5 мс)
    delay(DEAD_TIME_MS);

    // Шаг 2: Включаем нужный канал и его светодиод
    if (signalPresent) {
        // Сигнал ЕСТЬ -> работает Выход 1 (PB0) и светится Светодиод 1 (PA6)
        digitalWrite(PIN_OUT_1, RELAY_ON);
        digitalWrite(PIN_LED_OUT_1, HIGH);
    } else {
        // Сигнала НЕТ -> работает Выход 2 (PB1) и светится Светодиод 2 (PA7)
        digitalWrite(PIN_OUT_2, RELAY_ON);
        digitalWrite(PIN_LED_OUT_2, HIGH);
    }

    // Шаг 3: Включаем блокировку входа на 25 секунд
    isLocked = true;
    lockStartTime = millis();
}

void setup() {
    // Вход с внутренней подтяжкой к земле (GND)
    pinMode(PIN_SIGNAL_IN, INPUT_PULLDOWN);

    // Силовые выходы
    pinMode(PIN_OUT_1, OUTPUT);
    pinMode(PIN_OUT_2, OUTPUT);

    // Светодиоды индикации
    pinMode(PIN_LED_OUT_1, OUTPUT);
    pinMode(PIN_LED_OUT_2, OUTPUT);
    pinMode(PIN_BOARD_LED, OUTPUT);

    // Считываем состояние при подаче питания и сразу запускаем соответствующий канал на 25 сек
    currentSignalState = (digitalRead(PIN_SIGNAL_IN) == HIGH);
    lastRawSignal      = currentSignalState;
    switchChannels(currentSignalState);
}

void loop() {
    uint32_t now = millis();

    // 1. ОБРАБОТКА 25-СЕКУНДНОЙ БЛОКИРОВКИ
    if (isLocked) {
        if ((now - lockStartTime) < LOCK_TIME_MS) {
            // Пока идут 25 секунд:
            // - Синий светодиод на плате (PC13) быстро мигает (раз в 250 мс), сигнализируя о блокировке
            // - Любые изменения на входе PA0 полностью игнорируются (return)
            bool blink = ((now / 250) % 2) == 0;
            digitalWrite(PIN_BOARD_LED, blink ? LOW : HIGH);
            return; 
        } else {
            // 25 секунд прошли — снимаем блокировку!
            isLocked = false;
            // Переводим светодиод PC13 в штатный режим (горит при Выходе 1, погашен при Выходе 2)
            digitalWrite(PIN_BOARD_LED, currentSignalState ? LOW : HIGH);
            // Обновляем точку отсчёта антидребезга по фактическому состоянию пина PA0
            lastRawSignal = (digitalRead(PIN_SIGNAL_IN) == HIGH);
            lastDebounceTime = now;
        }
    }

    // 2. ОПРОС ВХОДА PA0 (работает только после окончания 25 секунд)
    bool rawSignal = (digitalRead(PIN_SIGNAL_IN) == HIGH);

    if (rawSignal != lastRawSignal) {
        lastDebounceTime = now;
        lastRawSignal = rawSignal;
    }

    if ((now - lastDebounceTime) >= DEBOUNCE_MS) {
        if (rawSignal != currentSignalState) {
            currentSignalState = rawSignal;
            switchChannels(currentSignalState);
        }
    }
}