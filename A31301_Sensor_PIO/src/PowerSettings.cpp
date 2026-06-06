#include "PowerSettings.h"
#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>

namespace {
  const char* NS = "power";
  const char* K_LOW_PCT   = "lowPct";
  const char* K_CRIT_PCT  = "critPct";
  const char* K_LBI_MS    = "lowBInt";
  const char* K_LBW_MS    = "lowBWid";
  const char* K_CFP_MS    = "critPer";
  const char* K_CW_MS     = "critWin";
  const char* K_LB_COUNT  = "lowBCnt";
  const char* K_NO_CRIT   = "noCrit";

  PowerSettings gS{};
  bool gInit = false;

  static inline int clampInt(int v, int lo, int hi){ if (v < lo) return lo; if (v > hi) return hi; return v; }
  static inline uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi){ if (v < lo) return lo; if (v > hi) return hi; return v; }

  void clampNormalize(PowerSettings& s) {
    s.critPct = clampInt(s.critPct, 0, 99);
    s.lowPct  = clampInt(s.lowPct,  1, 100);
    if (s.lowPct <= s.critPct) s.lowPct = (s.critPct < 100) ? (s.critPct + 1) : 100;

    s.lowBlinkIntervalMs = clampU32(s.lowBlinkIntervalMs, 1000u, 60000u);
    s.lowBlinkWidthMs    = clampU32(s.lowBlinkWidthMs,    50u,   2000u);
    s.critFlashPeriodMs  = clampU32(s.critFlashPeriodMs,  50u,   2000u);
    s.critWindowMs       = clampU32(s.critWindowMs,       1000u, 60000u);
    if (s.lowBlinkCount < 1) s.lowBlinkCount = 1; else if (s.lowBlinkCount > 3) s.lowBlinkCount = 3;
    s.disableCritShutdown = s.disableCritShutdown ? 1 : 0;
  }

  PowerSettings defaults() {
    PowerSettings d{
      SPS_LOW_BATT_PERCENT,
      SPS_CRIT_BATT_PERCENT,
      SPS_LOW_BLINK_INTERVAL_MS,
      SPS_LOW_BLINK_WIDTH_MS,
      SPS_CRIT_FLASH_PERIOD_MS,
      SPS_CRIT_WINDOW_MS,
      (uint8_t)SPS_LOW_BLINK_COUNT,
      (uint8_t)0 // disableCritShutdown default OFF
    };
    clampNormalize(d);
    return d;
  }

  void loadNvs() {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) { gS = defaults(); return; }

    gS = defaults();
    int32_t i32; uint32_t u32;
    if (nvs_get_i32(h, K_LOW_PCT, &i32) == ESP_OK)  gS.lowPct = (int)i32;
    if (nvs_get_i32(h, K_CRIT_PCT, &i32) == ESP_OK) gS.critPct = (int)i32;
    if (nvs_get_u32(h, K_LBI_MS, &u32) == ESP_OK)   gS.lowBlinkIntervalMs = u32;
    if (nvs_get_u32(h, K_LBW_MS, &u32) == ESP_OK)   gS.lowBlinkWidthMs = u32;
    if (nvs_get_u32(h, K_CFP_MS, &u32) == ESP_OK)   gS.critFlashPeriodMs = u32;
    if (nvs_get_u32(h, K_CW_MS, &u32) == ESP_OK)    gS.critWindowMs = u32;
    // lowBlinkCount stored as u8
    uint8_t u8 = 0;
    size_t sz = sizeof(u8);
    if (nvs_get_blob(h, K_LB_COUNT, &u8, &sz) == ESP_OK && sz == sizeof(u8)) gS.lowBlinkCount = u8; else gS.lowBlinkCount = 1;

    // disableCritShutdown stored as u8 (0/1), default 0
    uint8_t noCrit = 0;
    if (nvs_get_u8(h, K_NO_CRIT, &noCrit) == ESP_OK) gS.disableCritShutdown = noCrit ? 1 : 0; else gS.disableCritShutdown = 0;

    clampNormalize(gS);
    nvs_close(h);
  }

  void saveNvs(const PowerSettings& s) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, K_LOW_PCT,  (int32_t)s.lowPct);
    nvs_set_i32(h, K_CRIT_PCT, (int32_t)s.critPct);
    nvs_set_u32(h, K_LBI_MS,   s.lowBlinkIntervalMs);
    nvs_set_u32(h, K_LBW_MS,   s.lowBlinkWidthMs);
    nvs_set_u32(h, K_CFP_MS,   s.critFlashPeriodMs);
    nvs_set_u32(h, K_CW_MS,    s.critWindowMs);
    // store lowBlinkCount as a single byte blob
    uint8_t u8 = s.lowBlinkCount;
    nvs_set_blob(h, K_LB_COUNT, &u8, sizeof(u8));
    // store disableCritShutdown as u8 (0/1)
    uint8_t noCrit = s.disableCritShutdown ? 1 : 0;
    nvs_set_u8(h, K_NO_CRIT, noCrit);
    nvs_commit(h);
    nvs_close(h);
  }
}

namespace PowerSettingsStore {
  void begin() {
    if (gInit) return;
    // Initialize NVS (idempotent); safe if already called elsewhere
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      nvs_flash_init();
    }
    loadNvs();
    gInit = true;
  }

  PowerSettings get() { return gS; }

  void set(const PowerSettings& in) { PowerSettings s = in; clampNormalize(s); gS = s; saveNvs(gS); }

  void resetDefaults() { gS = defaults(); saveNvs(gS); }
}
