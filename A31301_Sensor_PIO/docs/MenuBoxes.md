### On‑device menu controls: NumberBox, RadioBox, CheckBox, and NumberPicker

This document describes the new menu widgets added to the PairMaster/Slave firmware UI, how they behave, and how to use them safely with the project’s NVS hygiene rules.

Audience: firmware developers extending the on‑device OLED menu (Adafruit SSD1306) in Ballu_PIO.

Contents
- Overview and when to use each control
- Data models and fields (OLED_Display.h)
- User interactions (buttons and states)
- Persistence best practices (RAM cache + save‑on‑commit)
- Normal menu style markers for simple lists
- Examples and patterns from MenuConfig.cpp
- Testing checklist and gotchas

---

Overview

- NumberBox: Edits a bounded integer in place. Great for short numeric ranges (e.g., 1..13 channel, battery 
  thresholds). Up/Down change value; Enter toggles edit/commit.
- RadioBox: Presents a mutually exclusive list as an inline editor page. Up/Down change highlighted option; Enter commits.
- CheckBox: Toggles a boolean setting. Single Enter flips and persists.
- NumberPicker: Per‑digit editor for fixed‑point numbers (e.g., 2.5 Hz). Up/Down move between digits (browse mode) or 
  increment/decrement the selected digit (edit mode). Enter toggles between browse and edit. Back saves and exits.

All four controls are integrated into the existing OLED list navigation and honor the overlay header area to minimize flicker.

---

Data models (include/OLED_Display.h)

1) NumberBoxModel
- Fields
  - value: int32_t* — bound RAM variable
  - minVal, maxVal, step
  - unit: const char* (optional, e.g., "%" or "ch")
  - onChange(int32_t): optional live callback while editing
  - onCommit(int32_t): persist on commit (called on Enter while editing)
  - textSize: uint8_t (default 1) — optional scaling of the value line

2) RadioBoxModel
- Fields
  - selected: int32_t* — holds the selected option value
  - options: const RadioBoxOption* — array of {label, value}
  - optionCount: int
  - onCommit(int32_t): persist on commit
  - textSize: uint8_t (default 1)
  - onPreOpen(): called right before opening the list; use to resync bound value from live stores

3) CheckBoxModel
- Fields
  - value: bool*
  - onToggle(bool): optional live callback (update RAM cache)
  - onCommit(bool): persist immediately when toggled
  - textSize: uint8_t (default 1)
  - onPreOpen(): resync before toggling

4) NumberPickerModel
- Scaled fixed‑point editor using base‑10 scaling (valueScaled = round(value * 10^decimals)).
- Fields
  - valueScaled: int32_t*
  - minScaled, maxScaled: inclusive bounds in scaled units
  - decimals: uint8_t — number of fractional digits
  - minIntDigits: pad integer part with zeros (default 1)
  - padFraction: pad/truncate fractional part (default true)
  - unit: const char*
  - onChange(int32_t scaled): optional live notify
  - onCommit(int32_t scaled): persist on Back
  - textSize: uint8_t (default 1)

MenuItem wiring
- Each MenuItem has a kind discriminator (MenuItemKind) and payload fields nb/rb/cb/np.
- For convenience, use factory helpers to avoid aggregate‑init pitfalls:
  - makeNumberBoxItem(name, id, NumberBoxModel)
  - makeRadioBoxItem(name, id, RadioBoxModel)
  - makeCheckBoxItem(name, id, CheckBoxModel)
  - makeNumberPickerItem(name, id, NumberPickerModel)

---

User interactions (buttons)

- Common navigation: Up/Down move list selection; Enter selects; Back returns.

NumberBox
- Enter to start editing → Up/Down adjust by step (clamped to [min,max]); Enter again commits and exits edit; Back cancels.
- Visual: value rendered with ASCII arrows: "^ 12% v". Text size is configurable via nb.textSize.

RadioBox
- Enter opens the radio list; Up/Down move the bullet; Enter commits; Back cancels and returns.
- Visual: "(o)" for selected and "( )" for others (ASCII‑safe). Optional rb.textSize.
- onPreOpen() is called before opening to synchronize the current selection from live RAM stores (e.g., BridgeSettings).

CheckBox
- Enter toggles the boolean. onPreOpen() runs before toggling. onToggle updates local cache; onCommit persists.
- Visual: "[x] ON" or "[ ] OFF" in the editor; for "normal menu" style, see Markers section below.

NumberPicker
- Two states: browse (select digit) and edit (change digit).
  - Browse: Up/Down move the digit cursor (wraparound). Enter enters Edit.
  - Edit: Up increments digit (with carry), Down decrements (with borrow), clamped to [minScaled,maxScaled]. Enter returns to Browse.
  - Back: commits via onCommit() and exits the picker.
- Visual: only the selected digit is inverted (white background, black digit). Unit is shown after the number.

---

Persistence and NVS hygiene

- RAM cache first: Read settings once and keep local copies. Stores (PowerSettingsStore, BridgeSettingsStore) provide a RAM‑cached get()/load().
- Save only on explicit user actions: NumberBox commit, CheckBox toggle, RadioBox selection, NumberPicker Back.
- Avoid periodic NVS reads/writes. For UI correctness when a setting may have changed elsewhere, use onPreOpen() to resync bound values from the store just before opening the editor.

---

Normal menu style markers (no editor page)

When a setting has a small set of preset choices (e.g., 2–5 items), prefer decorating the list labels directly to show the current choice:

- Radio‑style: prefix with "(o) " for selected and "( ) " for others.
- Checkbox‑style single toggle: draw one item whose label includes "[x]" or "[ ]"; pressing Enter flips the state.

Helpers used in src/MenuConfig.cpp:
- stripMarkerPrefix(String): removes any previous marker prefix.
- applyRadioMarkers(MenuItem* items, int count, int selectedIndex)
- applyCheckMarkers(MenuItem* items, int count, int selectedIndex) — simple variant for checkbox visuals.

Examples (from src/MenuConfig.cpp)

1) Battery Capacity (normal menu, radio markers)
```
static MenuItem batteryCapacityMenuItems[] = {
  {"450 mAh", nullptr, 0, 211, setCap450},
  {"520 mAh", nullptr, 0, 212, setCap520},
  {"660 mAh", nullptr, 0, 213, setCap660},
  {"Back",     nullptr, 0, 214, Utilities::returnToPreviousMenu}
};

static void refreshBatteryCapacityMenuMarkers() {
  int sel = currentBatteryCapIndex();
  applyRadioMarkers(batteryCapacityMenuItems, 3, sel);
  batteryCapacityMenuItems[3].name = stripMarkerPrefix(batteryCapacityMenuItems[3].name);
}
```

2) ESP‑NOW Channel (NumberBox within Bridge submenu)
```
static int32_t s_bridge_channel = kDefaultEspChannel;
static MenuItem bridgeChannel_nb = makeNumberBoxItem(
  "Channel", 411,
  NumberBoxModel(&s_bridge_channel, 1, 13, 1, "ch", nullptr, [](int32_t v){ applyEspChannel((uint8_t)v); })
);
```

3) Crit auto‑shutdown (single‑item checkbox‑style normal submenu)
```
static MenuItem powerCritShutdownToggleMenuItems[] = {
  {"[ ] Auto: OFF", nullptr, 0, 858, toggleCritShutdownWrapper},
  {"Back",          nullptr, 0, 859, Utilities::returnToPreviousMenu}
};

static void refreshCritShutdownToggleLabel() {
  PowerSettings ps = PowerSettingsStore::get();
  bool enabled = (ps.disableCritShutdown == 0);
  powerCritShutdownToggleMenuItems[0].name = String(enabled ? "[x] " : "[ ] ") + "Auto: " + (enabled ? "ON" : "OFF");
}
```

4) IMU Quaternion (normal menu with radio markers, persisted to BridgeSettings)
```
static void setQuatAhrs(){ gQuatSource = QuatSource::AHRS; BridgeSettings s; BridgeSettingsStore::load(s); s.imuQuatMode = 0; BridgeSettingsStore::save(s); refreshImuQuatMarkers(); oledDisplay.updateMenu(); }
static void setQuatOnChip(){ gQuatSource = QuatSource::ONCHIP; BridgeSettings s; BridgeSettingsStore::load(s); s.imuQuatMode = 1; BridgeSettingsStore::save(s); refreshImuQuatMarkers(); oledDisplay.updateMenu(); }
```

5) Crit flash rate (NumberPicker in Hz×10 scaled units)
```
static int32_t s_critFlashHz10 = 40; // 4.0 Hz
static MenuItem critFlashRate_np = makeNumberPickerItem(
  "Crit flash rate", 845,
  NumberPickerModel(&s_critFlashHz10, 5, 100, 1, "Hz", nullptr, onCritHzCommit)
);
```

Text size
- NumberBox, RadioBox, CheckBox, and NumberPicker support an optional per‑control textSize; default matches normal UI (1×).
- Use larger sizes sparingly to avoid line wrapping or clipping.

Testing checklist
- Build and flash; navigate to each control:
  - NumberBox: values clamp to [min,max]; Enter commits; Back cancels.
  - RadioBox: onPreOpen picks up the latest store value; Enter commits.
  - CheckBox: onPreOpen syncs; Enter toggles; onCommit persists; immediate read‑back matches saved value.
  - NumberPicker: per‑digit editing works with carries; Back commits; range clamped.
- Normal menu markers update immediately after selection; re‑entering shows correct selection; settings persist across reboot.

Gotchas and tips
- Avoid NVS reads in loops. Use onPreOpen to sync right before showing an editor, or refresh markers when entering a submenu.
- When mixing normal menu markers and editors for the same setting, always refresh labels after persisting (call oledDisplay.updateMenu()).
- ASCII glyphs only: OLED’s default font does not render Unicode bullets. Use "(o)" and "( )"; for toggles use "[x]" / "[ ]".

---

Where to look in the code
- include/OLED_Display.h — models and MenuItemKind
- src/OLED_Display.cpp — renderer and button handling for the editors
- src/MenuConfig.cpp — concrete usage patterns, factories, and marker helpers
- include/PowerSettings.h, src/PowerSettings.cpp — power settings store
- include/BridgeSettings.h, src/BridgeSettings.cpp — bridge/runtime settings store
