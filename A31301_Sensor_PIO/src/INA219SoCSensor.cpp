#include "BatterySensor.h"
#include "Globals.h"
#include <Arduino.h>
#include <math.h>

#if defined(BOARD_GEN_2)

#include <Wire.h>
#include <Adafruit_INA219.h>
#include "BatteryConfig.h"

// ===== User Config =====
static const float RSHUNT  = 0.10f;             // ohms (0.05f if you change shunt)
static const bool  I_POSITIVE_IS_DISCHARGE = true;

// dV/dt thresholds (static)
static const float DVDT_ENTER = 0.001f;   // V/s
static const float DVDT_EXIT  = 0.002f;   // V/s

static const float OCV_V[11]   = {4.20f,4.10f,3.93f,3.83f,3.77f,3.70f,3.64f,3.58f,3.48f,3.40f,3.30f};
static const float OCV_SOC[11] = {100.0f,90.0f,80.0f,70.0f,60.0f,50.0f,40.0f,30.0f,20.0f,10.0f,0.0f};

static float interpOCVtoSOC(float v) {
  if (v >= OCV_V[0]) return 1.0f; if (v <= OCV_V[10]) return 0.0f;
  for (int i=0;i<10;i++) if (v <= OCV_V[i] && v >= OCV_V[i+1]) {
    float t = (v - OCV_V[i+1]) / (OCV_V[i] - OCV_V[i+1]);
    return (OCV_SOC[i+1] + t*(OCV_SOC[i] - OCV_SOC[i+1])) / 100.0f;
  }
  return 0.0f;
}

class INA219SoCSensor : public IBatterySensor {
  Adafruit_INA219 ina{0x40};
  BatteryReading r{};
  float soc = 1.0f, ema_I = 0.0f, rest_timer = 0.0f, last_V = 0.0f;
  bool  atRest = false;
  float Rint = 0.12f;   // initial internal resistance estimate
  bool  inited = false;

  // Low-voltage corrective pull guard
  float lv_timer_s = 0.0f;

  // Dynamic capacity + thresholds
  int   cap_mAh = 520;
  float i_rest  = (520/50.0f)/1000.0f;
  float i_exit  = (520/30.0f)/1000.0f;

  // Simple charging model state
  enum class ChgState : uint8_t { NoCharger, Charging_CC, Charging_CV, TopOff_Full };
  ChgState chgState = ChgState::NoCharger;
  float I_load_est = 0.15f; // A, estimated load (updated when not charging)

  // Boot timing for fast settle
  uint32_t boot_ms = 0;

  // Post-unplug relaxation tracking
  uint32_t unplug_ms = 0;     // timestamp of last unplug event
  bool     wasCharging = false;
  bool     postUnplugActive = false;

  void recomputeThresholds(int cap) {
    cap_mAh = cap;
    i_rest  = (cap_mAh/50.0f)/1000.0f;  // A
    i_exit  = (cap_mAh/30.0f)/1000.0f;  // A
  }
public:
  bool begin() override {
    Wire.begin(SDA_PIN, SCL_PIN);
    if (!ina.begin()) return false;

    // Use a preset for ranges; we will compute current from shunt voltage directly
    ina.setCalibration_32V_1A();

    // Apply stored capacity
    BatteryConfig::initNVS();
    recomputeThresholds(BatteryConfig::getCapacityMah());

    // Initial measurements and seed SoC from OCV
    float V_bus   = ina.getBusVoltage_V();
    float V_shunt = ina.getShuntVoltage_mV();
    float I       = (V_shunt / RSHUNT) / 1000.0f;   // A
    if (!I_POSITIVE_IS_DISCHARGE) I = -I;

    float I_for_deload = I;
    if (fabsf(I_for_deload) > i_exit) I_for_deload = (I_for_deload > 0 ? i_exit : -i_exit);
    float Vocv = V_bus + I_for_deload * Rint;

    soc    = interpOCVtoSOC(Vocv);
    last_V = V_bus;

    r = BatteryReading{V_bus, I, soc, false, false, -1.0f};
    inited = true;
    boot_ms = millis();
    return true;
  }

  void update(float dt_s) override {
    if (!inited) return;
    if (dt_s <= 0.0f) dt_s = 0.02f;

    // Pick up capacity changes at runtime
    int curCap = BatteryConfig::getCapacityMah();
    if (curCap != cap_mAh) recomputeThresholds(curCap);

    // Measurements
    float V_bus   = ina.getBusVoltage_V();    // V
    float V_shunt = ina.getShuntVoltage_mV(); // mV (can be negative on charge)

    // Compute current from shunt voltage for exact scaling regardless of library calibration
    // mA = mV / Ω => A = (mV / Ω) / 1000
    float I = (V_shunt / RSHUNT) / 1000.0f;   // A
    if (!I_POSITIVE_IS_DISCHARGE) I = -I;

    // 1) Maintain load estimate when not charging (using measured discharge current)
    // Update EMA for current and compute dV/dt
    const float kI = 0.2f; // ~50 Hz
    ema_I = (1.0f - kI)*ema_I + kI*I;
    float dVdt = (V_bus - last_V) / dt_s;
    last_V = V_bus;

    // Infer charging activity (no CHG pin)
    bool chgActive = false;
    #ifdef FORCE_CHG_ON_ZERO_I
      // Force mode: any near-zero current means "charging"
      chgActive = (fabsf(I) < i_exit);
    #else
      // Hysteresis based on V and dV/dt (safer default)
      // Rising voltage or near-top flat voltage with non-negative slope while measured I is near zero
      if (fabsf(I) < i_exit) {
        if (V_bus >= 4.15f) {
          chgActive = (dVdt > -0.0005f); // CV/top-off region
        } else if (V_bus >= 3.85f) {
          chgActive = (dVdt > 0.0010f);  // CC region requires clearer positive slope
        }
      }
    #endif

    // Update charge state machine
    switch (chgState) {
      case ChgState::NoCharger:
        if (chgActive) chgState = (V_bus >= 4.15f) ? ChgState::Charging_CV : ChgState::Charging_CC;
        break;
      case ChgState::Charging_CC:
        if (!chgActive) chgState = ChgState::NoCharger;
        else if (V_bus >= 4.15f) chgState = ChgState::Charging_CV;
        break;
      case ChgState::Charging_CV:
        if (!chgActive) {
          if (V_bus >= 4.18f && fabsf(dVdt) < 0.0005f) chgState = ChgState::TopOff_Full; else chgState = ChgState::NoCharger;
        }
        break;
      case ChgState::TopOff_Full:
        if (chgActive) chgState = ChgState::Charging_CV;
        else if (V_bus < 4.10f) chgState = ChgState::NoCharger;
        break;
    }

    // Detect unplug transition and start relaxation window
    {
      bool chargingNow = (chgState != ChgState::NoCharger);
      if (wasCharging && !chargingNow) {
        unplug_ms = millis();
        postUnplugActive = true;
      }
      wasCharging = chargingNow;
    }

    // Maintain load estimate only when not charging
    if (chgState == ChgState::NoCharger) {
      float I_disp = fmaxf(0.0f, I); // + = discharge
      const float kL = 0.02f;
      I_load_est = (1.0f - kL)*I_load_est + kL*I_disp;
      if (!isfinite(I_load_est)) I_load_est = 0.15f;
      if (I_load_est < 0.0f) I_load_est = 0.0f; if (I_load_est > 0.6f) I_load_est = 0.6f;
    }

    // 2) Effective current for integration (synthetic during charge)
    const float C_Ah = cap_mAh / 1000.0f;
    float I_eff = I; // default: measured
    float I_syn = 0.0f;
    const float I_charger_nom = 0.5f; // As requested
    if (chgState == ChgState::Charging_CC || chgState == ChgState::Charging_CV) {
      float I_syn_cc = fmaxf(0.0f, I_charger_nom - I_load_est);
      const float kcv = 5.0f; // A/V, slightly more conservative CV taper
      float I_syn_cv = fmaxf(0.0f, kcv * (4.20f - V_bus));
      I_syn = (chgState == ChgState::Charging_CC) ? I_syn_cc : I_syn_cv;
      I_eff = -I_syn; // charging → negative current for integration
    }

    // 3) Coulomb counting with I_eff
    float dSOC = -(I_eff * dt_s / 3600.0f) / C_Ah;
    soc = fminf(1.0f, fmaxf(0.0f, soc + dSOC));

    // 4) OCV correction at rest when not charging; fast settle right after boot
    bool maybeRest = (fabsf(ema_I) < i_rest) && (fabsf(dVdt) < DVDT_ENTER);
    bool exitRest  = (fabsf(ema_I) > i_exit) || (fabsf(dVdt) > DVDT_EXIT);
    if (!atRest) { rest_timer = maybeRest ? (rest_timer + dt_s) : 0.0f; if (rest_timer > 90.0f) atRest = true; }
    else if (exitRest) { atRest = false; rest_timer = 0.0f; }

    if (atRest && chgState == ChgState::NoCharger) {
      float Vocv = V_bus + ema_I * Rint;
      float soc_ocv = interpOCVtoSOC(Vocv);
      const float alpha = 0.2f;
      soc = (1.0f - alpha)*soc + alpha*soc_ocv;
    } else {
      uint32_t t_ms = millis();
      if (t_ms - boot_ms < 5000 && fabsf(ema_I) < i_exit) {
        float Vocv0 = V_bus + ema_I * Rint;
        float soc_ocv0 = interpOCVtoSOC(Vocv0);
        const float alpha_fast = 0.4f;
        soc = (1.0f - alpha_fast)*soc + alpha_fast*soc_ocv0;
      }
    }

    // 4b) Low-voltage corrective pull (even if not at rest), downward-only
    if (chgState == ChgState::NoCharger && V_bus <= 3.45f) {
      // Suppress aggressive pulls while a heavy load is actively dragging voltage down
      const float LV_LOAD_SUPPRESS_A = 0.20f; // ~2–3 W loads keep SoC correction off
      if (fabsf(ema_I) > LV_LOAD_SUPPRESS_A) {
        lv_timer_s = 0.0f;
      } else {
        lv_timer_s += dt_s;
      }

      if (lv_timer_s > 1.0f) { // require sustained low-V window without heavy load
        // Use EMA current limited by load suppression threshold to de-load voltage
        float I_for_deload = ema_I;
        if (fabsf(I_for_deload) > LV_LOAD_SUPPRESS_A)
          I_for_deload = (I_for_deload > 0 ? LV_LOAD_SUPPRESS_A : -LV_LOAD_SUPPRESS_A);
        float Vocv_lv = V_bus + I_for_deload * Rint;
        float soc_ocv_lv = interpOCVtoSOC(Vocv_lv);

        // Stronger correction the lower the voltage; fades by 3.45 V
        float kneeSpan = 3.45f - 3.30f; if (kneeSpan < 0.05f) kneeSpan = 0.05f;
        float t = (3.45f - V_bus) / kneeSpan; if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        float pull_rate_per_sec = 0.10f + 0.15f * t; // 0.10..0.25 per second
        float alpha_lv = pull_rate_per_sec * dt_s;
        if (alpha_lv > 0.25f) alpha_lv = 0.25f;

        if (soc > soc_ocv_lv) {
          soc = (1.0f - alpha_lv)*soc + alpha_lv*soc_ocv_lv;
        }
      }
    } else {
      lv_timer_s = 0.0f;
    }

    // Post-unplug relaxation: for ~2 minutes, pull SoC down toward OCV estimate (downward only)
    if (postUnplugActive) {
      const uint32_t RELAX_WINDOW_MS = 120000; // 2 minutes
      uint32_t now_ms = millis();
      uint32_t age = now_ms - unplug_ms;

      // Use de-loaded OCV estimate using limited EMA current
      float I_for_deload = ema_I;
      if (fabsf(I_for_deload) > i_exit)
        I_for_deload = (I_for_deload > 0 ? i_exit : -i_exit);
      float Vocv_relax = V_bus + I_for_deload * Rint;
      float soc_ocv_relax = interpOCVtoSOC(Vocv_relax);

      // Strong early pull decaying to zero by end of window
      float t = fminf(1.0f, age / float(RELAX_WINDOW_MS));
      float pull_rate_per_sec = (1.0f - t) * 0.30f; // 30%/s at start → 0%/s at end
      float alpha_relax = pull_rate_per_sec * dt_s;
      if (alpha_relax > 0.25f) alpha_relax = 0.25f;

      if (soc > soc_ocv_relax) {
        soc = (1.0f - alpha_relax) * soc + alpha_relax * soc_ocv_relax;
      }

      if (age >= RELAX_WINDOW_MS) postUnplugActive = false;
    }

    // 5) Gentle pull near full during CV/top-off
    if ((chgState == ChgState::Charging_CV || chgState == ChgState::TopOff_Full) && V_bus >= 4.18f) {
      const float PULL_RATE_PER_SEC = 0.03f; // s^-1, slightly reduced
      float alpha_pull = PULL_RATE_PER_SEC * dt_s;
      if (alpha_pull > 0.2f) alpha_pull = 0.2f; // safety clamp for unusually large dt
      soc += alpha_pull * (1.0f - soc);
      if (soc > 0.995f) soc = 0.995f; // cap while plugged
    }

    // 6) Rough ETA (minutes): time-to-full when charging, time-to-empty when discharging
    float eta_min = -1.0f;
    if (chgState == ChgState::Charging_CC) {
      float I_cc = fmaxf(1e-3f, fmaxf(0.0f, I_charger_nom - I_load_est));
      float hours = (1.0f - soc) * C_Ah / I_cc;
      eta_min = hours * 60.0f;
    } else if (chgState == ChgState::Charging_CV) {
      float I_cv_now = fmaxf(1e-3f, I_syn);
      float hours_coulomb = (1.0f - soc) * C_Ah / I_cv_now;
      // CV tail heuristic parameters: empirically derived for a 0.5 A charger.
      const float CV_TARGET_V = 4.20f;              // Target full-charge voltage
      const float CV_TAIL_BASE_MIN = 8.0f;          // Base overhead minutes once in CV near target
      const float CV_TAIL_SLOPE_MIN = 20.0f;        // Additional minutes when ~50 mV away from target
      const float CV_TAIL_HEADROOM_REF_MV = 50.0f;  // Reference headroom span for scaling
      float headroom_mV = fmaxf(0.0f, (CV_TARGET_V - V_bus) * 1000.0f);
      float tail_min = CV_TAIL_BASE_MIN + (CV_TAIL_SLOPE_MIN * (headroom_mV / CV_TAIL_HEADROOM_REF_MV)); // ~8–28 min for 0–50 mV headroom
      eta_min = hours_coulomb * 60.0f + tail_min;
    } else if (chgState == ChgState::TopOff_Full) {
      eta_min = 0.0f;
    } else if (chgState == ChgState::NoCharger) {
      // Discharging: runtime to empty based on SoC and estimated discharge current
      float I_discharge = fmaxf(5e-3f, I_load_est); // A; floor to avoid blowups
      float hours = (soc * C_Ah) / I_discharge;
      // Clamp to a reasonable upper bound (~45 days)
      if (!isfinite(hours) || hours < 0.0f) {
        eta_min = -1.0f;
      } else {
        const float MAX_MIN = 24.0f * 60.0f * 45.0f;
        float minutes = hours * 60.0f;
        if (minutes > MAX_MIN) minutes = MAX_MIN;
        if (minutes < 0.0f) minutes = 0.0f;
        eta_min = minutes;
      }
    }

    // Protective SoC floor clamps based on absolute voltage when not charging
    if (chgState == ChgState::NoCharger) {
      if (V_bus <= 3.30f) {
        if (soc > 0.02f) soc = 0.02f; // ≤2% at/under 3.30 V
      } else if (V_bus <= 3.35f) {
        if (soc > 0.06f) soc = 0.06f; // ≤6% at/under 3.35 V
      } else if (V_bus <= 3.40f) {
        if (soc > 0.12f) soc = 0.12f; // ≤12% at/under 3.40 V
      }
    }

    // Output reading
    r.voltage_V = V_bus;
    r.current_A = I;        // keep measured current (near 0 during charge)
    r.soc_0_1   = soc;
    r.atRest    = atRest;
    r.charging  = (chgState != ChgState::NoCharger);
    r.eta_min   = eta_min;
  }

  BatteryReading get() const override { return r; }
};

// Factory for Gen2
IBatterySensor* makeBatterySensor() { return new INA219SoCSensor(); }

#endif // BOARD_GEN_2
