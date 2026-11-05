# Thermistors on the harness and finished UPS

The Nucleo harness uses two independent **10 kΩ, B3950 NTC** sensors.
The production UPS now uses **four battery sensors and one board sensor**.
See [the production thermal map](../STM32CubeIDE/THERMAL.md) for the current
connector assignments and fan policy. Both builds share the conversion module.

## Circuit and connections

Build this twice on the Nucleo breadboard. The UPS PCB includes the resistors
and capacitors; its sensors connect to J2, J5, J11, J6 and J10.

```text
3.3 V ── 10 kΩ 1% ──┬── STM32 ADC input
                     ├── 100 nF ── GND
                     └── 10 kΩ B3950 NTC ── GND
```

| Harness measurement | NUCLEO-L152RE |
| --- | --- |
| Battery temperature | PC0 / ADC10, CN7 pin 38 |
| Board temperature | PC1 / ADC11, CN7 pin 36 |
| Sensor returns | GND, e.g. CN10 pin 20 |
| Divider supply | 3.3 V, CN7 pin 16 |

Production connector and component assignments are in the linked thermal map.

**Nucleo solder bridges:** PC0/PC1 reach these headers with **SB51/SB56 ON
and SB46/SB52 OFF**. Check the actual board, especially if previously modified
for Arduino shields. Otherwise those header positions may carry PB8/PB9
I²C instead. Keep the emulator bus on CN5 D15/D14. Verify the normal
3.3 V VDDA/VREF supply configuration as well.

For production, use J2/J5/J11/J6 pins 1–2 for batteries 1/2/3/4,
respectively. Use **J10 pins 3–4 for the board sensor**; J10 pin 2 is unused.
The old R45/C71 battery divider has been removed. Thermistor polarity does
not matter. Keep sensor/ground pairs away from switching and power wiring.
Use electrically insulating attachment to each cell body and strain relief.

## Firmware

Build the full Nucleo harness:

```powershell
python tests/display_nucleo/build.py --emulators --thermistors
```

Load `tests/display_nucleo/build/nucleo_l152re_harness_adc.hex` into the
NUCLEO-L152RE with ST-LINK verification. The display alternates simulated
BQ/PD status and **REAL ADC TEMPERATURE** approximately every two seconds.
The ADC continues polling when the display is unavailable.

For just the display and real sensors, omit `--emulators`:
`nucleo_l152re_oled_adc.hex`. The original display-only test is still available
by omitting both switches.

Production bring-up firmware already calls `Thermistors_Init()` and
`Thermistors_Poll()` in `STM32CubeIDE/Core/Src/main.c`. Build separately:

```powershell
python tests/build_ups.py
```

This creates the **RC-target** image under `tests/display_nucleo/build/ups/`.
Never interchange this image with the Nucleo RE image. On the UPS, readings
are currently available through the debugger; production OLED/status UI
integration is not included in the bring-up loop.

On the Nucleo, watch `thermistors[0]` (battery), `thermistors[1]` (board), and
`thermistor_errors`. Each reading has raw ADC counts, milli-degrees Celsius,
and a status: OK, OPEN, SHORT, OUT_OF_RANGE, or ADC_ERROR. Error readings
replace stale valid temperatures. Near-rail thresholds also classify
extreme resistances as open/short; these are diagnostic heuristics.

The module uses 12-bit ADC1, HSI/4, long 384-cycle sampling, discards the first
sample after switching channels, and averages eight samples. Conversion uses
R25 = 10 kΩ and B = 3950 K. The useful sensor range is −30 to +125 °C.
This is a low-cost measurement: resistor tolerance, sensor tolerance,
self-heating, mounting, and board noise affect accuracy. At 25 °C, continuous
3.3 V bias dissipates about 0.27 mW in the sensor (roughly 0.4 °C self-heating
using the manufacturer's still-air dissipation factor).

Production indices 0–3 are batteries 1–4; index 4 is board temperature.
CubeMX reserves PB13/PB14/PB15 and PC0/PC1 as analog GPIO; the custom module owns ADC1 setup.
ADC HAL enablement lives in a preserved USER CODE block. After regeneration,
retain the ADC driver sources and `thermistors.c`, and rebuild. Do not add
a second generated ADC owner without reconciling initialization.
The four ADC HAL files are ST v1.4.6, commit
`9a48991ee0890a249d80a03451dac1392baf33d6`, matching the existing HAL.

**These readings do not yet implement charge shutdown.** Battery-specific
charging temperature limits, fault policy, and hardware enforcement must be
integrated and tested before relying on temperature for protection.

## Bench checks before using the readings

With power off, substitute the following resistors for each thermistor;
leave its 10 kΩ upper resistor and 100 nF capacitor installed. Expected
numbers assume a 3.3 V ADC reference and supply.

| Lower resistance | Approximate voltage | 12-bit counts | Expected indication |
| --- | ---: | ---: | --- |
| 10 kΩ | 1.650 V | 2048 | 25 °C |
| 4.7 kΩ | 1.055 V | 1309 | 43 °C |
| 22 kΩ | 2.269 V | 2815 | 8 °C |
| Disconnected | 3.3 V | 4095 | OPEN |
| Short to ground | 0 V | 0 | SHORT |

1. Measure both divider nodes before connecting to PC0/PC1; neither may exceed
   the STM32's analog supply. Do not use Arduino 5 V for these dividers.
2. Check the resistor substitutions on each channel, independently, then fit
   the real sensors. Warm one gently with fingers: only its reading should rise.
3. Disconnect one sensor and confirm OPEN while the other keeps updating.
   Check SHORT with a ground jumper, changing wiring with power off.
4. Run with the OLED and emulator bus active for ten minutes. Record ADC,
   display, and bus error counts. Repeat resets and complete power cycles.
5. Repeat on the fabricated UPS with converters operating; bench wiring
   cannot establish the board's final noise or thermal performance.

## Optional Arduino test of the existing PD thermistor

The UPS already has **TH1 = 100 kΩ** and **R8 = 68.1 kΩ** at the MPQ5031's
5 V NTC input. That circuit remains separate and is not wired into PC0/PC1.
**Never connect its 5 V node to the STM32 ADC.**

To exercise the PD emulator's optional thermal model, recreate this divider
on the PD Uno or the Mega: its own 5 V → 68.1 kΩ → A0 → 100 kΩ NTC → GND,
plus 100 nF from A0 to GND. Use the same 5 V supply as the AVR ADC reference.
Do not reuse the selected 10 kΩ sensors in this circuit.

At 115200 baud, PD console `t` prints ADC state; `e` enables the model.
On the Mega use `Pt` and `Pe`. It starts disabled. A 4.7 kΩ substitute
for the lower NTC produces a load-shedding event after three low samples;
15 kΩ or 100 kΩ allows recovery after 16 continuous seconds above the
recovery threshold. Use resistors to test this without heating batteries.

The model uses typical 9.1% trip / 16.7% recovery voltage ratios and models
NTC_MODE=0 only. It sets PD load-shedding event bit 0, **not** the internal
ANA_OTP bit 2. It does not model the PD power engine or validate actual
protection. Open/short indications appear on serial independently of the
modeled trip behavior; an open sensor produces a high voltage and does not
automatically trip this simplified model.

## Verification status — October 20, 2026

The five production assignments have been checked with a native KiCad
netlist and ST's STM32L152RCT6 pin table. Both production and two-sensor
Nucleo firmware compile. Fan policy tests cover all five sensors,
hysteresis, faults, startup and stale data. Physical flashing, resistor
checks, accuracy and operation under load remain untested. See the project
thermal map for the current design; earlier two-sensor PCB checks do not
establish readiness of the revised board.

Sources: [ST Nucleo manual, Tables 10 and 30](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf);
[ST RM0038 ADC reference](https://www.st.com/resource/en/reference_manual/rm0038-stm32l100xx-stm32l151xx-stm32l152xx-and-stm32l162xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf);
[Adafruit 4890 specifications](https://www.adafruit.com/product/4890);
[MPS MPQ5031 datasheet](https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MPQ5031GRE-AEC1/document_id/8999/).
