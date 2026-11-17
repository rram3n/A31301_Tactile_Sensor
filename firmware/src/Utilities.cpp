#include "Utilities.h"
#include <SoftPowerSwitch.h>
#include "Globals.h"

using namespace Utilities;

void Utilities::TaskCenterButtonCheck(void* /*pvParameters*/) {
    const unsigned long longPressDuration = 2000;
    unsigned long pressStartTime = 0;
    bool isPressed = false;

    while (true) {
        if (digitalRead(POWER_SW) == LOW) {
            if (!isPressed) {
                isPressed = true;
                pressStartTime = millis();
            }
            else if (millis() - pressStartTime >= longPressDuration) {
                SoftPowerSwitch sw(RED_LED_PIN, POWER_GATE, POWER_SW);
                sw.shutdown();
                while (digitalRead(POWER_SW) == LOW) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                isPressed = false;
            }
        } else {
            isPressed = false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void Utilities::returnToPreviousMenu() {
    oledDisplay.returnToPreviousMenu();
}
