/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "temperature_view.h"
#include "thermistors.h"
#include <stdio.h>

static void format(char *out, unsigned sensor)
{
    const ThermistorReading r = thermistors[sensor];
    const char *name = sensor ? "BOARD" : "BAT";
    if (r.status == THERM_OK) {
        int c = (int)((r.milli_celsius + (r.milli_celsius >= 0 ? 500 : -500)) / 1000);
        /* Whole degrees avoid implying millidegree measurement accuracy. */
        (void)snprintf(out, 22, "%s %s%u C", name, c < 0 ? "MINUS " : "",
                      (unsigned)(c < 0 ? -c : c));
    } else {
        const char *status = r.status == THERM_OPEN ? "OPEN" :
                             r.status == THERM_SHORT ? "SHORT" :
                             r.status == THERM_OUT_OF_RANGE ? "RANGE" : "ADC ERROR";
        (void)snprintf(out, 22, "%s %s", name, status);
    }
}
HAL_StatusTypeDef Temperature_Show(I2C_HandleTypeDef *display)
{
    char battery[22], board[22];
    format(battery, 0);
    format(board, 1);
    return OLED_Text(display, "REAL ADC TEMPERATURE", battery, board, "10K NTC B3950");
}
