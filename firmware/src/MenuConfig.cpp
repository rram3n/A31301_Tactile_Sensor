#include "MenuConfig.h"
#include "Globals.h"
#include <SoftPowerSwitch.h>
#include "Utilities.h"
#include "SensorDisplay.h"

// ——— Wrappers for menu actions ———

// Per-sensor selection wrappers: set the active sensor index, then open the
// live sensor view. SensorDisplay::showSensorData() reads `selectedSensor`.
static void selectS0()  { selectedSensor = 0;  SensorDisplay::showSensorData(); }
static void selectS1()  { selectedSensor = 1;  SensorDisplay::showSensorData(); }
static void selectS2()  { selectedSensor = 2;  SensorDisplay::showSensorData(); }
static void selectS3()  { selectedSensor = 3;  SensorDisplay::showSensorData(); }
static void selectS4()  { selectedSensor = 4;  SensorDisplay::showSensorData(); }
static void selectS5()  { selectedSensor = 5;  SensorDisplay::showSensorData(); }
static void selectS6()  { selectedSensor = 6;  SensorDisplay::showSensorData(); }
static void selectS7()  { selectedSensor = 7;  SensorDisplay::showSensorData(); }
static void selectS8()  { selectedSensor = 8;  SensorDisplay::showSensorData(); }
static void selectS9()  { selectedSensor = 9;  SensorDisplay::showSensorData(); }
static void selectS10() { selectedSensor = 10; SensorDisplay::showSensorData(); }
static void selectS11() { selectedSensor = 11; SensorDisplay::showSensorData(); }
static void selectS12() { selectedSensor = 12; SensorDisplay::showSensorData(); }
static void selectS13() { selectedSensor = 13; SensorDisplay::showSensorData(); }
static void selectS14() { selectedSensor = 14; SensorDisplay::showSensorData(); }
static void selectS15() { selectedSensor = 15; SensorDisplay::showSensorData(); }

// Battery page (shows voltage / percentage)
static void showVoltageWrapper() {
    SensorDisplay::showVoltage();
}

// Soft power-down
static void shutdownPowerWrapper() {
    SoftPowerSwitch sw(RED_LED_PIN, POWER_GATE, POWER_SW);
    sw.shutdown();
}

// ——— Menu definitions ———

// Shutdown confirmation submenu
static MenuItem shutdownMenuItems[] = {
    {"Yes", nullptr, 0, 202, shutdownPowerWrapper},
    {"No",  nullptr, 0, 201, Utilities::returnToPreviousMenu}
};

// Main-menu items: 16 Hall sensors at the top level + battery percentage + shutdown.
// Each sensor entry calls its selectSx wrapper -> SensorDisplay::showSensorData().
static MenuItem mainMenuItems[] = {
    {"S0: 0x60",  nullptr, 0, 310, selectS0},
    {"S1: 0x61",  nullptr, 0, 311, selectS1},
    {"S2: 0x62",  nullptr, 0, 312, selectS2},
    {"S3: 0x63",  nullptr, 0, 313, selectS3},
    {"S4: 0x64",  nullptr, 0, 314, selectS4},
    {"S5: 0x65",  nullptr, 0, 315, selectS5},
    {"S6: 0x66",  nullptr, 0, 316, selectS6},
    {"S7: 0x67",  nullptr, 0, 317, selectS7},
    {"S8: 0x68",  nullptr, 0, 318, selectS8},
    {"S9: 0x69",  nullptr, 0, 319, selectS9},
    {"S10: 0x6A", nullptr, 0, 320, selectS10},
    {"S11: 0x6B", nullptr, 0, 321, selectS11},
    {"S12: 0x6C", nullptr, 0, 322, selectS12},
    {"S13: 0x6D", nullptr, 0, 323, selectS13},
    {"S14: 0x6E", nullptr, 0, 324, selectS14},
    {"S15: 0x6F", nullptr, 0, 325, selectS15},
    {"Battery %", nullptr, 0, 330, showVoltageWrapper}, // show battery percentage
    {"Shutdown",  shutdownMenuItems, 2, 1, nullptr}
};

// The single, exported `mainMenu`:
MenuItem MenuConfig::mainMenu = {
    "Main Menu",
    mainMenuItems,
    sizeof(mainMenuItems)/sizeof(MenuItem),
    0,
    nullptr
};

void MenuConfig::updateMainMenuForRole() {
    // Single fixed layout; no role-based menu swapping anymore.
    MenuConfig::mainMenu.submenu = mainMenuItems;
    MenuConfig::mainMenu.submenuSize = sizeof(mainMenuItems)/sizeof(MenuItem);
    oledDisplay.setMainMenu(&MenuConfig::mainMenu);
    oledDisplay.updateMenu();
}

