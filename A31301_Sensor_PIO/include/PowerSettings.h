#pragma once
#include <stdint.h>

// Defaults sourced from existing macros to preserve current behavior if settings are unset
#ifndef SPS_LOW_BATT_PERCENT
#define SPS_LOW_BATT_PERCENT 10
#endif
#ifndef SPS_CRIT_BATT_PERCENT
#define SPS_CRIT_BATT_PERCENT 0
#endif
#ifndef SPS_LOW_BLINK_INTERVAL_MS
#define SPS_LOW_BLINK_INTERVAL_MS 3000u
#endif
#ifndef SPS_LOW_BLINK_WIDTH_MS
#define SPS_LOW_BLINK_WIDTH_MS 250u
#endif
#ifndef SPS_CRIT_FLASH_PERIOD_MS
#define SPS_CRIT_FLASH_PERIOD_MS 250u
#endif
#ifndef SPS_CRIT_WINDOW_MS
#define SPS_CRIT_WINDOW_MS 5000u
#endif
#ifndef SPS_LOW_BLINK_COUNT
#define SPS_LOW_BLINK_COUNT 1
#endif

struct PowerSettings {
  int      lowPct;               // 1..100, must be > critPct
  int      critPct;              // 0..99
  uint32_t lowBlinkIntervalMs;   // 1000..60000
  uint32_t lowBlinkWidthMs;      // 50..2000 (also used as ON gap between pulses)
  uint32_t critFlashPeriodMs;    // 50..2000
  uint32_t critWindowMs;         // 1000..60000
  uint8_t  lowBlinkCount;        // 1..3 pulses per interval
  uint8_t  disableCritShutdown;  // 0=false (default), 1=true → never auto-shutdown on critical battery
};

namespace PowerSettingsStore {
  // Initialize NVS and load settings (idempotent)
  void begin();
  // Get a copy of current settings (thread-safe snapshot semantics sufficient for our use)
  PowerSettings get();
  // Set settings (clamped+normalized) and persist to NVS
  void set(const PowerSettings& s);
  // Restore default values and save to NVS
  void resetDefaults();
}
