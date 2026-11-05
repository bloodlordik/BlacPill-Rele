#include <Arduino.h>

// ================= 1. НАСТРОЙКА ПИНОВ (Black Pill F401CC) =================
const uint8_t PIN_CAMERA_IN  = PA0;  // Вход сигнала от камеры (подача 3.3V)
const uint8_t PIN_TA12_ADC   = PA1;  // Вход датчика тока TA12-200 (SIG -> PA1, GND -> G)

const uint8_t PIN_OUT_OP     = PB0;  // Выход PB0 (включается, если ТОКА НЕТ)
const uint8_t PIN_OUT_PH     = PB1;  // Выход PB1 (включается, если ТОК ЕСТЬ)

const uint8_t PIN_LED_OP     = PA6;  // Светодиод индикации PB0
const uint8_t PIN_LED_PH     = PA7;  // Светодиод индикации PB1
const uint8_t PIN_BOARD_LED  = PC13; // Встроенный светодиод платы (Active LOW: LOW = горит)

// ================= 2. НАСТРОЙКА РЕЛЕ И ВРЕМЕНИ =================
const bool RELAY_ACTIVE_HIGH = true;  // Поставьте false, если реле включается уровнем LOW

const uint32_t DEBOUNCE_MS   = 25;    // Антидребезг входа PA0 (25 мс)
const uint32_t DEAD_TIME_MS  = 5;     // Защитная пауза переключения (5 мс)
const uint32_t LOCK_TIME_MS  = 25000;  // Блокировка входа на 5 секунд (5000 мс)

// Параметры датчика тока TA12-200
const float V_REF            = 3.3f;
const float ADC_MAX_VALUE    = 4095.0f;
const float R_LOAD           = 800.0f;
const float TURNS_RATIO      = 2000.0f;
const float WIRE_TURNS       = 1.0f;

// Порог срабатывания датчика тока (0.20 А RMS)
const float MOTOR_CURRENT_THRESHOLD_A = 0.20f;
const uint32_t CURRENT_WINDOW_MS      = 100;  // Окно замера (100 мс = 5 периодов 50 Гц)

// ================= 3. ПЕРЕМЕННЫЕ СОСТОЯНИЯ =================
const uint8_t RELAY_ON  = RELAY_ACTIVE_HIGH ? HIGH : LOW;
const uint8_t RELAY_OFF = RELAY_ACTIVE_HIGH ? LOW  : HIGH;

uint32_t adcWindowStart     = 0;
int currentWindowMaxADC     = 0;
float latestMotorCurrentRMS = 0.0f;
bool isMotorRunning         = false;

bool lastRawCamera          = false;
bool stableCameraState      = false;
uint32_t lastDebounceTime   = 0;

bool isLocked               = false;
uint32_t lockStartTime      = 0;

// --- СОСТОЯНИЕ ПО УМОЛЧАНИЮ: обе линии PB0 и PB1 РАЗОМКНУТЫ ---
void setDefaultState() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF);
    digitalWrite(PIN_LED_OP, LOW);

    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_OFF);
    digitalWrite(PIN_LED_PH, LOW);
}

// --- СЦЕНАРИЙ 1: ТОКА НЕТ -> Подаем сигнал на PB0, PB1 разомкнут ---
void triggerActionNoCurrent() {
    digitalWrite(PIN_OUT_PH, RELAY_OFF);
    digitalWrite(PIN_LED_PH, LOW);

    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_OP, RELAY_ON);
    digitalWrite(PIN_LED_OP, HIGH);
}

// --- СЦЕНАРИЙ 2: ТОК ЕСТЬ -> Подаем сигнал на PB1, PB0 разомкнут ---
void triggerActionWithCurrent() {
    digitalWrite(PIN_OUT_OP, RELAY_OFF);
    digitalWrite(PIN_LED_OP, LOW);

    delay(DEAD_TIME_MS);

    digitalWrite(PIN_OUT_PH, RELAY_ON);
    digitalWrite(PIN_LED_PH, HIGH);
}

// --- Безопасное чтение АЦП с принудительным разрядом паразитной ёмкости пина ---
int readTa12Adc() {
    // 1. Кратковременно прижимаем пин к GND, чтобы разрядить ёмкость пустого пина
    pinMode(PIN_TA12_ADC, OUTPUT);
    digitalWrite(PIN_TA12_ADC, LOW);
    delayMicroseconds(5);

    // 2. Переводим пин обратно во вход и даём датчику TA12-200 (800 Ом) выставить своё напряжение
    pinMode(PIN_TA12_ADC, INPUT_ANALOG);
    delayMicroseconds(30);

    return analogRead(PIN_TA12_ADC);
}

// --- Неблокирующий фоновый опрос датчика тока на PA1 ---
void updateCurrentSensor() {
    int adcVal = readTa12Adc();
    if (adcVal > currentWindowMaxADC) {
        currentWindowMaxADC = adcVal;
    }

    uint32_t now = millis();
    if ((now - adcWindowStart) >= CURRENT_WINDOW_MS) {
        // Расчет тока по максимальному значению за 100 мс
        float iMax = ((float)currentWindowMaxADC / ADC_MAX_VALUE) * (V_REF / R_LOAD) * TURNS_RATIO;
        latestMotorCurrentRMS = (iMax / 1.4142f) / WIRE_TURNS;

        if (latestMotorCurrentRMS < 0.08f) {
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

    pinMode(PIN_OUT_OP, OUTPUT);
    pinMode(PIN_OUT_PH, OUTPUT);

    pinMode(PIN_LED_OP, OUTPUT);
    pinMode(PIN_LED_PH, OUTPUT);
    pinMode(PIN_BOARD_LED, OUTPUT);

    // По умолчанию обе линии PB0 и PB1 разомкнуты, синий светодиод PC13 погашен
    setDefaultState();
    digitalWrite(PIN_BOARD_LED, HIGH);

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

    // 1. Постоянный фоновый замер тока на PA1
    updateCurrentSensor();

    // 2. Обработка 5-секундной блокировки входа
    if (isLocked) {
        if ((now - lockStartTime) < LOCK_TIME_MS) {
            // Во время блокировки синий светодиод PC13 быстро мигает (каждые 200 мс)
            bool blink = ((now / 200) % 2) == 0;
            digitalWrite(PIN_BOARD_LED, blink ? LOW : HIGH);
            return;
        } else {
            // 5 секунд истекли — размыкаем обе линии PB0 и PB1
            isLocked = false;
            setDefaultState();

            lastRawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);
            stableCameraState = lastRawCamera;
            lastDebounceTime = now;
        }
    }

    // В дежурном режиме синий светодиод PC13 ГОРИТ (LOW), если на датчике PA1 есть ток,
    // и ПОГАШЕН (HIGH), если тока нет
    digitalWrite(PIN_BOARD_LED, isMotorRunning ? LOW : HIGH);

    // 3. Чтение сигнала на PA0 (с антидребезгом)
    bool rawCamera = (digitalRead(PIN_CAMERA_IN) == HIGH);

    if (rawCamera != lastRawCamera) {
        lastDebounceTime = now;
        lastRawCamera = rawCamera;
    }

    if ((now - lastDebounceTime) >= DEBOUNCE_MS) {
        if (rawCamera != stableCameraState) {
            stableCameraState = rawCamera;

            // При подаче сигнала на PA0
            if (stableCameraState) {
                if (!isMotorRunning) {
                    // Тока на PA1 НЕТ -> подаем сигнал на PB0 (PA6)
                    triggerActionNoCurrent();
                } else {
                    // Ток на PA1 ЕСТЬ -> подаем сигнал на PB1 (PA7)
                    triggerActionWithCurrent();
                }

                // Блокируем вход на 5 секунд
                isLocked = true;
                lockStartTime = now;
            }
        }
    }
}