# PlatformIO Ballu Code

- Special thanks to Eric (@EricYufengWu) for migrating from Arduino IDE to PlatformIO

For legacy codes, go to release ver. 1.4.0
## How to flash
Make sure you have installed:
- VS Code
- Platform IO extension
- Platform IO Core
- ESP-IDF

### Build
1. Make sure the environment you use is correct (Default to env:4d_systems_esp32s3_gen4_r8n16)
2. click build (check mark in the picture below)
3. If everything compiles, all the dependencies are installed correctly. 
4. Connect the BALLU board while pressing the reset button
5. Click upload (right arrow in the picture below)
![img.png](pio_program.png)


### if you get an error connecting, try one of the following
1. check if the ballu board screen is off. If not replug while pressing reset.
2. set the serial port manually (default: auto)
3. Make sure the type-c cable supports data transfer (not charging cable)
4. If you still have issues uploading, create a GitHub issue


## Upload without build
`pio run -t nobuild -t upload` in the terminal (Click PlatformIO: Open New Terminal)



#### Quick Links
- Main usage and connection guides: see repository root README.
- Debugging tips: see root README → Debugging Guide.

#### After Flashing: First Steps
- Pair Xbox controller: Joystick → Connect
- Try BLE bridge: Bridge → BLE Slave (on remote) and Bridge → BLE Master (on controller)
- Try ESP-NOW: Bridge → ESP-NOW Slave (Pair) on remote; Bridge → ESP-NOW Master on controller; select ballu_…
- Save the pair and later use Bridge → ESP-NOW for quick connect

#### Serial Streaming Test
- On device: Sensor → Stream: ALL -> Serial
- On PC: open a serial terminal at 115200 or use PC-Controller/receive_a31301_stream.py


---

## Code Structure Overview (Ballu_PIO)
This section orients firmware contributors to the organization and responsibilities of the firmware code. It focuses on the ESP32-S3 build (Arduino framework) used by Ballu Gen1/Gen2 boards.

### 1) PlatformIO environments (platformio.ini)
- default_envs = ballu_gen2
  - BOARD_GEN_2, NimBLE enabled, zh_network, extra INA219 dependency.
- env:ballu_gen1
  - BOARD_GEN_1, NimBLE enabled.
- env:hub_role
  - ROLE_HUB defined, isolates only the hub role entry (see src/hub_role_main.cpp), serial monitor at 115200. Uses build_src_filter to only compile hub_role_main.cpp (which includes espnow_hub.cpp).

Typical commands from Ballu_PIO:
- pio run -e ballu_gen2
- pio run -e ballu_gen2 -t upload
- pio device monitor -b 115200
- To build only hub role: pio run -e hub_role

### 2) Entry points and roles
- src/main.cpp (default, non-hub build)
  - Full interactive firmware with OLED UI, BLE, ESP-NOW, LoRa, Servos, RGB, Battery background.
  - Schedules InitTask for heavy inits (sensors, RGB, servos, BLE, LoRa, settings) and keeps UI responsive.
  - loop() orchestrates:
    - PCSerialControl::poll() → handles ASCII control/status from PC-Controller (HUB_START/STOP/STATUS etc.).
    - OLED menu handling and state display.
    - Reads joystick snapshot (JoystickBLE) once and fans out to ServoController and RGBController.
    - LoRa processing.
    - If BridgeSettings.role == Hub → forward joystick to pair masters (ESP-NOW).
    - If BridgeSettings.role == PairMaster → originate joystick to slave when Hub is idle; injects brief synthetic color button pulses for RGB keepalive.
- src/hub_role_main.cpp (env:hub_role)
  - Minimal entry that includes espnow_hub.cpp to build a clean Hub-only image without the UI and other subsystems. Useful for bench testing PC-Controller ↔ Hub flows.

### 3) Subsystems (headers in include/, implementations in src/)
- Bridges and control paths
  - ESPNowBridge.{h,cpp}
    - Manages ESP-NOW roles: Hub, PairMaster, PairSlave; pairing beacons; sending joystick commands (hubSendToPairs, pairMasterSendToSlave), and last-hub timing logic.
  - BLEBridge.{h,cpp} + JoystickBLE.{h,cpp}
    - BLE transport for Xbox controller; JoystickBLE exposes joyVal[] and a TaskJoystickBLE RTOS task. BLEBridge handles connection setup depending on role.
  - PCSerialControl.{h,cpp}
    - ASCII command/response interface over USB CDC. Exposes HUB_START/HUB_STOP/HUB_STATUS, LIST_MAP/IDX/MAP prints, and diagnostics used by the Flask backend.
  - lora.{h,cpp}
    - LoRa auxiliary link (optional), integrated in loop() for byte processing and role reporting.
- UI and configuration
  - OLED_Display.{h,cpp}
    - OLED drawing, menu navigation, button handling.
  - MenuConfig.{h,cpp}
    - Menu tree and callbacks; calls into bridges (BLE/ESP-NOW/LoRa) and settings.
  - BridgeSettings.{h,cpp}
    - Persistent (NVS) settings: current role, paired MACs, hasMaster/Slave, left/right PM flags. BridgeSettingsStore::{begin,load,save}.
  - PM_HubPairing.{h,cpp}
    - Pairing helpers for Hub/PairMaster/PairSlave modes.
- Actuators and feedback
  - RGBController.{h,cpp}
    - LED color logic driven by joystick and modes; has currentColor for diagnostics; optional debug with throttling.
  - ServoController.{h,cpp}
    - Servo setup and handleServos() using joystick snapshot; stores calibration in NVS.
- Power and battery
  - SoftPowerSwitch.{h,cpp}
    - Power button and red LED handling, power gate control, debounced long-press task.
  - BatteryBackground.{h,cpp}, BatteryConfig.{h,cpp}, BatteryFactory.{h,cpp}, BatterySensor.{h,cpp}, INA219SoCSensor.cpp, AnalogVoltageSensor.cpp
    - Battery monitoring stack; background task periodically samples current/voltage, estimates SoC, raises alerts; configured via BatteryConfig/Factory.
  - PowerSettings.{h,cpp}
    - Load/save battery and power-related settings (NVS) used by the background monitor.
- Sensors
  - A31301.{h,cpp}
    - Hall-effect sensor array (9 devices on I2C). SensorDisplay.{h,cpp} renders sensor pages to OLED; Utilities.{h,cpp} includes small helpers.

### 4) Control and data flow (high-level)
- Xbox controller → JoystickBLE::joyVal[] (asynchronous task on core 0)
- main loop → snapshot joyVal into a local array to avoid tearing, then:
  - ServoController.handleServos() → updates servos
  - RGBController.handleRGB() → updates LEDs
  - If role == Hub and ESPNowBridge active → build JoystickCmd and hubSendToPairs()
  - If role == PairMaster and no recent Hub joystick seen → pairMasterSendToSlave() at ~20 Hz (with keepalive every ~1.5 s)
- PC-Controller (Flask) over USB CDC:
  - PCSerialControl::poll() handles commands and prints. Ensure firmware emits READY, IDX:, MAP:, and HUB_STATUS/STAT lines as used by backend parsers.

### 5) Logging and PC-Controller contracts
- READY, IDX:, MAP: lines are consumed by PC-Controller to detect readiness and build idx→mac map.
- HUB_STATUS/HUB_STATS commands should print single-line summaries parsable on the server; avoid excessive flooding.
- When adding new diagnostics, prefer concise key=value tokens (e.g., NODE: idx=7 batt=78 rssi=-62) — these are parsed by the backend.

### 6) Extending safely (firmware)
- Timing and tasks
  - Keep UI responsive: heavy inits go into InitTask. Use xTaskCreatePinnedToCore with reasonable stack sizes.
  - Avoid blocking in loop(); prefer short, periodic work with millis() scheduling.
- Joystick mapping
  - Maintain the int16 [-32768..32767] mapping for lx/ly/rx/ry and 0..255 for triggers to match PC-Controller expectations.
- ASCII protocol stability
  - Do not remove READY / LIST_MAP / IDX/MAP prints; BalluController.wait_for_idx_map() relies on them.
  - If changing field names, update PC-Controller parsers accordingly.
- Persistence
  - Use BridgeSettingsStore and ServoController::initNVS for NVS read/write. Avoid frequent writes in tight loops.
- Battery safety
  - Respect BatteryBackground thresholds; surface alerts on OLED and, where applicable, via PC-Serial logs.

### 7) Quick file map (selected)
- Roles/entry: src/main.cpp, src/hub_role_main.cpp, src/espnow_hub.cpp, src/espnow_node.cpp
- Bridges: include/ESPNowBridge.h, src/ESPNowBridge.cpp; include/BLEBridge.h, src/BLEBridge.cpp; include/PCSerialControl.h, src/PCSerialControl.cpp; include/lora.h, src/lora.cpp
- UI: include/OLED_Display.h, src/OLED_Display.cpp; include/MenuConfig.h, src/MenuConfig.cpp
- Actuators: include/RGBController.h, src/RGBController.cpp; include/ServoController.h, src/ServoController.cpp
- Power/Battery: include/SoftPowerSwitch.h, src/SoftPowerSwitch.cpp; include/Battery*.h, src/Battery*.cpp; src/INA219SoCSensor.cpp, src/AnalogVoltageSensor.cpp
- Sensors: include/A31301.h, src/A31301.cpp; src/SensorDisplay.cpp
- Utilities/Globals: include/Globals.h, include/Utilities.h, src/Utilities.cpp, include/PinsGen*.h, include/Version.h

### 8) Common gotchas
- BLE + ESP-NOW coexistence: do not free BT memory; keep NimBLE config flags (see platformio.ini). Use 400 kHz I2C for OLED/sensors.
- Hub vs full build: use env:hub_role to produce a slim Hub image for PC-Controller testing; it compiles espnow_hub.cpp via hub_role_main.cpp only.
- Serial prints rate: verbose logs can overwhelm USB; throttle or guard with VERBOSE flags in your code.

If you add new modules, mirror the style: declare public API in include/, implement in src/, and keep prints compatible with the PC-Controller’s parsers.
