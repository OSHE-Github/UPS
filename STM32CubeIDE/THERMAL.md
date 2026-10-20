# UPS temperature inputs and fan control

The production STM32L152RCT6 uses five independent ADC inputs. Pin numbers
below are for its LQFP64 package, verified against ST datasheet Table 9:
https://www.st.com/resource/en/datasheet/stm32l152rc.pdf

**Implementation status:** this map is implemented in the schematic and
firmware. The project PCB has not been updated for these changes; a temporary
routing trial was left unapplied. Update the PCB from the revised schematic
and review it separately before using this document as an as-built wiring map.

| Reading index | Sensor / net | Connector | MCU pin | ADC1 channel | Pull-up / filter |
| --- | --- | --- | --- | --- | --- |
| 0 | Battery 1 / bat_th1 | J2.1 signal, J2.2 GND | PB13 / 34 | 19 | R24 / C70 |
| 1 | Battery 2 / bat_th2 | J5.1 signal, J5.2 GND | PB14 / 35 | 20 | R28 / C69 |
| 2 | Battery 3 / bat_th3 | J11.1 signal, J11.2 GND | PC0 / 8 | 10 | R47 / C73 |
| 3 | Battery 4 / bat_th4 | J6.1 signal, J6.2 GND | PB15 / 36 | 21 | R34 / C68 |
| 4 | Board / TEMP_BOARD | J10.4 signal, J10.3 GND | PC1 / 9 | 11 | R46 / C72 |

In the schematic, J6 retains its PB15 connection and is now named `bat_th4`
(formerly `bat_th34`). J11 carries `bat_th3`. J10 pin 1 remains ground;
pin 2 is unused. The redundant R45/C71 battery divider has been removed
from the schematic.

Each channel uses a **10 kΩ, 1% pull-up to +3.3 V**, a **10 kΩ B3950 NTC
to ground**, and a **100 nF capacitor from the signal to ground**.
Thermistor leads have no polarity. Attach one insulated sensor to each
cell body, with strain relief and no exposed conductor touching cell
terminals. Put the board sensor against the power component or board area
whose measured temperature should control cooling; verify that location
under load. The connector location is not the temperature measurement
location when using a sensor on leads.

The separate MPQ5031 TH1 circuit uses a 100 kΩ thermistor and a 5 V supply.
It is not one of these five channels and must not connect to the MCU ADC.

## Acquisition

`thermistors.c` owns ADC1 and configures all five GPIOs in analog mode with
no internal pull-up/down. It uses HSI/4, 12-bit conversion, 384-cycle
sampling, discards one sample after channel switching, then averages eight.
Readings are available in `thermistors[]`: raw counts, milli-degrees Celsius,
and OK / OPEN / SHORT / OUT_OF_RANGE / ADC_ERROR status. Failed reads replace
old data. `thermistor_scan_complete` and `thermistor_sample_tick` identify a
completed scan and its age.

The `UPS_THERMISTORS` build flag retains the existing **two-channel Nucleo
harness**: PC0/ADC10 at index 0 and PC1/ADC11 at index 1. Its breadboard
dividers and display remain separate from the five-channel production map.
Do not interchange the Nucleo RE and production RC firmware images.

CubeMX reserves these pins as analog GPIO. Preserve the custom ADC owner
and its sources when regenerating the project.

## Fan policy

PB0 controls the active-high fan driver. Firmware requests ON during
initialization. With valid readings, the hottest of all five sensors turns
the fan on at **40 °C** and off at **35 °C**; between those thresholds the
previous state is retained. An incomplete scan, any sensor fault, or data
older than 2 seconds requests ON. Thresholds live in `fan_control.h`.

These are provisional cooling thresholds for bring-up. They do not
implement battery charge/discharge temperature limits or a safety shutdown.
The hardware gate pull-down keeps the fan off before firmware starts.
The policy also depends on the main loop continuing to execute; it is not
an independent hardware watchdog. Qualify the fan driver and establish
battery-specific protection separately.

## Bench verification

With power off, substitute a resistor for each thermistor in turn:

| Substitute | Expected ADC counts | Expected result |
| --- | ---: | --- |
| 10 kΩ | about 2048 | about 25 °C |
| 4.7 kΩ | about 1309 | about 43 °C; fan ON |
| 22 kΩ | about 2815 | about 8 °C |
| Open circuit | near 4095 | OPEN; fan ON |
| Ground short | near 0 | SHORT; fan ON |

Confirm every sensor changes only its own reading. Measure the signal
voltage before attaching the MCU; it must stay within the analog supply
range. Test hysteresis, resets, and faults, then repeat with converters and
fan operating to check noise. Build and simulated control tests do not
replace these hardware checks.
