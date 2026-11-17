#include <Arduino.h>

// ================= 1. НАСТРОЙКА ПИНОВ (Black Pill F401CC) =================
const uint8_t PIN_CAMERA_IN    = PA0;  // Вход сигнала от камеры (подача 3.3V)
const uint8_t PIN_TA12_ADC     = PA1;  // Вход датчика тока TA12-200 (SIG -> PA1, GND -> G)

// Концевики положения ворот (подключаются между пином и GND):
const uint8_t PIN_LIMIT_OPEN   = PA2;  // Концевик ОТКРЫТО (NC: замкнут на GND, при упоре РАЗМЫКАЕТСЯ)
const uint8_t PIN_LIMIT_CLOSE  = PA3;  // Концевик ЗАКРЫТО (NC: замкнут на GND, при упоре РАЗМЫКАЕТСЯ)

const uint8_t PIN_OUT_OP       = PB0;  // Выход PB0 (OP: Открытие/Старт)
const uint8_t PIN_OUT_PH       = PB1;  // Выход PB1 (PH: Оптическая линия)

const uint8_t PIN_LED_OP       = PA6;  // Светодиод индикации PB0
const uint8_t PIN_LED_PH       = PA7;  // Светодиод индикации PB1
const uint8_t PIN_BOARD_LED    = PC13; // Встроенный светодиод платы (Active LOW: LOW = горит)

// ================= 2. НАСТРОЙКА РЕЛЕ И ВРЕМЕНИ =================
// Если модуль реле управляется нулем (Active LOW) — ставим false
const bool RELAY_ACTIVE_HIGH   = false; 

const uint32_t DEBOUNCE_MS     = 25;    // Фильтр дребезга камеры и концевиков (25 мс)
const uint32_t DEAD_TIME_MS    = 5;     // Защитная пауза переключения реле (5 мс)
const uint32_t LOCK_TIME_MS    = 25000; // Базовое максимальное время хода ворот (25 сек)

// Параметры датчика тока TA12-200
const float V_REF              = 3.3f;
const float ADC_MAX_VALUE      = 4095.0f;
const float R_LOAD             = 800.0f;
const float TURNS_RATIO        = 2000.0f;
const float WIRE_TURNS         = 1.0f;

const float MOTOR_CURRENT_THRESHOLD_A = 0.15f; // Порог обнаружения тока двигателя
const uint32_t CURRENT_WINDOW_MS      = 100;

// ================= 3. ПЕРЕМЕННЫЕ СОСТОЯНИЯ =================
const uint8_t RELAY_ON  = RELAY_ACTIVE_HIGH ? HIGH : LOW;
const uint8_t RELAY_OFF = RELAY_ACTIVE_HIGH ? LOW  : HIGH;

uint32_t adcWindowStart       = 0;
int currentWindowMaxADC       = 0;
float latestMotorCurrentRMS   = 0.0f;
bool isMotorRunning           = false;

bool lastRawCamera            = false;
bool stableCameraState        = false;
uint32_t lastCameraDebounce   = 0;

bool isLocked                 = false;
uint32_t lockStartTime        = 0;

// Переменные концевика открытия (NC: LOW = замкнут/норма, HIGH = разомкнут/сработал)
bool lastRawLimitOpen         = false;
bool stableLimitOpen          = false;
uint32_t lastLimitDebounce     = 0;

// --- Состояние по умолчанию: обе линии PB0 и PB1 отключены ---
void setDefaultState() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF);
    digitalWrite(PIN_LED_OP, LOW);
    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_OFF);
    digitalWrite(PIN_LED_PH, LOW);
}

// --- Сценарий 1: ТОКА НЕТ (ворота стоят) -> Подаем сигнал на PB0 (OP) ---
void triggerActionGateIdle() {
    digitalWrite(PIN_OUT_PH, RELAY_OFF);
    digitalWrite(PIN_LED_PH, LOW);
    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_OP, RELAY_ON);
    digitalWrite(PIN_LED_OP, HIGH);
}

// --- Сценарий 2: ТОК ЕСТЬ (ворота уже в пути) -> Подаем сигнал на PB1 (PH) ---
void triggerActionGateMoving() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF);
    digitalWrite(PIN_LED_OP, LOW);
    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_ON);
    digitalWrite(PIN_LED_PH, HIGH);
}

// --- Безопасный замер тока на PA1 с подавлением наводок ---
int readTa12Adc() {
    pinMode(PIN_TA12_ADC, OUTPUT);
    digitalWrite(PIN_TA12_ADC, LOW);
    delayMicroseconds(5);

    pinMode(PIN_TA12_ADC, INPUT_ANALOG);
    delayMicroseconds(30);

    return analogRead(PIN_TA12_ADC);
}

void updateCurrentSensor() {
    int adcVal = readTa12Adc();
    if (adcVal > currentWindowMaxADC) {
        currentWindowMaxADC = adcVal;
    }

    uint32_t now = millis();
    if ((now - adcWindowStart) >= CURRENT_WINDOW_MS) {
        float iMax = ((float)currentWindowMaxADC / ADC_MAX_VALUE) * (V_REF / R_LOAD) * TURNS_RATIO;
        latestMotorCurrentRMS = (iMax / 1.4142f) / WIRE_TURNS;

        if (latestMotorCurrentRMS < 0.05f) {
            latestMotorCurrentRMS = 0.0f;
        }

        isMotorRunning = (latestMotorCurrentRMS >= MOTOR_CURRENT_THRESHOLD_A);

        currentWindowMaxADC = 0;
        adcWindowStart = now;
    }
}

void setup() {
    analogReadResolution(12);

    pinMode(PIN_CAMERA_IN, INPUT_PULLDOWN);
    pinMode(PIN_TA12_ADC, INPUT_ANALOG);

    // Входы концевиков с внутренней подтяжкой к 3.3V
    // Пока концевик замкнут на GND — там LOW. Когда разомкнулся — становится HIGH.
    pinMode(PIN_LIMIT_OPEN, INPUT_PULLUP);
    pinMode(PIN_LIMIT_CLOSE, INPUT_PULLUP);

    // Режим OUTPUT_OPEN_DRAIN защитит от залипания реле при питании от 5V
    pinMode(PIN_OUT_OP, OUTPUT_OPEN_DRAIN);
    pinMode(PIN_OUT_PH, OUTPUT_OPEN_DRAIN);

    pinMode(PIN_LED_OP, OUTPUT);
    pinMode(PIN_LED_PH, OUTPUT);
    pinMode(PIN_BOARD_LED, OUTPUT);

    setDefaultState();
    digitalWrite(PIN_BOARD_LED, HIGH);

    // Стартовый замер тока
    uint32_t startup = millis();
    adcWindowStart = startup;
    while ((millis() - startup) < 120) {
        updateCurrentSensor();
    }

    lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
    stableCameraState = lastRawCamera;

    stableLimitOpen = (digitalRead(PIN_LIMIT_OPEN) == HIGH);
    lastRawLimitOpen = stableLimitOpen;
}

void loop() {
    uint32_t now = millis();

    // 1. Фоновое обновление датчика тока
    updateCurrentSensor();

    // 2. Чтение концевика открытого положения (PA2) с антидребезгом
    bool rawLimitOpen = (digitalRead(PIN_LIMIT_OPEN) == HIGH); // true = контакт разомкнут (ворота дошли)
    if (rawLimitOpen != lastRawLimitOpen) {
        lastLimitDebounce = now;
        lastRawLimitOpen = rawLimitOpen;
    }
    if ((now - lastLimitDebounce) >= DEBOUNCE_MS) {
        stableLimitOpen = rawLimitOpen;
    }

    // 3. Обработка блокировки и контроль времени движения
    if (isLocked) {
        // УСЛОВИЕ ДОСРОЧНОГО СБРОСА:
        // Если сработал концевик открытия (разомкнулся -> HIGH) — сбрасываем таймер немедленно!
        if (stableLimitOpen) {
            isLocked = false;
            setDefaultState();
            digitalWrite(PIN_BOARD_LED, HIGH);

            // Синхронизируем состояние входа камеры
            lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
            stableCameraState = lastRawCamera;
            lastCameraDebounce = now;
            return;
        }

        // Если концевик еще не сработал — проверяем предельный таймер 25 секунд
        if ((now - lockStartTime) < LOCK_TIME_MS) {
            bool blink = ((now / 200) % 2) == 0;
            digitalWrite(PIN_BOARD_LED, blink ? LOW : HIGH);
            return; // Вход камеры пока заблокирован
        } else {
            // 25 секунд истекли по таймауту (например, если концевик не сработал)
            isLocked = false;
            setDefaultState();

            lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
            stableCameraState = lastRawCamera;
            lastCameraDebounce = now;
        }
    }

    // В дежурном режиме светодиод PC13 горит только при наличии тока на моторе
    digitalWrite(PIN_BOARD_LED, isMotorRunning ? LOW : HIGH);

    // 4. Опрос сигнала от камеры на PA0
    bool rawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
    if (rawCamera != lastRawCamera) {
        lastCameraDebounce = now;
        lastRawCamera = rawCamera;
    }

    if ((now - lastCameraDebounce) >= DEBOUNCE_MS) {
        if (rawCamera != stableCameraState) {
            stableCameraState = rawCamera;

            if (stableCameraState) {
                // Если ворота уже полностью открыты (концевик PA2 разомкнут),
                // не пытаемся подавать команду на открытие повторно
                if (!stableLimitOpen) {
                    if (!isMotorRunning) {
                        triggerActionGateIdle();
                    } else {
                        triggerActionGateMoving();
                    }

                    isLocked = true;
                    lockStartTime = now;
                }
            }
        }
    }
}