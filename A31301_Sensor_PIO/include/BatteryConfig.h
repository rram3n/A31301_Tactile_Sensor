#pragma once
#include <stdint.h>

namespace BatteryConfig {
  // Initialize NVS if needed (NVS is already initialized in InitTask in main.cpp)
  void initNVS();

  // Returns one of {450, 520, 660}; default 520 if not set
  int  getCapacityMah();

  // Persist capacity (will be clamped to {450,520,660})
  void setCapacityMah(int cap_mAh);
}
