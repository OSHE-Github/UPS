/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef UPS_FAN_CONTROL_H
#define UPS_FAN_CONTROL_H
#include <stdint.h>

/* Bring-up cooling settings, not cell charging/discharging safety limits.
 * Confirm with the selected cells, enclosure and measured thermal behavior. */
#define FAN_ON_MILLI_C 40000
#define FAN_OFF_MILLI_C 35000
#define FAN_SAMPLE_MAX_AGE_MS 2000U

extern volatile uint8_t fan_is_on;
void Fan_Init(void);
void Fan_Update(void);
#endif
