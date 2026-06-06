#include "SoftPowerSwitch.h"
#include "BatteryBackground.h"
#include "PowerSettings.h"

// Static storage
TaskHandle_t SoftPowerSwitch::sTask = nullptr;
volatile SoftPowerSwitch::Mode SoftPowerSwitch::sMode = SoftPowerSwitch::Mode::Normal;
volatile uint32_t SoftPowerSwitch::sLastLowBlinkMs = 0;
volatile uint32_t SoftPowerSwitch::sPulseEndMs = 0;
volatile uint32_t SoftPowerSwitch::sCriticalEndMs = 0;
volatile bool     SoftPowerSwitch::sLedState = true; // matches setup() initial HIGH
volatile uint8_t  SoftPowerSwitch::sRemainPulses = 0;
volatile bool     SoftPowerSwitch::sInOffPhase = false;

SoftPowerSwitch::SoftPowerSwitch(int powerLedPin, int powerGatePin, int powerSwitchPin)
    : powerLedPin(powerLedPin), powerGatePin(powerGatePin), powerSwitchPin(powerSwitchPin), lastSwitchState(LOW) {}
    // Initialize with the power switch assumed to be in the unpressed state

void SoftPowerSwitch::setup() {
    Serial.begin(115200);
    pinMode(powerGatePin, OUTPUT);
    digitalWrite(powerGatePin, HIGH); // Initially, power gate is HIGH
    pinMode(powerSwitchPin, INPUT_PULLUP);
    pinMode(powerLedPin, OUTPUT);
    digitalWrite(powerLedPin, HIGH); // Initially, red LED is ON (active HIGH)
    sLedState = true;
}

bool SoftPowerSwitch::switch_power() {
    bool currentSwitchState = digitalRead(powerSwitchPin);

    // Rising edge is detected here
    if (currentSwitchState == LOW && lastSwitchState == HIGH) { 
        // Flip the power gate state only when the button is released
        shutdown();
    }

    lastSwitchState = currentSwitchState; // Update the last switch state at the end of the function
    return currentSwitchState; // You can return the current state or a status flag based on your application needs
}

void SoftPowerSwitch::shutdown() {
    // Enter shutting down mode so the task stops blinking
    sMode = Mode::ShuttingDown;
    digitalWrite(powerGatePin, LOW);   // Turn off main power gate
    digitalWrite(powerLedPin, LOW);    // Ensure LED is OFF
    sLedState = false;
}

void SoftPowerSwitch::ledOn()  { digitalWrite(powerLedPin, HIGH); sLedState = true; }
void SoftPowerSwitch::ledOff() { digitalWrite(powerLedPin, LOW);  sLedState = false; }
void SoftPowerSwitch::ledToggle() { sLedState = !sLedState; digitalWrite(powerLedPin, sLedState ? HIGH : LOW); }

void SoftPowerSwitch::beginMonitoring() {
    if (sTask != nullptr) return; // already running

    // Start after BatteryBackground has started in InitTask; task will wait until ready
    auto taskThunk = [](void* pv){ BatteryMonitorTask(pv); };
    xTaskCreatePinnedToCore(
        taskThunk,
        SPS_TASK_NAME,
        SPS_TASK_STACK,
        this,
        SPS_TASK_PRIORITY,
        &sTask,
        SPS_TASK_CORE // run on core alongside BatteryBackground
    );
}

void SoftPowerSwitch::BatteryMonitorTask(void* pv) {
    SoftPowerSwitch* self = static_cast<SoftPowerSwitch*>(pv);

    // Wait until BatteryBackground has a valid reading
    while (!BatteryBackground::isReady()) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (;;) {
        // Read dynamic settings (updated live from Settings menu; defaults from macros)
        PowerSettings ps = PowerSettingsStore::get();
        const uint32_t kLowBlinkIntervalMs = ps.lowBlinkIntervalMs;
        const uint32_t kLowBlinkWidthMs    = ps.lowBlinkWidthMs;
        const uint32_t kCritFlashPeriodMs  = ps.critFlashPeriodMs;
        const uint32_t kCritWindowMs       = ps.critWindowMs;
        // If externally shut down, stop any further work
        if (sMode == Mode::ShuttingDown) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        BatteryReading r{};
        bool ok = BatteryBackground::getLatest(r);
        if (!ok) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        // Compute % safely; clamp to [0,100]
        int socPct = (int)roundf(fmaxf(0.0f, fminf(1.0f, r.soc_0_1)) * 100.0f);
        uint32_t now = millis();

        // If charging, completely suppress warnings and shutdown logic
        bool isCharging = r.charging;

        // Compute effective percentage thresholds from runtime settings and normalize
        int critPct = ps.critPct; if (critPct < 0) critPct = 0; if (critPct > 100) critPct = 100;
        int lowPct  = ps.lowPct;  if (lowPct  < 0) lowPct  = 0; if (lowPct  > 100) lowPct  = 100;
        if (lowPct <= critPct) {
            // Ensure low threshold is above critical to avoid overlap; disable low-blink window if misconfigured
            lowPct = critPct + 1; if (lowPct > 100) lowPct = 100;
        }

        // State transitions (gated by charging)
        if (isCharging) {
            // While charging: force Normal state (solid LED), cancel any blink/flash timers
            if (sMode != Mode::Normal) {
                sMode = Mode::Normal;
                self->ledOn();
            }
            // Reset timers so we don't immediately blink upon unplugging
            sLastLowBlinkMs = now;
            sPulseEndMs = now;
            sCriticalEndMs = now;
            sRemainPulses = 0;
            sInOffPhase = false;
        } else {
            if (socPct <= critPct && sMode != Mode::CriticalFlash) {
                // Enter critical flashing before shutdown (may be disabled via settings)
                sMode = Mode::CriticalFlash;
                sCriticalEndMs = now + kCritWindowMs;
                sPulseEndMs = now + kCritFlashPeriodMs; // first toggle at +250ms
                // Ensure LED has a known starting state (ON so toggles are visible)
                self->ledOn();
            } else if (socPct > critPct && socPct < lowPct && (sMode == Mode::Normal || sMode == Mode::LowBlinkIdle)) {
                // Normal low-battery reminder mode (single short blink every interval)
                sMode = Mode::LowBlinkIdle;
            } else if (socPct >= lowPct && sMode != Mode::Normal) {
                // Back to normal (no blinking)
                sMode = Mode::Normal;
                self->ledOn(); // keep power LED solid ON in normal state
            }
        }

        // If charging, suppress low-battery reminder blink (keep LED solid on) and suppress critical shutdown as well
        bool suppressLowBlink = isCharging;

        // State actions
        switch (sMode) {
            case Mode::Normal:
                // LED solid ON
                self->ledOn();
                break;

            case Mode::LowBlinkIdle:
                if (suppressLowBlink) {
                    self->ledOn();
                    break;
                }
                if (now - sLastLowBlinkMs >= kLowBlinkIntervalMs) {
                    // Start a group of short dark pulses
                    sLastLowBlinkMs = now;
                    uint8_t cnt = ps.lowBlinkCount;
                    if (cnt < 1) cnt = 1; else if (cnt > 3) cnt = 3;
                    sRemainPulses = cnt;
                    sInOffPhase = true;
                    self->ledOff();
                    sPulseEndMs = now + kLowBlinkWidthMs; // OFF duration
                    sMode = Mode::LowBlinkPulse;
                }
                break;

            case Mode::LowBlinkPulse:
                if (suppressLowBlink) {
                    self->ledOn();
                    sMode = Mode::LowBlinkIdle;
                    sRemainPulses = 0;
                    sInOffPhase = false;
                    break;
                }
                if ((int32_t)(now - sPulseEndMs) >= 0) {
                    if (sInOffPhase) {
                        // OFF phase ended -> turn ON for the inter-pulse gap
                        self->ledOn();
                        sInOffPhase = false;
                        sPulseEndMs = now + kLowBlinkWidthMs; // ON gap same as width
                    } else {
                        // ON gap ended -> one pulse completed
                        if (sRemainPulses > 0) sRemainPulses--;
                        if (sRemainPulses > 0) {
                            // Start next OFF pulse
                            self->ledOff();
                            sInOffPhase = true;
                            sPulseEndMs = now + kLowBlinkWidthMs;
                        } else {
                            // Sequence complete, ensure LED ON and return to idle
                            self->ledOn();
                            sMode = Mode::LowBlinkIdle;
                        }
                    }
                }
                break;

            case Mode::CriticalFlash:
                // If we recovered above thresholds, exit critical flashing without shutdown
                if (socPct > critPct) {
                    if (socPct < lowPct) {
                        sMode = Mode::LowBlinkIdle;
                    } else {
                        sMode = Mode::Normal;
                        self->ledOn();
                    }
                    break;
                }
                // Otherwise, keep flashing every critFlashPeriodMs
                if ((int32_t)(now - sPulseEndMs) >= 0) {
                    self->ledToggle();
                    sPulseEndMs = now + kCritFlashPeriodMs;
                }
                if ((int32_t)(now - sCriticalEndMs) >= 0) {
                    // End of critical window reached
                    if (ps.disableCritShutdown) {
                        // Never auto-shutdown: extend the flash window to keep warning visible
                        sCriticalEndMs = now + kCritWindowMs;
                        // Keep flashing until battery recovers above critPct
                    } else {
                        // Done flashing: shutdown power
                        self->shutdown();
                        // sMode will be ShuttingDown inside shutdown(), LED off
                    }
                }
                break;

            case Mode::ShuttingDown:
                // Nothing; power already cut
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(SPS_MONITOR_PERIOD_MS)); // ~50 Hz update cadence, configurable
    }
}
