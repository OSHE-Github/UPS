/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_THERMAL_H
#define UPS_THERMAL_H
#include <stdint.h>

struct ThermalState {
  bool shed = false;
  bool recovering = false;
  uint8_t lowSamples = 0;
  uint32_t recoveryStart = 0;
};
enum ThermalEvent { THERMAL_NONE, THERMAL_ENTER, THERMAL_EXIT };

// Typical MPQ5031 ratios, not a model of tolerance or all PD-engine states.
// Call every 100 ms. Three low samples debounce breadboard contact noise.
static inline ThermalEvent thermalUpdate(ThermalState &s, uint16_t adc, uint32_t now)
{
  const uint32_t ratio = (uint32_t)adc * 1000U;
  if (!s.shed) {
    if (ratio <= 1023UL * 91U) {
      if (++s.lowSamples >= 3) {
        s.shed = true;
        s.lowSamples = 0;
        s.recovering = false;
        return THERMAL_ENTER;
      }
    } else s.lowSamples = 0;
  } else if (ratio >= 1023UL * 167U) {
    if (!s.recovering) {
      s.recovering = true;
      s.recoveryStart = now;
    } else if ((uint32_t)(now - s.recoveryStart) >= 16000UL) {
      s.shed = false;
      s.recovering = false;
      return THERMAL_EXIT;
    }
  } else s.recovering = false;
  return THERMAL_NONE;
}
#endif
