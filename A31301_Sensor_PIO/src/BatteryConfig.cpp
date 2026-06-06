#include "BatteryConfig.h"
#include <nvs_flash.h>
#include <nvs.h>

namespace {
  constexpr const char* NS  = "battery";
  constexpr const char* KEY = "cap";
  constexpr int DEFAULT_CAP = 520;

  inline int canonical(int v) {
    // map any value to the nearest allowed option
    if (v <= 485) return 450;
    if (v >= 635) return 660;
    return 520;
  }
}

void BatteryConfig::initNVS() {
  // No-op: NVS is initialized in InitTask (main.cpp)
}

int BatteryConfig::getCapacityMah() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
  if (err != ESP_OK) return DEFAULT_CAP;

  int32_t val = DEFAULT_CAP;
  err = nvs_get_i32(h, KEY, &val);
  nvs_close(h);
  if (err != ESP_OK) return DEFAULT_CAP;
  return canonical(static_cast<int>(val));
}

void BatteryConfig::setCapacityMah(int cap_mAh) {
  cap_mAh = canonical(cap_mAh);
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_i32(h, KEY, cap_mAh);
  nvs_commit(h);
  nvs_close(h);
}
