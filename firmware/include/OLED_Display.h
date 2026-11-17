#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <functional>
#include <vector>

// New UI primitives for on-device settings editors
enum class MenuItemKind : uint8_t {
    Submenu = 0,   // default: existing behavior
    Action,        // leaf with callback
    NumberBox,     // bounded integer editor
    RadioBox,      // mutually exclusive choice set
    CheckBox,      // boolean toggle
    NumberPicker   // per-digit fixed-point editor
};

struct NumberBoxModel {
    int32_t* value = nullptr;
    int32_t minVal = 0;
    int32_t maxVal = 100;
    int32_t step   = 1;
    const char* unit = nullptr; // optional, e.g. "%" or "ch"
    std::function<void(int32_t)> onChange; // optional live notify
    std::function<void(int32_t)> onCommit; // persist on Enter (commit)
    uint8_t textSize = 1; // per-control text size (uniform scaling); default normal

    NumberBoxModel() = default;
    NumberBoxModel(int32_t* v,
                   int32_t minV,
                   int32_t maxV,
                   int32_t stp,
                   const char* u,
                   std::function<void(int32_t)> onCh,
                   std::function<void(int32_t)> onCm,
                   uint8_t ts = 1)
        : value(v), minVal(minV), maxVal(maxV), step(stp), unit(u), onChange(onCh), onCommit(onCm), textSize(ts) {}
};

struct RadioBoxOption {
    const char* label = "";
    int32_t value = 0;
    // Constructors to make initialization robust across toolchains
    RadioBoxOption() = default;
    RadioBoxOption(const char* lbl, int32_t v) : label(lbl), value(v) {}
};

struct RadioBoxModel {
    int32_t* selected = nullptr;                 // bound to live RAM field
    const RadioBoxOption* options = nullptr;     // array
    int optionCount = 0;
    std::function<void(int32_t)> onCommit;       // persist on Enter (commit)
    uint8_t textSize = 1;                        // per-control text size
    // Called right before the editor opens; use to sync bound value from live stores
    std::function<void()> onPreOpen;

    RadioBoxModel() = default;
    RadioBoxModel(int32_t* sel,
                  const RadioBoxOption* opts,
                  int count,
                  std::function<void(int32_t)> onCm,
                  uint8_t ts = 1,
                  std::function<void()> pre = nullptr)
        : selected(sel), options(opts), optionCount(count), onCommit(onCm), textSize(ts), onPreOpen(pre) {}
};

struct CheckBoxModel {
    bool* value = nullptr;                       // bound to live RAM field
    std::function<void(bool)> onToggle;          // optional live notify
    std::function<void(bool)> onCommit;          // persist when toggled
    uint8_t textSize = 1;                        // per-control text size
    // Called right before the editor opens; use to sync bound value from live stores
    std::function<void()> onPreOpen;

    CheckBoxModel() = default;
    CheckBoxModel(bool* v,
                  std::function<void(bool)> onT,
                  std::function<void(bool)> onC,
                  uint8_t ts = 1,
                  std::function<void()> pre = nullptr)
        : value(v), onToggle(onT), onCommit(onC), textSize(ts), onPreOpen(pre) {}
};

// NumberPicker: edit a fixed-point number by selecting digits and incrementing/decrementing them
struct NumberPickerModel {
    // Value is scaled by 10^decimals. Example: 2.5 with decimals=1 → valueScaled=25
    int32_t* valueScaled = nullptr;
    int32_t minScaled = 0;        // inclusive
    int32_t maxScaled = 0;        // inclusive
    uint8_t decimals = 0;         // number of fractional digits
    // Formatting
    uint8_t minIntDigits = 1;     // pad integer part with zeros
    bool padFraction = true;      // pad fractional part to decimals length
    const char* unit = nullptr;   // e.g., "Hz"
    std::function<void(int32_t)> onChange; // optional live notify (scaled units)
    std::function<void(int32_t)> onCommit; // persist when exiting via Back (scaled units)
    uint8_t textSize = 1;         // value rendering size

    NumberPickerModel() = default;
    NumberPickerModel(int32_t* vScaled,
                      int32_t minS,
                      int32_t maxS,
                      uint8_t dec,
                      const char* u,
                      std::function<void(int32_t)> onCh,
                      std::function<void(int32_t)> onCm,
                      uint8_t ts = 1,
                      uint8_t minDigits = 1,
                      bool padFrac = true)
        : valueScaled(vScaled), minScaled(minS), maxScaled(maxS), decimals(dec),
          minIntDigits(minDigits), padFraction(padFrac), unit(u),
          onChange(onCh), onCommit(onCm), textSize(ts) {}
};

// Structure to define a menu item
struct MenuItem {
    String name;              // Changed from String* to String
    MenuItem* submenu;
    int submenuSize;
    int uniqueId;
    std::function<void()> action;

    // New kind + payloads (ignored unless applicable). Defaults preserve old behavior.
    MenuItemKind kind = MenuItemKind::Submenu;
    NumberBoxModel nb{};   // used when kind == NumberBox
    RadioBoxModel rb{};    // used when kind == RadioBox
    CheckBoxModel cb{};    // used when kind == CheckBox
    NumberPickerModel np{}; // used when kind == NumberPicker

    // Convenience ctor for legacy-style items (name, submenu, size, id, action)
    MenuItem() = default;
    MenuItem(const String& n,
             MenuItem* sm,
             int smSize,
             int id,
             std::function<void()> act)
        : name(n), submenu(sm), submenuSize(smSize), uniqueId(id), action(act) {}
};

// Structure to map a button to its action
struct ButtonAction {
    int pin;
    std::function<void()> action;
};

class OLED_Display {
public:
    // Constructor with default OLED parameters
    OLED_Display(int8_t oled_reset = 11, uint8_t screen_address = 0x3C,
                uint8_t screen_width = 128, uint8_t screen_height = 64);
    
    // Initialize the OLED display and set up default buttons
    void begin(uint8_t rotation = 0, uint8_t text_size = 1, uint16_t text_color = SSD1306_WHITE,
               int upPin = 2, int downPin = 3, int enterPin = 4, int backPin = 5, int centerPin = 6);
    
    // Display control methods
    void clearDisplay();
    void displayText(const String &text, int16_t x, int16_t y);
    void print(const String &text);
    void println(const String &text);
    void show();
    void setCursor(int16_t x, int16_t y);
    // Minimal wrappers to allow helpers to perform limited drawing without
    // accessing the private Adafruit_SSD1306 directly.
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void setTextColorMono(uint16_t color);
    void setTextSize(uint8_t sizeX, uint8_t sizeY);
    void displayInfo(const String &info);
    
    // Overlay-aware page helpers
    enum class PageStyle : uint8_t { WithOverlay, Fullscreen };
    void beginPage(PageStyle style = PageStyle::WithOverlay);
    void endPage();
    void setOverlayHeightPx(uint8_t px);
    uint8_t getOverlayHeightPx() const;
    int rowY(int row) const;

    // Shows a simple status page under the overlay: title + lines
    void showStatusLines(const String& title, const String lines[], int lineCount);

    // Generic list renderer under the overlay; returns drawn count
    int renderList(const String& title,
                   const String& subtitle,
                   const String items[],
                   int itemCount,
                   int selectedIndex,
                   int* outTopIndex = nullptr);

    // Displays a left-aligned string on the second line
    void displayState(const String &state);
    
    // Displays a right-aligned string on the second line
    void displayStateInfo(const String &stateInfo);
    
    // Menu management methods
    void setMainMenu(MenuItem* menu);
    void updateMenu();
    void returnToPreviousMenu();
    int handleMenu();
    
    // Methods to manage menu navigation state
    MenuItem* getCurrentMenu() const { return currentMenu; }
    void setCurrentMenu(MenuItem* menu) { currentMenu = menu; }
    int getSelected() const { return selected; }
    void setSelected(int sel) { selected = sel; }
    int getSubmenuSize() const { return submenuSize; }
    void setSubmenuSize(int size) { submenuSize = size; }
    void incrementSelection() { selected++; if(selected >= submenuSize){selected = 0;}}
    void decrementSelection() { selected--; if (selected < 0) {selected = submenuSize - 1;} }

    // Method to add custom button actions
    void addButtonAction(int pin, std::function<void()> action);
    String getCurrentMenuName();
    void pushMenu(MenuItem* menu);
    void popMenu();
        bool detectRisingEdge(int pin, int& lastState, unsigned long& lastPressTime);

    ~OLED_Display(); // Destructor to clean up the menu stack

private:
    // Generalized rising edge detection with debouncing
    
    Adafruit_SSD1306 display;
    int8_t oled_reset;
    uint8_t currentTextSizeX = 1;
    uint8_t currentTextSizeY = 1;
    uint8_t screen_address;
    uint8_t screen_width;
    uint8_t screen_height;
    
    // Vector to hold all button actions (default and custom)
    std::vector<ButtonAction> buttonActions;
    
    // Tracking last states and press times for each button
    std::vector<int> lastStates;
    std::vector<unsigned long> lastButtonPressTime;
    std::vector<MenuItem*> menuStack; // Stack to hold the menu history
    
    // Menu navigation pointers
    MenuItem* currentMenu;
    int selected = 0;
    int submenuSize = 0;
    
    // Debounce parameters
    const unsigned long debounceDelay = 50; // milliseconds

    // Persistent banner/state
    String currentState = "";      // Stores the current state
    String currentStateInfo = "";  // Stores the current state info

    // Overlay/page helpers
    uint8_t overlayHeightPx_ = 16;  // Reserve two rows by default
    bool    pageOpen_ = false;
};

#endif // OLED_DISPLAY_H
