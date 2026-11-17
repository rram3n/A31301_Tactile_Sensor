#include "Globals.h"
#include <Wire.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "PowerSettings.h"
#include <SoftPowerSwitch.h>

#include "SensorDisplay.h"
#include "MenuConfig.h"
#include "Utilities.h"
#include "BatteryBackground.h"

// ---------------------------------------------------------------------------
// MENU-ONLY FIRMWARE
// Hall-sensor reader: OLED menu, 16x A31301 live view, Battery page, and soft
// power switch. No BLE / LoRa / ESP-NOW / servo / RGB / joystick code.
// ---------------------------------------------------------------------------

// 16x A31301 Hall-effect sensors at I2C addresses 0x60..0x6F
A31301 hallSensors[16] = {
    A31301(0x60), A31301(0x61), A31301(0x62), A31301(0x63),
    A31301(0x64), A31301(0x65), A31301(0x66), A31301(0x67),
    A31301(0x68), A31301(0x69), A31301(0x6A), A31301(0x6B),
    A31301(0x6C), A31301(0x6D), A31301(0x6E), A31301(0x6F)
};
uint8_t               selectedSensor = 0; // default to the first sensor

// Sensor display configuration (defaults)
SensorUnits gSensorUnits = SensorUnits::RAW;
float       gCountsPerMilliTesla = 81.967213f;  // counts per mT
uint32_t    gSensorPagePeriodMs = 100;          // ~10 Hz OLED page

OLED_Display          oledDisplay(OLED_RESET_PIN);

// Soft power switch (LED + power gate + battery monitor)
SoftPowerSwitch       gPowerSwitch(RED_LED_PIN, POWER_GATE, POWER_SW);

// Background init so the boot UI isn't blocked.
static void InitTask(void*) {
    // Hall sensors (bind to I2C bus)
    for (auto &hs : hallSensors) {
        hs.begin(Wire);
    }

    // Battery background task (feeds the Battery page and the SoftPowerSwitch monitor)
    BatteryBackground::start();

    vTaskDelete(nullptr);
}

void setup() {
    Serial.begin(115200);

    // Soft power switch + battery alert settings + monitor task
    gPowerSwitch.setup();
    PowerSettingsStore::begin();
    gPowerSwitch.beginMonitoring();

    // Center-button long-press shutdown handling
    pinMode(POWER_SW, INPUT_PULLUP);

    // I2C early and fast (OLED + Hall sensors)
    Wire.setPins(SDA_PIN, SCL_PIN);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000); // 400 kHz Fast Mode

    // OLED + menu for instant UI
    oledDisplay.begin(0, 1, SSD1306_WHITE, UP_PIN, DOWN_PIN, ENTER_PIN, BACK_PIN, POWER_SW);
    MenuConfig::updateMainMenuForRole();

    // Center-button long-press task (power off)
    xTaskCreatePinnedToCore( Utilities::TaskCenterButtonCheck, "CenterButtonCheckTask", 2048, nullptr, 1, nullptr, 1 );

    // NVS init
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Background hardware init on the other core
    xTaskCreatePinnedToCore( InitTask, "InitTask", 8192, nullptr, 1, nullptr, 1 );

    Serial.println("Setup complete. Menu-only build. UI ready.");
}

void loop() {
    // Drive menu navigation + the sensor / battery / shutdown actions it triggers.
    oledDisplay.handleMenu();
    delay(10);
}
