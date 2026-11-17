#pragma once
#include "OLED_Display.h"

// MenuConfig holds the `MenuItem mainMenu` and a helper to (re)install it.
namespace MenuConfig {
    extern MenuItem mainMenu;

    // Installs the main-menu layout on the OLED. Called from setup().
    void updateMainMenuForRole();
}
