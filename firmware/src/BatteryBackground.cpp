#include <Arduino.h>
#include "BatteryBackground.h"
#include "BatteryFactory.h"
#include "Globals.h"

namespace {
  // Shared state
  static BatteryReading gLatest{};
  static volatile bool gReady = false;
  static TaskHandle_t gTask = nullptr;
  // Critical section for copying the shared reading
  static portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

  // 50 Hz nominal
  static constexpr uint32_t PERIOD_MS = 20;

  void BatteryTask(void* /*pv*/) {
    // Create and begin the concrete battery sensor for the active board
    IBatterySensor* batt = makeBatterySensor();
    if (batt) {
      batt->begin();
    }

    uint32_t last = millis();
    for (;;) {
      uint32_t now = millis();
      float dt = (now - last) / 1000.0f; if (dt <= 0) dt = PERIOD_MS / 1000.0f; last = now;

      if (batt) {
        batt->update(dt);
        BatteryReading r = batt->get();
        portENTER_CRITICAL(&gMux);
        gLatest = r;
        gReady = true;
        portEXIT_CRITICAL(&gMux);
      }

      vTaskDelay(pdMS_TO_TICKS(PERIOD_MS));
    }
  }
}

namespace BatteryBackground {

  bool start() {
    if (gTask != nullptr) return true;
    // Pin to core 0 (second core from Arduino loop perspective)
    BaseType_t ok = xTaskCreatePinnedToCore(
      BatteryTask,
      "BatteryTask",
      4096,
      nullptr,
      1,
      &gTask,
      0  // core 0
    );
    return ok == pdPASS;
  }

  bool isReady() {
    return gReady;
  }

  bool getLatest(BatteryReading& out) {
    if (!gReady) return false;
    portENTER_CRITICAL(&gMux);
    out = gLatest;
    portEXIT_CRITICAL(&gMux);
    return true;
  }
}
