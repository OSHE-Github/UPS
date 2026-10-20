# Battery voltage from the BQ25730

The UPS uses the charger's VBAT ADC over I2C, not STM32 PA4.
`VBAT_SENSE` remains connected to BQ25730 SRN and the MPQ5031 R40/R41
divider. Only the MCU branch (series capacitor C65 and resistor R42) and
its controller sheet input have been removed from the schematic.
PA4 now has a no-connect marker and is configured as an unused analog GPIO;
the incorrect PA4 LED toggle has been removed.

Production I2C1 uses **PB8 / U4 pin 61 for SCL** and **PB9 / pin 62 for SDA**,
matching the schematic and its existing pull-ups. The OLED uses a separate
bus and is not implemented by this module.

## Firmware interface

`BatteryVoltage_Init(&hi2c1)` initializes the module.
Call `BatteryVoltage_Poll()` regularly from the main loop. It advances a
nonblocking conversion state machine with bounded 20 ms I2C transaction
timeouts; it does not wait in a loop for the ADC.

Use **`BatteryVoltage_Get()`** and check its status before using the value.
It returns millivolts, raw ADC code, sample timestamp and status. Only
`BATTERY_VOLTAGE_OK` is a normal valid reading. I2C errors, incorrect chip
identity, conversion timeout and configuration changes invalidate the
reading. The getter marks readings older than 2 seconds stale. Debugger
variables are `battery_voltage` and `battery_voltage_errors`; direct reads
of the variable do not perform the getter's age check.

The module:

1. Checks manufacturer `0x40` and device `0xD5` at registers `0x2E/0x2F`
   at the charger's 7-bit I2C address `0x6B`.
2. Clears only EN_LWPWR in ChargeOption0, then allows analog settling.
3. Enables VBAT in ADCOption and requests a one-shot conversion roughly
   once per second. Other enabled channels and reserved bits are retained;
   this module owns ADC conversion mode.
4. Waits for ADC_START to clear, verifies low-power/ADC settings, and reads
   VBAT at `0x2C`. A 500 ms conversion timeout covers the specified
   25 ms per channel for up to eight enabled channels.

Charge-current, charge-voltage and protection settings are not programmed
here. The charger remains in performance mode while this monitor is active,
which increases battery-only quiescent consumption relative to low-power
mode. Coordinate future sleep control or another ADC owner with this module.

## Scaling and limits

The current schematic has four parallel cells (1S4P), so this is **one
shared battery-bank voltage**, not four independent cell readings.

For the charger's 1S–4S configuration:

`battery_mV = 2880 + 64 × raw`

Examples: code 18 = 4032 mV; code 21 = 4224 mV. Resolution is 64 mV.
Codes 0 and 255 are marked `AT_RANGE_LIMIT`: they represent the endpoints
of the 2.88–19.2 V ADC range and cannot distinguish an endpoint voltage
from an out-of-range input. In particular, zero ADC code does not establish
a healthy battery at exactly 2.88 V or establish battery presence.
Do not derive a precise state-of-charge percentage from this alone.
Changing the hardware to 5S requires the different 8.16 V offset.

## Remaining hardware work

The **project PCB is unchanged**. Update it from the schematic and remove
the obsolete C65/R42 footprints and MCU-only branch copper during the
manual PCB revision. Keep the shared VBAT connection to SRN and R40.
Regenerate the manufacturing BOM after synchronization; C65/R42 are no
longer in the schematic BOM.

Before relying on the measurement, verify I2C operation on PB8/PB9 and
compare several battery-bank voltages with a meter, including near full
charge and near the ADC's lower limit. Test charger loss/reset, bus errors,
recovery, adapter removal and stale data. Software tests do not establish
accuracy, battery protection, or board readiness.

Source: [TI BQ25730 datasheet, sections 8.5, 8.6.1, 8.6.10, 8.6.16 and
8.6.24](https://www.ti.com/lit/ds/symlink/bq25730.pdf).
