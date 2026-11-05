# Adafruit 4440 test on NUCLEO-L152RE

This is a standalone display test for the **NUCLEO-L152RE**, not UPS firmware.
The selected display is Adafruit 4440, SSD1306 128×32, address 0x3C.
Use the 4397 cable and the same 3.3 V / 100 kHz / I²C2 interface intended for
the UPS. The test is built but has **not yet been run on physical hardware**.

## Wire with power disconnected

Plug the cable's small connector into the display. Connect its individual
female sockets to the Nucleo's male headers:

| Cable | Function | NUCLEO-L152RE header |
| --- | --- | --- |
| Black | GND | **CN10 pin 20** |
| Red | 3.3 V | **CN7 pin 16** |
| Blue | SDA / PB11 | **CN10 pin 18** |
| Yellow | SCL / PB10 | **CN10 pin 25** |

These are physical connector pin numbers from **UM1724, Rev 17, Table 30,
page 60**, not Arduino pin labels. Locate pin 1 and the odd/even rows on the
board before counting. PB10 is also Arduino D6; PB11 is on the Morpho header.
Do not connect to Arduino SDA/SCL for this test: those use I²C1.
Do not use the Nucleo's 5 V pin for the red wire.

Power the Nucleo through its ST-LINK USB connector using its normal USB-power
jumper configuration. Keep the display on the supplied 150 mm cable. Its
onboard pull-ups are sufficient for the starting configuration; no breadboard
resistors or reset wire are required.

## Build and load

From the repository root, with Python and Arm GNU tools on PATH:

```powershell
python tests/display_nucleo/build.py
```

The build creates:

- `build/nucleo_l152re_oled.hex` — address-bearing file for STM32CubeProgrammer.
- `build/nucleo_l152re_oled.elf` — symbols for debugging.
- `build/nucleo_l152re_oled.bin` — raw image, Flash base 0x08000000.

The build uses **STM32L152xE**, the RE startup file, and 512 KB Flash / 80 KB RAM.
It reuses the repository's HAL and core CMSIS sources. Two missing
device-specific files are downloaded from the pinned ST CMSIS Device L1
v2.3.2 commit and verified with SHA-256; later builds use the local cache.
ST's copyright notices remain in those files. Their license is the CMSIS
device component's license, also present under
`STM32CubeIDE/Drivers/CMSIS/Device/ST/STM32L1xx/License.md`.

In STM32CubeProgrammer, connect to the Nucleo over ST-LINK/SWD, select the
**HEX** file, program with verification, and reset/run the board. Programming
replaces the Nucleo's current application, so retain any test image you need
before flashing. This script never programs hardware automatically.

If creating a CubeIDE/CubeMX project instead, select **NUCLEO-L152RE** as the
board. Enable I²C2 on PB10/PB11 at 100 kHz, open-drain alternate function AF4
without internal pull-ups. `oled.c` and `oled.h` are the portable display
module. The supplied `main.c` is a standalone reference with its own SysTick
handler; do not add a second copy of that handler to a generated project.

## Expected behavior

After startup, two seconds of alternating checkerboards exercise all pixels.
The display then shows:

```text
UPS DISPLAY TEST
I2C2 3V3 100KHZ
FRAME 000004
TEST DATA ONLY
```

The counter changes roughly twice per second. Nucleo LD2 toggles every
half-second while display transfers succeed. If the display is absent or a
transfer fails, LD2 toggles rapidly, and the program retries initialization
roughly every two seconds. It uses bounded I²C timeouts, not an indefinite
wait. A bus physically held low may need power removed and the wiring fixed.

Debugger variables:

- `display_frames`: successfully transferred test frames.
- `display_errors`: initialization or frame-transfer failures.
- `display_status`: most recent HAL result; HAL_OK is 0.

An ACK at 0x3C alone is not a passing display test. Visually confirm the
checkerboard and readable, changing text.

## Bench acceptance record

1. Check power and wire order, then power up. Confirm the patterns and all
   four text lines appear without missing or shifted rows.
2. Let the counter run for 10 minutes. Confirm updates continue and
   `display_errors` remains zero.
3. Reset the Nucleo five times, then remove/reapply USB power five times.
   Confirm the display starts on every attempt.
4. With power off, disconnect the display and boot. Confirm the fast LED
   heartbeat continues. Power off before reconnecting, then repeat startup.
5. If a scope or logic analyzer is available, confirm approximately 100 kHz,
   address 0x3C, 3.3 V bus highs, and I²C Standard-mode rise time within 1 µs.
6. Record the module revision, test date, measured supply, and result below.

| Check | Result |
| --- | --- |
| Display/cable received | Pending |
| Visual startup and text | Pending |
| Ten-minute updates / error count | Pending |
| Reset and full power cycles | Pending |
| Display absent / reconnect with power off | Pending |
| Electrical measurements, if available | Pending |

The Nucleo test validates the display, driver, and cable on this interface.
Repeat startup and update tests on the fabricated UPS, where power sequencing,
PCB routing, and converter noise differ. Live UPS telemetry still needs its
own acquisition and integration code.

For the two-Arduino BQ/PD status test, see
[the complete harness guide](../power_emulators/README.md). Build with
`python tests/display_nucleo/build.py --emulators` to add I2C1 on PB8/PB9.
That produces a separate `nucleo_l152re_harness.hex` and shows explicitly
simulated status data on the same OLED.

Sources: [ST UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf),
[Adafruit 4440](https://www.adafruit.com/product/4440),
[4397 cable colors](https://www.adafruit.com/product/4397),
[pinned ST CMSIS source](https://github.com/STMicroelectronics/cmsis-device-l1/tree/1827333a7f7822282a6a46e4169596b5fb58cf61).

## Real ADC temperature option

Add `--thermistors` to sample PC0/ADC10 and PC1/ADC11. With
`--emulators --thermistors`, the OLED alternates simulated power status
and real temperature pages. Inputs keep updating if the display fails.
The ADC wiring, Nucleo solder-bridge requirements, J10 connections, and
resistor acceptance checks are in [the thermistor guide](../../docs/thermistor-adc.md).
Both the one-Mega and two-Uno emulator versions work with this host image.
