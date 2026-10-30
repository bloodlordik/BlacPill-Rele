#include <Arduino.h>

// ================= 1. НАСТРОЙКА ПИНОВ (Black Pill F401CC) =================
const uint8_t PIN_CAMERA_IN  = PA0;  // Вход сигнала от камеры
const uint8_t PIN_TA12_ADC   = PA1;  // Аналоговый вход датчика тока TA12-200

const uint8_t PIN_OUT_OP     = PB0;  // Линия OP (Открытие/Старт) - по умолчанию РАЗОМКНУТА
const uint8_t PIN_OUT_PH     = PB1;  // Линия PH (Оптическая)     - по умолчанию ЗАМКНУТА

const uint8_t PIN_LED_OP     = PA6;  // Светодиод индикации OP
const uint8_t PIN_LED_PH     = PA7;  // Светодиод индикации PH (горит, когда замкнута)
const uint8_t PIN_BOARD_LED  = PC13; // Встроенный светодиод платы (Active LOW)

// ================= 2. НАСТРОЙКА РЕЛЕ И ВРЕМЕНИ =================
const bool RELAY_ACTIVE_HIGH = true;  // Поставьте false, если реле включается низким уровнем (LOW)

const uint32_t DEBOUNCE_MS   = 25;    // Фильтр дребезга входа камеры (25 мс)
const uint32_t DEAD_TIME_MS  = 5;     // Защитная пауза переключения (5 мс)
const uint32_t LOCK_TIME_MS  = 25000; // Блокировка входа на 25 секунд

// Параметры датчика тока TA12-200
const float V_REF            = 3.3f;
const float ADC_MAX_VALUE    = 4095.0f;
const float R_LOAD           = 800.0f;
const float TURNS_RATIO      = 2000.0f;
const float WIRE_TURNS       = 1.0f;

// Порог определения работы мотора (0.15 А)
const float MOTOR_CURRENT_THRESHOLD_A = 0.15f;
const uint32_t CURRENT_WINDOW_MS      = 100;

// ================= 3. ПЕРЕМЕННЫЕ СОСТОЯНИЯ =================
const uint8_t RELAY_ON  = RELAY_ACTIVE_HIGH ? HIGH : LOW;
const uint8_t RELAY_OFF = RELAY_ACTIVE_HIGH ? LOW  : HIGH;

uint32_t adcWindowStart     = 0;
int currentWindowMaxADC     = 0;
int adcMinVal               = 4095;
float latestMotorCurrentRMS = 0.0f;
bool isMotorRunning         = false;

bool lastRawCamera          = false;
bool stableCameraState      = false;
uint32_t lastDebounceTime   = 0;

bool isLocked               = false;
uint32_t lockStartTime      = 0;

// --- СОСТОЯНИЕ ПО УМОЛЧАНИЮ: PH замкнута (горит A7), OP разомкнут (гашен A6) ---
void setDefaultState() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF);
    digitalWrite(PIN_LED_OP, LOW);       // OP выключен

    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_ON);
    digitalWrite(PIN_LED_PH, HIGH);      // PH всегда замкнута по умолчанию
}

// --- СЦЕНАРИЙ 1: ТОКА НЕТ (ворота стоят) -> Подаем сигнал на OP, PH остается замкнут ---
void triggerActionGateIdle() {
    digitalWrite(PIN_OUT_PH, RELAY_ON);  // PH остается замкнутым
    digitalWrite(PIN_LED_PH, HIGH);

    digitalWrite(PIN_OUT_OP, RELAY_ON);  // Подаем сигнал на OP
    digitalWrite(PIN_LED_OP, HIGH);
}

// --- СЦЕНАРИЙ 2: ТОК ЕСТЬ (ворота движутся) -> Разрываем PH, OP выключен ---
void triggerActionGateMoving() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF); // OP выключен
    digitalWrite(PIN_LED_OP, LOW);

    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_OFF); // Разрываем оптическую линию PH
    digitalWrite(PIN_LED_PH, LOW);       // Гасим светодиод PH
}

// --- Неблокирующий фоновый опрос датчика тока TA12-200 ---
void updateCurrentSensor() {
    int adcVal = analogRead(PIN_TA12_ADC);
    if (adcVal > currentWindowMaxADC) currentWindowMaxADC = adcVal;
    if (adcVal < adcMinVal)           adcMinVal = adcVal;

    uint32_t now = millis();
    if ((now - adcWindowStart) >= CURRENT_WINDOW_MS) {
        int peakToPeak = currentWindowMaxADC - adcMinVal;
        float iMax = ((float)peakToPeak / ADC_MAX_VALUE) * (V_REF / R_LOAD) * TURNS_RATIO;
        latestMotorCurrentRMS = (iMax / 1.4142f) / WIRE_TURNS;

        if (latestMotorCurrentRMS < 0.05f) {
            latestMotorCurrentRMS = 0.0f;
        }

        isMotorRunning = (latestMotorCurrentRMS >= MOTOR_CURRENT_THRESHOLD_A);

        currentWindowMaxADC = 0;
        adcMinVal = 4095;
        adcWindowStart = now;
    }
}

void setup() {
    analogReadResolution(12);

    pinMode(PIN_CAMERA_IN, INPUT_PULLDOWN);
    pinMode(PIN_TA12_ADC, INPUT_PULLDOWN);

    pinMode(PIN_OUT_OP, OUTPUT);
    pinMode(PIN_OUT_PH, OUTPUT);

    pinMode(PIN_LED_OP, OUTPUT);
    pinMode(PIN_LED_PH, OUTPUT);
    pinMode(PIN_BOARD_LED, OUTPUT);

    setDefaultState();

    // Первичный замер тока при старте
    uint32_t startupBegin = millis();
    adcWindowStart = startupBegin;
    while ((millis() - startupBegin) < 120) {
        updateCurrentSensor();
    }

    lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
    stableCameraState = lastRawCamera;
    
}

void loop() {
    uint32_t now = millis();

    // 1. Постоянный фоновый замер тока
    updateCurrentSensor();

    // 2. Проверка 25-секундной блокировки
    if (isLocked) {
        if ((now - lockStartTime) < LOCK_TIME_MS) {
            // Во время блокировки синий светодиод на плате (PC13) быстро мигает[cite: 1, 2],
            // а любые новые сигналы с камеры игнорируются.
            bool blink = ((now / 250) % 2) == 0;
            digitalWrite(PIN_BOARD_LED, blink ? LOW : HIGH);
            return;
        } else {
            // 25 секунд истекли — возвращаем всё в исходное состояние по умолчанию
            isLocked = false;
            setDefaultState();

            lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
            stableCameraState = lastRawCamera;
            lastDebounceTime = now;
        }
    }

    // В дежурном режиме синий светодиод PC13 горит, если через мотор идет ток
    digitalWrite(PIN_BOARD_LED, isMotorRunning ? LOW : HIGH);

    // 3. Чтение сигнала с камеры (с защитой от дребезга)
    bool rawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);

    if (rawCamera != lastRawCamera) {
        lastDebounceTime = now;
        lastRawCamera = rawCamera;
    }

    if ((now - lastDebounceTime) >= DEBOUNCE_MS) {
        if (rawCamera != stableCameraState) {
            stableCameraState = rawCamera;

            // Как только пришел сигнал с камеры
            if (stableCameraState) {
                if (!isMotorRunning) {
                    // Тока нет (ворота стоят) -> OP включается, PH остается замкнутым
                    triggerActionGateIdle();
                } else {
                    // Ток есть (ворота идут) -> PH разрывается, OP выключен
                    triggerActionGateMoving();
                }

                // Запускаем блокировку входа на 25 секунд
                isLocked = true;
                lockStartTime = now;
            }
        }
    }
}