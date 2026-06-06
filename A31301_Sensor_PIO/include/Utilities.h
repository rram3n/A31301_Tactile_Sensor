#pragma once
#include <Arduino.h>
#include "Globals.h"

namespace Utilities {
    // Center‐button long‐press (uses POWER_SW, RED_LED_PIN, POWER_GATE from Globals)
    void TaskCenterButtonCheck(void* pvParameters);

    // Return to previous menu (calls oledDisplay.returnToPreviousMenu())
    void returnToPreviousMenu();
}
