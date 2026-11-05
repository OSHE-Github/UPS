# Display addition for the October 21 PCB order

## Selected hardware

Use **Adafruit 4440**, a 0.91-inch, 128×32 monochrome SSD1306 I²C display,
with **Adafruit 4397**, the 150 mm STEMMA QT to individual female-header cable.
The cable plugs into the display without soldering and fits the UPS's 2.54 mm
male header. The display stays off the UPS PCB.

| Item | Quantity | Price checked October 20, 2026 |
| --- | ---: | ---: |
| [Adafruit 4440 display](https://www.adafruit.com/product/4440) | 1 | $12.50 |
| [Adafruit 4397 cable](https://www.adafruit.com/product/4397) | 1 | $0.95 |
| Straight 1×4, 2.54 mm male header, fitted as J9 | 1 | Use existing header stock; price not quoted |
| Display and cable subtotal | | **$13.45 before shipping and tax** |

Adafruit listed both items in stock when checked. DigiKey also listed the
[4440, part 1528-4440-ND](https://www.digikey.com/en/products/detail/adafruit-industries-llc/4440/10824271)
in stock for $12.50. Destination ZIP is **49931**. Adafruit requires email and
a full address before showing shipping options; arrival time is not confirmed.
Nothing has been purchased.

The exact module matters: it accepts 3.3 V power and logic, has automatic reset,
and uses 7-bit I²C address **0x3C**. Its
[published circuit](https://github.com/adafruit/Adafruit-128x32-I2C-OLED-Breakout-PCB)
has 10 kΩ pull-ups on the connector side. The UPS therefore needs no additional
pull-ups for this selected module and short cable. Start at **100 kHz** and
check the edges on the bench before increasing speed or cable length.

## UPS connection

J9 is added to `ecad/UPS/controller.kicad_sch` and routed in
`ecad/UPS/UPS.kicad_pcb`. It uses I²C2 independently of the power-controller bus.
Pin 1 is the square pad. Follow the silk labels and individual cable colors;
the cable's loose sockets do not impose a connector pin order.

| J9 pin | Silk | Signal | UPS destination | 4397 wire |
| --- | --- | --- | --- | --- |
| 1 | GND | Ground | Ground plane | Black |
| 2 | 3V3 | 3.3 V supply | U5 output, pin 5 | Red |
| 3 | SDA | I²C2 data | U4 PB11, package pin 30 | Blue |
| 4 | SCL | I²C2 clock | U4 PB10, package pin 29 | Yellow |

J9 pin 1 is at PCB coordinates (110.0, 116.5) mm. The connector sits in the
gap between the underside battery-holder courtyards. Its new signal routing
uses In2.Cu, with short top-layer escapes at the MCU. The existing design
already has four copper layers; this addition does not change the layer count
or board outline. Ground zones were refilled after routing.

Power the display from **3V3**, including on the test harness. The module's
input-side pull-ups follow its supply, so using 5 V would also raise its
connector-side I²C high level. Allow roughly 20 mA typical additional supply
load, per the manufacturer; measure actual draw for the intended screen.

## Nucleo test before committing to the display

The available **NUCLEO-L152RE** has an STM32L152RE, while the UPS uses an
STM32L152RCT6. They support the I²C2 interface used here, but use different
firmware targets, startup definitions, and memory sizes. Build the test as
**STM32L152xE**; retain **STM32L152xC** for the UPS.

Use the [test harness instructions](../tests/display_nucleo/README.md).
The test uses PB10/PB11, the same peripheral and GPIO names as the UPS. It
checks display initialization, pixel patterns, text updates, and reset/startup.
It does not exercise the UPS charger, battery measurements, or power paths.

## Validation and ordering status

- Fresh native KiCad export confirms all four J9 nets, including 3.3 V rather
  than 5 V, and PB10/PB11 package pins 29/30.
- Comparing the routed candidate against the original board with both sets of
  zones refilled and the same project constraints found **no new DRC violations**.
  The saved final board likewise introduces no new violation relative to that
  baseline. Existing unconnected-item count remains 239.
- The schematic ERC count decreased from 146 to 144, as PB10/PB11 are now connected.
  Existing schematic findings remain.
- The standalone Nucleo firmware compiled and linked for STM32L152xE.
  Its OLED driver also compiles for the UPS's STM32L152xC target.
- **Physical display test is pending.** No display was connected or programmed
  during preparation. Manufacturer compatibility and a successful build are
  not a bench-test result.
- **The whole UPS is not cleared for fabrication by this work.** Existing board
  shorts, clearance findings, and unrouted connections need the project's normal
  ordering review. This work only implements and checks the display addition.

The current UPS bring-up firmware still configures I²C1 on PB6/PB7, while its
schematic routes the power-controller bus to PB8/PB9. That separate firmware
mismatch remains to be corrected when integrating power-controller firmware.
The new display test does not use I²C1.

References: [STM32L152RC datasheet](https://www.st.com/resource/en/datasheet/stm32l152rc.pdf),
[NUCLEO-L152RE manual, UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf),
[Adafruit display specifications](https://www.adafruit.com/product/4440),
[Adafruit cable wiring](https://www.adafruit.com/product/4397).

## Complete harness parts cart

The prepared cart contains display 4440 ×1, cable 4397 ×1,
[I²C translator 757](https://www.adafruit.com/product/757) ×1 ($3.95), and
[10 kΩ B3950 sensor 4890](https://www.adafruit.com/product/4890) ×2 ($0.95 each).
**Subtotal: $19.30**, before shipping, tax, and any header/resistor/capacitor/
hookup-wire stock needed. The cart remains open; no order was placed.

Both Arduino arrangements and ADC readings are documented in the
[emulator guide](../tests/power_emulators/README.md) and
[thermistor guide](thermistor-adc.md). J10, R45/R46, and C71/C72 implement the
ADC addition. Final schematic ERC is 142 findings after both additions.
Existing board-wide issues remain.
