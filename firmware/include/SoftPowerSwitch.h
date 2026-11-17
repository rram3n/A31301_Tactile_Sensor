#ifndef SoftPowerSwitch_h
#define SoftPowerSwitch_h

#include <Arduino.h>

// SoftPowerSwitch configuration macros (override via -D at build time if desired)
#ifndef SPS_LOW_BLINK_INTERVAL_MS
#define SPS_LOW_BLINK_INTERVAL_MS   3000u   // 3 s between low-battery reminders
#endif
#ifndef SPS_LOW_BLINK_WIDTH_MS
#define SPS_LOW_BLINK_WIDTH_MS      250u     // 0.25 s dark pulse width
#endif
#ifndef SPS_CRIT_FLASH_PERIOD_MS
#define SPS_CRIT_FLASH_PERIOD_MS    250u     // 0.25 s toggle period during critical flash
#endif
#ifndef SPS_CRIT_WINDOW_MS
#define SPS_CRIT_WINDOW_MS          10000u    // 5 s flashing window before shutdown
#endif
#ifndef SPS_MONITOR_PERIOD_MS
#define SPS_MONITOR_PERIOD_MS       20u      // monitor task loop period (ms)
#endif
#ifndef SPS_TASK_NAME
#define SPS_TASK_NAME               "PowerBattMon"
#endif
#ifndef SPS_TASK_STACK
#define SPS_TASK_STACK              4096     // bytes
#endif
#ifndef SPS_TASK_PRIORITY
#define SPS_TASK_PRIORITY           1
#endif
#ifndef SPS_TASK_CORE
#define SPS_TASK_CORE               0        // core 0
#endif

// Battery percentage thresholds (override via -D at build time)
#ifndef SPS_LOW_BATT_PERCENT
#define SPS_LOW_BATT_PERCENT        15       // < low threshold triggers periodic blink
#endif
#ifndef SPS_CRIT_BATT_PERCENT
#define SPS_CRIT_BATT_PERCENT       5        // <= critical threshold triggers 5s flash then shutdown
#endif

class SoftPowerSwitch {
public:
    SoftPowerSwitch(int powerLedPin, int powerGatePin, int powerSwitchPin);
    void setup();
    bool switch_power(); // Method to handle power switching logic
    void shutdown();

    // Start/stop battery monitoring (idempotent start)
    void beginMonitoring();

private:
    // Background monitor task
    static void BatteryMonitorTask(void* pv);

    // Helpers for LED control
    void ledOn();
    void ledOff();
    void ledToggle();

    // Pins/state
    int powerLedPin;
    int powerGatePin;
    int powerSwitchPin;
    bool lastSwitchState;

    // Task handle (singleton across any instances)
    static TaskHandle_t sTask;

    // State machine for battery indications
    enum class Mode : uint8_t { Normal, LowBlinkIdle, LowBlinkPulse, CriticalFlash, ShuttingDown };
    static volatile Mode sMode;

    // Timing
    static volatile uint32_t sLastLowBlinkMs;   // last time we did a low-battery blink
    static volatile uint32_t sPulseEndMs;       // when to end current LED pulse (low-batt single-blink or critical ON/OFF step)
    static volatile uint32_t sCriticalEndMs;    // when 5-second critical window ends
    static volatile bool     sLedState;         // cached LED state for toggling
    static volatile uint8_t  sRemainPulses;     // remaining pulses in current low-battery group
    static volatile bool     sInOffPhase;       // true when LED is in OFF part of a pulse
};

#endif
