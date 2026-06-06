#pragma once
#include "BatterySensor.h"

namespace BatteryBackground {
  // Start the background task (idempotent). Returns true if started or already running.
  bool start();

  // Returns true once the background task has produced at least one valid reading.
  bool isReady();

  // Copy the latest reading into out. Returns false if not ready yet.
  bool getLatest(BatteryReading& out);
}
