#pragma once
#include <Arduino.h>

namespace Config {
    // --- Входы (Black Pill F401CC) ---
    constexpr uint8_t PIN_CAMERA_IN    = PA0;  // нормально разомкнут
    constexpr uint8_t PIN_TA12_ADC     = PA1;  // Датчик тока TA12-200
    constexpr uint8_t PIN_LIMIT_OPEN   = PA2;  // Концевик ОТКРЫТО (NC: разомкнут = HIGH)
    constexpr uint8_t PIN_LIMIT_CLOSE  = PA3;  // Концевик ЗАКРЫТО (NC: замкнут = LOW, разомкнут = HIGH)
    constexpr uint8_t PIN_RESET_BUTTON = PA4;  // Кнопка СБРОС (NO: нажата = LOW)
    constexpr uint8_t PIN_DOOR_IN      = PB6;  // Вход от контроллера PERCo (NO: замкнут на GND = LOW)

    // --- Выходы реле и индикации ---
    constexpr uint8_t PIN_OUT_OP       = PB0;  // Выход OP (Открытие/Старт) - Open Drain
    constexpr uint8_t PIN_OUT_PH       = PB1;  // Выход PH (Оптическая линия) - Open Drain
    constexpr uint8_t PIN_OUT_PASS     = PB8;  // Реле 1: Пропуск человека - Open Drain
    constexpr uint8_t PIN_OUT_CLOSE_DR = PB9;  // Реле 2: Закрытие двери - Open Drain

    constexpr uint8_t PIN_LED_OP       = PA6;  // Индикатор OP
    constexpr uint8_t PIN_LED_PH       = PA7;  // Индикатор PH
    constexpr uint8_t PIN_BOARD_LED    = PC13; // Встроенный LED (Active LOW)

    // --- Тайминги и полярность ---
    constexpr bool     RELAY_ACTIVE_HIGH   = false; // false = Active LOW (управление нулем)
    constexpr uint32_t DEBOUNCE_MS         = 25;    // Антидребезг (мс)
    constexpr uint32_t DEAD_TIME_MS        = 5;     // Защитная пауза реле (мс)
    constexpr uint32_t LOCK_TIME_MS        = 25000; // Максимальное время блокировки ворот (25 сек)
    constexpr uint32_t BLINK_INTERVAL_MS   = 200;   // Частота мигания PC13 (мс)

    // --- Тайминги режима прохода человека ---
    constexpr uint32_t PASS_OPEN_MS        = 2000;  // Время работы реле пропуска человека (2 сек)
    constexpr uint32_t PASS_WAIT_CLOSE_MS  = 14000; // Пауза перед закрытием двери (14 сек)
    constexpr uint32_t PASS_CLOSE_MS       = 2000;  // Длительность импульса реле закрытия двери (2 сек)

    // --- Параметры датчика тока TA12-200 ---
    constexpr float    V_REF             = 3.3f;
    constexpr float    ADC_MAX           = 4095.0f;
    constexpr float    R_LOAD            = 800.0f;
    constexpr float    TURNS_RATIO       = 2000.0f;
    constexpr float    WIRE_TURNS        = 1.0f;
    constexpr uint32_t CURRENT_WIN_MS    = 100;

    // Гистерезис тока двигателя
    constexpr float    CURRENT_ON_A      = 0.15f; // Порог включения
    constexpr float    CURRENT_OFF_A     = 0.10f; // Порог выключения
}