#pragma once
#include <Arduino.h>
#include "A31301.h"
#include "OLED_Display.h"

// ——— Board pins (Gen2) ———
#include "PinsGen2.h"

// Units selection for sensor values
enum class SensorUnits : uint8_t { RAW = 0, MILLITESLA = 1, GAUSS = 2 };

// Externally visible instances:
extern A31301                hallSensors[16];
extern uint8_t               selectedSensor; // 0..15
extern OLED_Display          oledDisplay;

// Sensor display configuration
extern SensorUnits           gSensorUnits;          // default RAW
extern float                 gCountsPerMilliTesla;  // counts per mT
extern uint32_t              gSensorPagePeriodMs;   // default 100 ms (~10 Hz)
