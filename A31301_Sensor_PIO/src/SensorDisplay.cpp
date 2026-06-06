#include "SensorDisplay.h"
#include "Globals.h"
#include <Arduino.h>
#include "MenuConfig.h"  // for resetting menu in showSensorData
#include "BatterySensor.h"
#include "BatteryFactory.h"
#include "BatteryConfig.h"
#include "BatteryBackground.h"

void SensorDisplay::showSensorData() {
    while (digitalRead(BACK_PIN) == HIGH) {
        // Move to the NEXT sensor when the joystick is pushed DOWN, PREVIOUS when UP
        // (active-low, wrap 0..15). NOTE: this board's stick is inverted vs the pin
        // names, so UP_PIN reads when the stick is physically pushed down.
        // One step per press: wait for release so a held stick doesn't race through.
        if (digitalRead(UP_PIN) == LOW) {        // physical DOWN -> next sensor (S0 -> S1)
            selectedSensor = (selectedSensor + 1) % 16;
            while (digitalRead(UP_PIN) == LOW && digitalRead(BACK_PIN) == HIGH) delay(10);
        }
        if (digitalRead(DOWN_PIN) == LOW) {      // physical UP -> previous sensor
            selectedSensor = (selectedSensor == 0) ? 15 : (selectedSensor - 1);
            while (digitalRead(DOWN_PIN) == LOW && digitalRead(BACK_PIN) == HIGH) delay(10);
        }

        oledDisplay.clearDisplay();

        A31301 &sensor = hallSensors[selectedSensor];
        int16_t x = sensor.readX();
        int16_t y = sensor.readY();
        int16_t z = sensor.readZ();
        bool connected = sensor.isConnected();

        // Header: selected sensor and address
        oledDisplay.setCursor(0, 0);
        oledDisplay.print("S");
        oledDisplay.print(String(selectedSensor));
        oledDisplay.print(" @0x");
        oledDisplay.print(String(sensor.getAddress(), HEX));

        oledDisplay.setCursor(0, 10);
        oledDisplay.print(connected ? "Connected" : "Disconnected");

        // Show values in selected units
        oledDisplay.setCursor(0, 22);
        if (gSensorUnits == SensorUnits::MILLITESLA) {
            float fx = (gCountsPerMilliTesla > 0.0f) ? (x / gCountsPerMilliTesla) : 0.0f;
            float fy = (gCountsPerMilliTesla > 0.0f) ? (y / gCountsPerMilliTesla) : 0.0f;
            float fz = (gCountsPerMilliTesla > 0.0f) ? (z / gCountsPerMilliTesla) : 0.0f;
            oledDisplay.print("X: "); oledDisplay.print(String(fx, 3)); oledDisplay.print(" mT");
            oledDisplay.setCursor(0, 34);
            oledDisplay.print("Y: "); oledDisplay.print(String(fy, 3)); oledDisplay.print(" mT");
            oledDisplay.setCursor(0, 46);
            oledDisplay.print("Z: "); oledDisplay.print(String(fz, 3)); oledDisplay.print(" mT");
        } else if (gSensorUnits == SensorUnits::GAUSS) {
            float fx = (gCountsPerMilliTesla > 0.0f) ? (x / gCountsPerMilliTesla * 10.0f) : 0.0f;
            float fy = (gCountsPerMilliTesla > 0.0f) ? (y / gCountsPerMilliTesla * 10.0f) : 0.0f;
            float fz = (gCountsPerMilliTesla > 0.0f) ? (z / gCountsPerMilliTesla * 10.0f) : 0.0f;
            oledDisplay.print("X: "); oledDisplay.print(String(fx, 3)); oledDisplay.print(" G");
            oledDisplay.setCursor(0, 34);
            oledDisplay.print("Y: "); oledDisplay.print(String(fy, 3)); oledDisplay.print(" G");
            oledDisplay.setCursor(0, 46);
            oledDisplay.print("Z: "); oledDisplay.print(String(fz, 3)); oledDisplay.print(" G");
        } else {
            // RAW
            oledDisplay.print("X: "); oledDisplay.print(String(x));
            oledDisplay.setCursor(0, 34);
            oledDisplay.print("Y: "); oledDisplay.print(String(y));
            oledDisplay.setCursor(0, 46);
            oledDisplay.print("Z: "); oledDisplay.print(String(z));
        }

        oledDisplay.show();
        delay(gSensorPagePeriodMs);
    }
    // Return to main menu once BACK is released:
    oledDisplay.setMainMenu(&MenuConfig::mainMenu);
}

void SensorDisplay::showVoltage() {
    // Ensure the background battery task is running
    BatteryBackground::start();

    // Local EMA state for average current over ~5 seconds
    static bool     s_init = false;
    static float    s_emaI = 0.0f;     // A
    static uint32_t s_lastMs = 0;

    if (!s_init) {
        s_init = true;
        s_emaI = 0.0f;
        s_lastMs = millis();
    }

    while (digitalRead(BACK_PIN) == HIGH) {
        BatteryReading r{};
        BatteryBackground::getLatest(r);

        // Update EMA of current with a time-based coefficient (~1 s time constant)
        uint32_t now = millis();
        float dt = (now - s_lastMs) / 1000.0f; if (dt < 0.0f) dt = 0.0f; s_lastMs = now;
        float tau = 1.0f; // seconds
        float k = dt / tau; if (k < 0.0f) k = 0.0f; if (k > 1.0f) k = 1.0f;
        s_emaI = (1.0f - k) * s_emaI + k * r.current_A;

        // Remaining capacity in Ah and estimated time to shutdown (Ets)
        int cap_mAh = BatteryConfig::getCapacityMah();
        float remaining_Ah = (cap_mAh / 1000.0f) * (r.soc_0_1 < 0 ? 0 : (r.soc_0_1 > 1 ? 1 : r.soc_0_1));
        float avg_I = s_emaI; // A, positive = discharge
        bool etsDefined = (avg_I > 0.005f); // ~5 mA threshold to avoid unstable numbers
        long ets_mins = 0;
        if (etsDefined) {
            float hours = remaining_Ah / avg_I;
            if (hours < 0) hours = 0;
            if (hours > 1e4f) hours = 1e4f; // clamp
            ets_mins = (long)(hours * 60.0f + 0.5f);
        }

        // Draw
        oledDisplay.clearDisplay();
        oledDisplay.setCursor(0, 0);
        #if defined(BOARD_GEN_2)
          oledDisplay.print("Battery");
        #else
          oledDisplay.print("Batt Analog)");
        #endif

        // Selected capacity (top-right)
        oledDisplay.setCursor(70, 0);
        oledDisplay.print("Cap:");
        oledDisplay.print(String(cap_mAh));
        oledDisplay.print("mAh");

        // Voltage
        oledDisplay.setCursor(0, 16);
        oledDisplay.print("V: "); oledDisplay.print(String(r.voltage_V, 3)); oledDisplay.print(" V");

        // Current with running average
        oledDisplay.setCursor(0, 28);
        oledDisplay.print("I: "); oledDisplay.print(String(r.current_A, 2)); oledDisplay.print(" A, avg: "); oledDisplay.print(String(s_emaI, 2));

        // State of charge
        oledDisplay.setCursor(0, 40);
        oledDisplay.print("SoC: "); oledDisplay.print(String(r.soc_0_1 * 100.0f, 1)); oledDisplay.print(" %");
        if (r.charging) { oledDisplay.print(" (Chg)"); }

        // ETA (charging) or Ets (discharging), in minutes
        oledDisplay.setCursor(0, 52);
        if (r.charging && r.eta_min >= 0.0f) {
            oledDisplay.print("ETA: ");
            oledDisplay.print(String(r.eta_min, 0));
            oledDisplay.print(" min");
        } else {
            oledDisplay.print("Ets: ");
            if (etsDefined) {
                oledDisplay.print(String(ets_mins)); oledDisplay.print(" mins");
            } else {
                oledDisplay.print("--");
            }
        }

        oledDisplay.show();
        delay(100); // ~10 Hz refresh
    }
    // Return to main menu once BACK is released:
    oledDisplay.setMainMenu(&MenuConfig::mainMenu);
}
