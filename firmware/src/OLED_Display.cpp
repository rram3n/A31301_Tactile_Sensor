#include "OLED_Display.h"
#include <limits.h>

// Simple edit state for in-place editors (NumberBox/RadioBox)
struct EditState { bool active = false; int radioCursor = 0; };
static EditState g_edit; // file-scope so all handlers can access

// Coalesce overlay redraws when a page is open to reduce flicker
static uint32_t s_lastOverlayDrawMs = 0;
static uint32_t s_lastOverlayInfoDrawMs = 0;

// File-scope helpers to avoid lambda-capture issues inside button handlers
static void s_drawNumberBox(OLED_Display* self, const String& title, int32_t val, const char* unit, uint8_t textSize) {
    if (!self) return;
    self->beginPage(OLED_Display::PageStyle::WithOverlay);
    self->setCursor(0, self->rowY(0)); self->print(title);
    self->setCursor(0, self->rowY(2)); self->setTextSize(textSize, textSize);
    // Render with simple ASCII arrows around the value: up arrow before, down arrow after
    // Example: "^ 12% v"
    self->print(String("^ "));
    self->print(String(val));
    if (unit && unit[0]) { self->print(String(unit)); }
    self->print(String(" v"));
    self->setTextSize(1,1);
    self->endPage();
}

static void s_drawCheckBox(OLED_Display* self, const String& title, bool checked, uint8_t textSize) {
    if (!self) return;
    self->beginPage(OLED_Display::PageStyle::WithOverlay);
    self->setCursor(0, self->rowY(0)); self->print(title);
    self->setCursor(0, self->rowY(2)); self->setTextSize(textSize, textSize);
    self->print(checked ? "[x] ON" : "[ ] OFF");
    self->setTextSize(1,1);
    self->endPage();
}

static void s_renderRadioList(OLED_Display* self, const String& title, const RadioBoxModel& rb, int& selIdx) {
    if (!self) return;
    const int n = rb.optionCount;
    const int maxN = (n < 8) ? n : 8; // cap
    String items[8];
    for (int i = 0; i < maxN; ++i) {
        bool isSet = (rb.selected && *rb.selected == rb.options[i].value);
        // Use ASCII-friendly markers to ensure proper rendering on OLED
        items[i] = String(isSet ? "(o) " : "( ) ") + rb.options[i].label;
    }
    // Temporarily render list at requested text size; fall back to 1 if invalid
    uint8_t ts = rb.textSize ? rb.textSize : 1;
    if (ts != 1) self->setTextSize(ts, ts);
    self->renderList(title, "", items, maxN, selIdx, &selIdx);
    if (ts != 1) self->setTextSize(1, 1);
}

// ---------------- NumberPicker helpers/state -----------------
struct PickerState {
    bool active = false;        // inside NumberPicker editor
    bool editingDigit = false;  // false=select digit, true=edit digit
    int cursor = 0;             // 0..totalDigits-1 over digits-only string (no sign/dot)
    int totalDigits = 0;        // cached last digit count
};
static PickerState g_picker;

// Build a fixed-point string from scaled value and also produce a digits-only string
static void np_formatScaled(int32_t valueScaled,
                            uint8_t decimals,
                            uint8_t minIntDigits,
                            bool padFraction,
                            String& outText,
                            String& outDigitsOnly) {
    bool neg = (valueScaled < 0);
    int32_t v = neg ? -valueScaled : valueScaled;
    int32_t pow10 = 1;
    for (uint8_t i = 0; i < decimals; ++i) pow10 *= 10;
    int32_t intr = (decimals ? v / pow10 : v);
    int32_t frac = (decimals ? v % pow10 : 0);

    // integer part
    char ibuf[24]; snprintf(ibuf, sizeof(ibuf), "%ld", (long)intr);
    String iStr(ibuf);
    while ((int)iStr.length() < (int)minIntDigits) iStr = String("0") + iStr;

    // fractional part
    String fStr;
    if (decimals) {
        char fbuf[24];
        // zero-padded to decimals
        snprintf(fbuf, sizeof(fbuf), "%0*ld", (int)decimals, (long)frac);
        fStr = String(fbuf);
        if (!padFraction) {
            while (fStr.length() && fStr[fStr.length()-1] == '0') fStr.remove(fStr.length()-1);
        }
    }

    outDigitsOnly = iStr + fStr;
    outText = String(neg ? "-" : "");
    outText += iStr;
    if (decimals) { outText += "."; outText += fStr; }
}

// Compute per-digit multipliers (scaled units) for each position in digits-only string
static void np_placeMultipliers(int digitsCount, std::vector<int32_t>& mulOut) {
    mulOut.clear(); mulOut.reserve(digitsCount);
    int32_t p = 1;
    // Rightmost digit has multiplier 1; build reverse then reverse the array
    for (int i = 0; i < digitsCount; ++i) { mulOut.push_back(0); }
    p = 1;
    for (int idx = digitsCount - 1; idx >= 0; --idx) {
        mulOut[idx] = p;
        // avoid overflow for very long numbers (we only expect up to ~6 digits)
        if (p <= INT32_MAX / 10) p *= 10;
    }
}

// Draw NumberPicker with only the selected digit inverted (white bg, black fg) and mode hint
static void s_drawNumberPicker(OLED_Display* self,
                               const String& title,
                               const NumberPickerModel& np,
                               int cursor,
                               bool editingDigit) {
    if (!self || !np.valueScaled) return;
    String text, digits;
    np_formatScaled(*np.valueScaled, np.decimals, np.minIntDigits, np.padFraction, text, digits);
    // Update cached total digits for navigation
    g_picker.totalDigits = digits.length();
    if (g_picker.totalDigits <= 0) { g_picker.totalDigits = 1; }
    if (cursor >= g_picker.totalDigits) cursor = g_picker.totalDigits - 1;
    if (cursor < 0) cursor = 0;
    // Persist clamped cursor so subsequent handlers don't access out of range
    g_picker.cursor = cursor;

    self->beginPage(OLED_Display::PageStyle::WithOverlay);
    self->setCursor(0, self->rowY(0)); self->print(title);

    uint8_t ts = np.textSize ? np.textSize : 1;
    self->setTextSize(ts, ts);
    const int yVal = self->rowY(2);
    self->setCursor(0, yVal);
    // Render full value (normal colors)
    self->print(text);
    if (np.unit && np.unit[0]) { self->print(String(" ") + np.unit); }

    // Invert only the currently selected digit in-place
    const int charW = 6 * ts; // 6px wide at size 1
    // Determine x position of selected digit in the text (skip '-' and '.')
    int digitIdx = 0; int selCol = -1; char selChar = '0';
    for (int i = 0; i < (int)text.length(); ++i) {
        char c = text[i];
        if (c == '-' || c == '.') continue;
        if (digitIdx == cursor) { selCol = i; selChar = c; break; }
        digitIdx++;
    }
    if (selCol >= 0) {
        int x = selCol * charW;
        // Fill background white over the digit cell, then draw the digit in black
        self->fillRect(x, yVal, charW, 8 * ts, SSD1306_WHITE);
        self->setTextColorMono(SSD1306_BLACK);
        self->setCursor(x, yVal);
        self->print(String(selChar));
        // Restore default color
        self->setTextColorMono(SSD1306_WHITE);
    }

    // Hint line
    self->setCursor(0, self->rowY(4));
    self->print(editingDigit ? "Edit: Up/Down change, Enter=OK, Back=Save"
                             : "Select digit: Up/Down, Enter=Edit, Back=Save");
    self->endPage();
}

// Constructor: Initializes the OLED display with given parameters
OLED_Display::OLED_Display(int8_t oled_reset, uint8_t screen_address,
                           uint8_t screen_width, uint8_t screen_height) 
    : display(screen_width, screen_height, &Wire, oled_reset),
      oled_reset(oled_reset), screen_address(screen_address),
      screen_width(screen_width), screen_height(screen_height),
      currentMenu(nullptr), selected(0), submenuSize(0) {
    // Initialize state and stateinfo if needed
    currentState = "";
    currentStateInfo = "";
}

// Begin method: Initializes the display and sets up default button actions
void OLED_Display::begin(uint8_t rotation, uint8_t text_size, uint16_t text_color,
                         int upPin, int downPin, int enterPin, int backPin, int centerPin) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, screen_address)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); // Don't proceed, loop forever
    }
    display.setRotation(rotation);
    display.clearDisplay();
    display.setTextSize(text_size);
    display.setTextColor(text_color);
    display.cp437(true);

    // Ensure overlay area is reserved from the start
    setOverlayHeightPx(16);
    
    // --- Helper lambdas replaced with static functions at top of file scope below ---

    // Define default button actions for menu navigation
    // Up Button
    buttonActions.push_back(ButtonAction{
        upPin,
        [this]() {
            if (!(currentMenu && currentMenu->submenu && selected < submenuSize)) { return; }
            MenuItem& it = currentMenu->submenu[selected];
            if (g_picker.active && it.kind == MenuItemKind::NumberPicker && it.np.valueScaled) {
                // In picker: Up moves cursor left in browse, or increments digit in edit
                String t, d; np_formatScaled(*it.np.valueScaled, it.np.decimals, it.np.minIntDigits, it.np.padFraction, t, d);
                std::vector<int32_t> mul; np_placeMultipliers(d.length(), mul);
                if (!g_picker.editingDigit) {
                    if (d.length() > 0) {
                        g_picker.cursor = (g_picker.cursor > 0) ? (g_picker.cursor - 1) : ((int)d.length() - 1);
                    }
                } else {
                    if (!mul.empty()) {
                        // Defensive clamp in case digit count shrank
                        int maxIdx = (int)mul.size() - 1;
                        if (g_picker.cursor > maxIdx) g_picker.cursor = maxIdx;
                        if (g_picker.cursor < 0) g_picker.cursor = 0;
                        int32_t delta = mul[g_picker.cursor];
                        int32_t after = *it.np.valueScaled + delta;
                        if (after > it.np.maxScaled) after = it.np.maxScaled;
                        *it.np.valueScaled = after;
                        if (it.np.onChange) it.np.onChange(after);
                    }
                }
                s_drawNumberPicker(this, it.name, it.np, g_picker.cursor, g_picker.editingDigit);
                return;
            }
            if (g_edit.active && it.kind == MenuItemKind::NumberBox && it.nb.value) {
                int32_t v = *it.nb.value + (it.nb.step ? it.nb.step : 1);
                if (v > it.nb.maxVal) v = it.nb.maxVal;
                *it.nb.value = v;
                if (it.nb.onChange) it.nb.onChange(v);
                s_drawNumberBox(this, it.name, v, it.nb.unit, it.nb.textSize);
                return;
            }
            if (g_edit.active && it.kind == MenuItemKind::RadioBox && it.rb.options) {
                if (g_edit.radioCursor > 0) g_edit.radioCursor--;
                s_renderRadioList(this, it.name, it.rb, g_edit.radioCursor);
                return;
            }
            this->incrementSelection();
            this->updateMenu();
        }
    });
    // Down Button
    buttonActions.push_back(ButtonAction{
        downPin,
        [this]() {
            if (!(currentMenu && currentMenu->submenu && selected < submenuSize)) { return; }
            MenuItem& it = currentMenu->submenu[selected];
            if (g_picker.active && it.kind == MenuItemKind::NumberPicker && it.np.valueScaled) {
                String t, d; np_formatScaled(*it.np.valueScaled, it.np.decimals, it.np.minIntDigits, it.np.padFraction, t, d);
                std::vector<int32_t> mul; np_placeMultipliers(d.length(), mul);
                if (!g_picker.editingDigit) {
                    if (d.length() > 0) {
                        g_picker.cursor = (g_picker.cursor + 1 < (int)d.length()) ? (g_picker.cursor + 1) : 0;
                    }
                } else {
                    if (!mul.empty()) {
                        // Defensive clamp in case digit count shrank
                        int maxIdx = (int)mul.size() - 1;
                        if (g_picker.cursor > maxIdx) g_picker.cursor = maxIdx;
                        if (g_picker.cursor < 0) g_picker.cursor = 0;
                        int32_t delta = mul[g_picker.cursor];
                        int32_t after = *it.np.valueScaled - delta;
                        if (after < it.np.minScaled) after = it.np.minScaled;
                        *it.np.valueScaled = after;
                        if (it.np.onChange) it.np.onChange(after);
                    }
                }
                s_drawNumberPicker(this, it.name, it.np, g_picker.cursor, g_picker.editingDigit);
                return;
            }
            if (g_edit.active && it.kind == MenuItemKind::NumberBox && it.nb.value) {
                int32_t v = *it.nb.value - (it.nb.step ? it.nb.step : 1);
                if (v < it.nb.minVal) v = it.nb.minVal;
                *it.nb.value = v;
                if (it.nb.onChange) it.nb.onChange(v);
                s_drawNumberBox(this, it.name, v, it.nb.unit, it.nb.textSize);
                return;
            }
            if (g_edit.active && it.kind == MenuItemKind::RadioBox && it.rb.options) {
                if (g_edit.radioCursor + 1 < it.rb.optionCount) g_edit.radioCursor++;
                s_renderRadioList(this, it.name, it.rb, g_edit.radioCursor);
                return;
            }
            this->decrementSelection();
            this->updateMenu();
        }
    });
    // Enter Button
    buttonActions.push_back(ButtonAction{
        enterPin,
        [this]() {
            if (!(currentMenu && currentMenu->submenu && selected < submenuSize)) return;
            MenuItem& it = currentMenu->submenu[selected];
            switch (it.kind) {
                case MenuItemKind::Submenu:
                case MenuItemKind::Action: {
                    if (it.action) it.action();
                    if (it.submenu) {
                        MenuItem* submenu = new MenuItem{ it.name, it.submenu, it.submenuSize, it.uniqueId, it.action };
                        pushMenu(submenu);
                    }
                    break; }
                case MenuItemKind::NumberBox: {
                    if (g_edit.active) {
                        if (it.nb.onCommit && it.nb.value) it.nb.onCommit(*it.nb.value);
                        g_edit.active = false;
                        updateMenu();
                    } else {
                        g_edit.active = true;
                        int32_t v = it.nb.value ? *it.nb.value : 0;
                        s_drawNumberBox(this, it.name, v, it.nb.unit, it.nb.textSize);
                    }
                    break; }
                case MenuItemKind::CheckBox: {
                    // Just-in-time sync before opening/toggling
                    if (it.cb.onPreOpen) it.cb.onPreOpen();
                    if (it.cb.value) {
                        bool v = !(*it.cb.value);
                        *it.cb.value = v;
                        if (it.cb.onToggle) it.cb.onToggle(v);
                        if (it.cb.onCommit) it.cb.onCommit(v);
                        s_drawCheckBox(this, it.name, v, it.cb.textSize);
                    }
                    break; }
                case MenuItemKind::RadioBox: {
                    if (g_edit.active) {
                        if (it.rb.options && it.rb.selected) {
                            const RadioBoxOption& opt = it.rb.options[g_edit.radioCursor];
                            *it.rb.selected = opt.value;
                            if (it.rb.onCommit) it.rb.onCommit(*it.rb.selected);
                        }
                        g_edit.active = false;
                        updateMenu();
                    } else {
                        // Just-in-time sync before opening list
                        if (it.rb.onPreOpen) it.rb.onPreOpen();
                        g_edit.active = true;
                        g_edit.radioCursor = 0;
                        if (it.rb.options && it.rb.selected) {
                            for (int i = 0; i < it.rb.optionCount; ++i) {
                                if (*it.rb.selected == it.rb.options[i].value) { g_edit.radioCursor = i; break; }
                            }
                        }
                        s_renderRadioList(this, it.name, it.rb, g_edit.radioCursor);
                    }
                    break; }
                case MenuItemKind::NumberPicker: {
                    if (!g_picker.active) {
                        g_picker = PickerState{}; g_picker.active = true; g_picker.editingDigit = false; g_picker.cursor = 0;
                    } else {
                        // Toggle edit mode
                        g_picker.editingDigit = !g_picker.editingDigit;
                    }
                    s_drawNumberPicker(this, it.name, it.np, g_picker.cursor, g_picker.editingDigit);
                    break; }
            }
        }
    });
    // Back Button
    buttonActions.push_back(ButtonAction{
        backPin,
        [this]() {
            if (g_picker.active) {
                // Commit picker value on exit
                if (currentMenu && currentMenu->submenu && selected < submenuSize) {
                    MenuItem& it = currentMenu->submenu[selected];
                    if (it.kind == MenuItemKind::NumberPicker && it.np.onCommit && it.np.valueScaled) {
                        it.np.onCommit(*it.np.valueScaled);
                    }
                }
                g_picker = PickerState{};
                this->updateMenu();
                return;
            }
            if (g_edit.active) {
                g_edit.active = false; // cancel edit and redraw list
                this->updateMenu();
                return;
            }
            this->returnToPreviousMenu();
        }
    });
    // Center Button
    buttonActions.push_back(ButtonAction{
        centerPin,
        [this]() {
            Serial.println("Center button pressed!");
            this->displayInfo("Center Pressed");
            // Additional default action can be added here
        }
    });
    
    // Initialize button pins and states
    for (size_t i = 0; i < buttonActions.size(); ++i) {
        pinMode(buttonActions[i].pin, INPUT_PULLUP);
        lastStates.push_back(digitalRead(buttonActions[i].pin));
        lastButtonPressTime.push_back(0);
    }

    // Initial display of state and stateinfo
    displayState(currentState);
    displayStateInfo(currentStateInfo);
}

// Method to add custom button actions
void OLED_Display::addButtonAction(int pin, std::function<void()> action) {
    buttonActions.push_back(ButtonAction{pin, action});
    pinMode(pin, INPUT_PULLUP);
    lastStates.push_back(digitalRead(pin));
    lastButtonPressTime.push_back(0);
}

// Clears the OLED display
void OLED_Display::clearDisplay(){
    display.clearDisplay();
}

// Sets the text size (uniform scaling as per Adafruit_GFX)
void OLED_Display::setTextSize(uint8_t sizeX, uint8_t sizeY) {
    currentTextSizeX = sizeX;
    currentTextSizeY = sizeY;
    display.setTextSize(sizeX); // Adafruit_GFX only supports uniform scaling
}

// Displays text at specified coordinates
void OLED_Display::displayText(const String &text, int16_t x, int16_t y) {
    display.setCursor(x, y);
    display.println(text);
    show();
}

// Prints text without a newline
void OLED_Display::print(const String &text) {
    display.print(text);
}

// Prints text with a newline
void OLED_Display::println(const String &text) {
    display.println(text);
}

// Updates the OLED display with current buffer
void OLED_Display::show() {
    display.display();
}

// Sets the cursor position
void OLED_Display::setCursor(int16_t x, int16_t y) {
    display.setCursor(x, y);
}

// Minimal wrappers to operate on private Adafruit_SSD1306 from helpers
void OLED_Display::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    display.fillRect(x, y, w, h, color);
}

void OLED_Display::setTextColorMono(uint16_t color) {
    display.setTextColor(color);
}

// Overlay-aware helpers
void OLED_Display::beginPage(PageStyle style) {
    // For overlay pages, preserve the banner and only clear content area below it
    if (style == PageStyle::WithOverlay) {
        // Do not redraw overlay here; it persists
        // Clear only the area below the overlay to avoid ghosting
        display.fillRect(0, overlayHeightPx_, screen_width, screen_height - overlayHeightPx_, SSD1306_BLACK);
    } else {
        // Fullscreen: clear entire display buffer
        clearDisplay();
    }
    pageOpen_ = true;
}

void OLED_Display::endPage() {
    if (!pageOpen_) return;
    show();
    pageOpen_ = false;
}

void OLED_Display::setOverlayHeightPx(uint8_t px) { overlayHeightPx_ = px; }
uint8_t OLED_Display::getOverlayHeightPx() const { return overlayHeightPx_; }
int OLED_Display::rowY(int row) const { return (int)overlayHeightPx_ + row * 10; }

void OLED_Display::showStatusLines(const String& title, const String lines[], int lineCount) {
    beginPage(PageStyle::WithOverlay);
    setCursor(0, rowY(0));
    print(title);
    for (int i = 0; i < lineCount; ++i) {
        setCursor(0, rowY(1 + i));
        print(lines[i]);
    }
    endPage();
}

int OLED_Display::renderList(const String& title,
                             const String& subtitle,
                             const String items[],
                             int itemCount,
                             int selectedIndex,
                             int* outTopIndex) {
    beginPage(PageStyle::WithOverlay);

    // Headers
    setCursor(0, rowY(0)); print(title);
    setCursor(0, rowY(1)); print(subtitle);

    const int maxVisible = 6;
    int top = 0;
    if (selectedIndex >= maxVisible) top = selectedIndex - (maxVisible - 1);
    if (outTopIndex) *outTopIndex = top;

    for (int row = 0; row < maxVisible; ++row) {
        int idx = top + row; if (idx >= itemCount) break;
        setCursor(0, rowY(2 + row));
        if (idx == selectedIndex) {
            // Invert colors for the selected item (highlight)
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        print(items[idx]);
    }

    // Restore default text color
    display.setTextColor(SSD1306_WHITE);

    endPage();
    int drawn = itemCount - top; if (drawn < 0) drawn = 0; if (drawn > maxVisible) drawn = maxVisible;
    return drawn;
}

String OLED_Display::getCurrentMenuName() {
    if (currentMenu) {
        return currentMenu->name;
    }
    return "";
}


// Displays informational text at the top-right corner
void OLED_Display::displayInfo(const String &info) {
    int charWidth = 6; // Width of a character at text size 1
    int charHeight = 8; // Height of a character at text size 1

    // Calculate the width of the info text based on its length and current text size
    int infoWidth = info.length() * charWidth * currentTextSizeX;
    int infoHeight = charHeight * currentTextSizeY;

    // Define the x and y coordinates for the info area
    int16_t x = screen_width - infoWidth - 2; // 2 pixels padding from the right edge
    int16_t y = 0; // Top of the screen

    // Clear the previous info area by filling it with the background color
    // Assuming the background color is black
    display.fillRect(x, y, infoWidth + 4, infoHeight, SSD1306_BLACK); // Added padding to ensure complete clearing

    // Set text properties
    display.setTextColor(SSD1306_WHITE); // Set text color to white
    display.setCursor(x, y);
    display.setTextSize(currentTextSizeX); // Ensure text size is set correctly

    // Print the new info text
    display.println(info);
    show(); // Update the display with the new info
}

// **New Method Implementation**
// Displays a left-aligned string on the second line (overlay row 2)
void OLED_Display::displayState(const String &state) {
    const int charWidth  = 6; // Width of a character at text size 1
    const int charHeight = 8; // Height of a character at text size 1

    // Always reserve overlay height to protect content area below
    setOverlayHeightPx(16);

    // If unchanged, skip draw to avoid flicker
    if (state == currentState) {
        return;
    }

    // Optional: coalesce redraws while a page is open (>= 60 ms)
    uint32_t now = millis();
    if (pageOpen_ && (now - s_lastOverlayDrawMs) < 60) {
        // Accept new text but let endPage() flush once later
        currentState = state;
        return;
    }
    s_lastOverlayDrawMs = now;

    // Compute clear width using max(prev, new) to avoid ghosting when shrinking
    const String prev = currentState;
    const size_t maxChars = (state.length() > prev.length()) ? state.length() : prev.length();
    const int clearWidth = (int)maxChars * charWidth * currentTextSizeX + 4;

    // Update stored state AFTER computing prev length
    currentState = state;

    // Draw on second overlay line (y = 8 at size 1)
    const int16_t x = 0;                // Left-aligned
    const int16_t y = charHeight * currentTextSizeY; // Second line

    // Clear and draw
    display.fillRect(x, y, clearWidth, charHeight * currentTextSizeY, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    display.setTextSize(currentTextSizeX);
    display.println(state);

    // Only flush immediately if a page is not open; otherwise endPage() will flush once
    if (!pageOpen_) show();
}

// Displays a right-aligned string on the second line (overlay row 2, right-aligned)
void OLED_Display::displayStateInfo(const String &stateInfo) {
    const int charWidth  = 6; // Width of a character at text size 1
    const int charHeight = 8; // Height of a character at text size 1

    // If unchanged, skip draw to avoid flicker
    if (stateInfo == currentStateInfo) {
        return;
    }

    // Optional: coalesce redraws while a page is open (>= 60 ms)
    uint32_t now = millis();
    if (pageOpen_ && (now - s_lastOverlayInfoDrawMs) < 60) {
        currentStateInfo = stateInfo;
        return;
    }
    s_lastOverlayInfoDrawMs = now;

    // Compute clear width using max(prev, new) to avoid ghosting when shrinking
    const String prev = currentStateInfo;
    const size_t maxChars = (stateInfo.length() > prev.length()) ? stateInfo.length() : prev.length();
    const int infoWidth  = (int)maxChars * charWidth * currentTextSizeX;
    const int infoHeight = charHeight * currentTextSizeY;

    // Update stored info AFTER computing prev length
    currentStateInfo = stateInfo;

    // Right-aligned on the second overlay line
    const int16_t x = screen_width - infoWidth - 2;
    const int16_t y = charHeight * currentTextSizeY; // Second line

    // Clear and draw
    display.fillRect(x, y, infoWidth + 4, infoHeight, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x, y);
    display.setTextSize(currentTextSizeX);
    display.println(stateInfo);

    // Only flush immediately if no page is open
    if (!pageOpen_) show();
}
// **End of New Method Implementation**


// Sets the main menu
void OLED_Display::setMainMenu(MenuItem* menu) {
    menuStack.clear();
    currentMenu = menu;
    submenuSize = menu->submenuSize;
    selected = 0;
    updateMenu();
}

// Updates the OLED display to reflect the current menu
void OLED_Display::updateMenu() {
    clearDisplay();  // Clear the display for fresh output
    display.setTextColor(SSD1306_WHITE);
    displayText(currentMenu->name, 0, 0);  // Display the name of the current menu


    // Define the maximum number of items visible on the screen at a time
    const int maxVisibleItems = 6;
    int scrollOffset = 0;

    // Adjust scrollOffset so the selected item is always within the visible range
    if (selected >= scrollOffset + maxVisibleItems) {
        scrollOffset = selected - maxVisibleItems + 1;
    } else if (selected < scrollOffset) {
        scrollOffset = selected;
    }

    // Check for the existence of submenu items before attempting to display them
    if (currentMenu->submenu && submenuSize > 0) {
        for (int i = 0; i < maxVisibleItems && (scrollOffset + i) < submenuSize; i++) {
            int itemIndex = scrollOffset + i;
            if (itemIndex == selected) {
                display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);  // Highlight selected item
            } else {
                display.setTextColor(SSD1306_WHITE);  // Normal text for other items
            }
            displayText(currentMenu->submenu[itemIndex].name, 0, (i+2)*8);  // Position each item
        }
    }

    // **Redraw Persistent Elements**
    // Ensure that state and stateinfo are redrawn after updating the menu
    displayState(currentState);
    displayStateInfo(currentStateInfo);
    // **End of Redraw Persistent Elements**

    show();  // Make sure to update the display with new data
}

// Handles button presses and invokes corresponding actions
int OLED_Display::handleMenu() {
    int currentMenuId = -1;

    for (size_t i = 0; i < buttonActions.size(); ++i) {
        ButtonAction &ba = buttonActions[i];
        if (detectRisingEdge(ba.pin, lastStates[i], lastButtonPressTime[i])) {
            // Invoke the assigned action
            if (ba.action) {
                ba.action();
            }
        }
    }

    return currentMenuId;
}


void OLED_Display::returnToPreviousMenu() {
   popMenu();
}


// Detects a rising edge on a given pin with debouncing
bool OLED_Display::detectRisingEdge(int pin, int& lastState, unsigned long& lastPressTime) {
    int currentState = digitalRead(pin);
    unsigned long currentTime = millis();

    // Check if the time since last press is greater than debounceDelay
    if ((currentTime - lastPressTime) < debounceDelay) {
        return false;
    }

    bool rising = (currentState == HIGH && lastState == LOW);
    if (rising) {
        lastPressTime = currentTime;
    }
    lastState = currentState;
    return rising;
}

void OLED_Display::pushMenu(MenuItem* menu) {
    if (currentMenu != nullptr) {
        menuStack.push_back(currentMenu); // Push the current menu onto the stack
    }
    currentMenu = menu; // Set the new menu as the current menu
    submenuSize = menu->submenuSize;
    selected = 0;
    updateMenu();
}

void OLED_Display::popMenu() {
    if (!menuStack.empty()) {
        currentMenu = menuStack.back(); // Retrieve the last menu from the stack
        menuStack.pop_back();           // Remove it from the stack
        submenuSize = currentMenu->submenuSize;
        selected = 0;
        updateMenu();
    } else {
        Serial.println("OLED_Display: Menu stack is empty. Cannot pop menu.");
        // Optionally, you can set the main menu here or handle the underflow case
    }
}


OLED_Display::~OLED_Display() {
    // Iterate through the menu stack and delete each menu
    while (!menuStack.empty()) {
        MenuItem* menu = menuStack.back();
        menuStack.pop_back();
        if (menu->submenu) {
            delete[] menu->submenu; // Assuming submenu was allocated with new[]
        }
        delete menu;
    }

    // Delete the currentMenu if it's dynamically allocated
    // Assuming menus with uniqueId >= 500 are dynamic
    if (currentMenu && currentMenu->uniqueId >= 500) {
        if (currentMenu->submenu) {
            delete[] currentMenu->submenu;
        }
        delete currentMenu;
    }
}
