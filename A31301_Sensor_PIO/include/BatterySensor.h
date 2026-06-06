#pragma once
#include <stdint.h>

struct BatteryReading {
  float voltage_V;   // instantaneous (or de-loaded) bus voltage
  float current_A;   // positive = discharge
  float soc_0_1;     // 0..1 (Gen1 derives from OCV)
  bool  atRest;      // true if rest condition detected (Gen2), else true
  bool  charging;    // true if charging is active (inferred if no CHG pin)
  float eta_min;     // minutes remaining: time-to-empty when discharging, time-to-full when charging; -1 if unknown
};

class IBatterySensor {
public:
  virtual ~IBatterySensor() {}
  virtual bool begin() = 0;
  virtual void update(float dt_s) = 0;   // call at ~50 Hz for Gen2; slower ok for Gen1
  virtual BatteryReading get() const = 0;
};
