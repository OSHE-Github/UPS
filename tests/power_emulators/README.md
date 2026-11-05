# UPS harness: one Mega or two Uno R3 boards

Use the NUCLEO-L152RE as host, either **one Mega 2560** or **two Uno R3
boards**, and the Adafruit 4440 display. The Arduinos emulate a read-only
subset of the BQ25730 charger and MPQ5031 PD controller. BQ/PD values are
simulated; the optional STM32 ADC temperature readings are real.

```text
NUCLEO-L152RE
  I2C1 PB8/PB9 ── 3.3 V / 5 V translator ── Mega (both roles)
                                           OR Uno BQ + Uno PD
  I2C2 PB10/PB11 ── OLED 0x3C, powered at 3.3 V
  PC0 / PC1 ADC ── two real thermistor dividers at 3.3 V
```

## Choose a version

- **Two Uno R3 boards:** each answers exactly one address using Arduino Wire.
  This permits independent device unplug/reset checks.
- **One Mega 2560:** custom TWI interrupts serve both devices concurrently.
  Console selection changes commands only; both device roles keep running.

**Mega limitation:** its hardware address mask also ACKs `0x29`, `0x2A`,
`0x2B`, `0x68`, `0x69`, and `0x6A`, as well as the intended `0x28` and
`0x6B`. Only `0x28` and `0x6B` implement registers. Alias reads return `FF`;
alias transactions are counted. A scan therefore finds eight addresses.
Use this version on the isolated breadboard test bus, without real BQ/PD
chips or other devices at those addresses. Keep the OLED on I²C2.

## Wiring

These profiles require classic **Uno R3 / ATmega328P** or **Mega 2560 /
ATmega2560**, not Uno R4. Check the actual board markings.

Use an I²C bidirectional level translator such as
[Adafruit 757](https://www.adafruit.com/product/757), $3.95. Its headers
need soldering; its onboard 10 kΩ pull-ups are the starting configuration.
AVR Wire enables internal pull-ups to 5 V, so omitting external 5 V
resistors alone does not make direct STM32 wiring appropriate.

| Signal | Nucleo / translator low side | Translator high side |
| --- | --- | --- |
| SCL | PB8, CN5 pin 10 / D15 | Both Unos A5, OR Mega pin 21 |
| SDA | PB9, CN5 pin 9 / D14 | Both Unos A4, OR Mega pin 20 |
| Low-side reference | Nucleo 3.3 V, CN7 pin 16 | — |
| High-side reference | — | 5 V from one Arduino |
| Ground | Nucleo GND, e.g. CN10 pin 20 | GND on each Arduino |

Power each board from its own USB connector. Join grounds, **not their
5 V outputs**. With two Unos, join both boards to the same high-side SCL/SDA
pair. Use short jumpers and change wiring with power off.

The [display guide](../display_nucleo/README.md) gives OLED cable wiring:
black GND, red 3.3 V, blue PB11 SDA, yellow PB10 SCL. It uses I²C2.
The [thermistor guide](../../docs/thermistor-adc.md) gives the two 3.3 V ADC
dividers and required Nucleo solder-bridge settings.

The selected display, cable, translator, and two thermistors total **$19.30**
before shipping/tax and breadboard parts, headers, wires, and USB cables.
No order has been placed; delivery to ZIP 49931 needs a checkout estimate.

## Build and load

From the repository root, with Python, Arm GNU tools, and Arduino IDE/CLI:

```powershell
python tests/power_emulators/build.py uno-bq
python tests/power_emulators/build.py uno-pd
# OR, for the single Mega:
python tests/power_emulators/build.py mega-both
# Nucleo with simulated power status and real ADC temperatures:
python tests/display_nucleo/build.py --emulators --thermistors
```

Arduino HEX files go under `build/uno-bq`, `build/uno-pd`, and `build/mega-both`.
The scripts do not flash or select a port. Alternatively, open
`power_emulator/power_emulator.ino` in Arduino IDE. For Uno BQ leave
`UPS_EMULATOR_PD=0`; for Uno PD set it to `1`; for Mega set
`UPS_EMULATOR_DUAL=1`. Select the matching board and USB port before upload.
Retain any existing program you need.

The full Nucleo image is
`tests/display_nucleo/build/nucleo_l152re_harness_adc.hex`. Program through
ST-LINK with STM32CubeProgrammer verification. Omit `--thermistors` for
`nucleo_l152re_harness.hex`; omit both switches for the display-only test.
This RE-target image is not for the production RC MCU.

## Try the scenarios

Open serial at **115200 baud**. Uno roles accept lowercase commands.
On Mega, send uppercase `B` or `P` to select the role first, e.g. `Bn`,
`Bf`, `Pa`, `Pm`, `Pf`. Both devices remain active on I²C regardless of
the selected console role. Line endings are ignored.

| Role | Command | Injected response |
| --- | --- | --- |
| BQ | `n` | Input present, fast charging; `8400` |
| BQ | `b` | No input / charging indication; `0000` |
| BQ | `f` | BATOC event; `8040`, then `8000` |
| PD | `a` | Attach / PDO1 event; `000A`, `0400` |
| PD | `d` | Zero both status words |
| PD | `m` | Capability mismatch / PDO1 event; `000A`, `2400` |
| PD | `f` | Internal thermal event; `0004`, `0000` |
| Either | `s` | Read, unsupported-read, rejected-write counters |
| PD | `t` | Optional A0 NTC ADC state |
| PD | `e` | Toggle optional NTC model; initially off |

The OLED shows `SIM TEST`, raw status, and accumulated event bits.
`LATCH B40` records BATOC; internal PD thermal adds `04` to `LATCH P`.
`M1` latches capability mismatch. Reset the Nucleo to clear latches.
PD status words clear on read, so events may appear in raw values for only
one frame. Zero PD status is not proof of detachment.

With `--thermistors`, the display alternates the status page and
`REAL ADC TEMPERATURE`. Polling continues even if the display is disconnected.
Missing/wrong IDs show `BQ OFFLINE` or `PD OFFLINE` and clear stale raw values.
Debugger variables include `power_polls`, `bq_errors`, `pd_errors`, `bq_id`,
`pd_id`, `bq_status`, `pd_status1`, `pd_status2`, `bq_faults_seen`,
`pd_events_seen`, `pd_mismatch_seen`, and display counters.

The optional Arduino A0 model is for the existing **100 kΩ PD NTC circuit**,
separate from the new **10 kΩ STM32 ADC sensors**. See the thermistor guide
for its 5 V divider, resistor substitution tests, and limits. On Mega use
`Pt` / `Pe`.

## Protocol limits

- Register-pointer writes followed by two-byte reads, low byte first.
- BQ: byte-addressed `0x20/0x21` status, `0x2E/0x2F` IDs (`40`/`D5`).
  Fault bits 7:5 clear when the status response is queued.
- PD: word-addressed `0x10/0x11` status and `0x12` vendor ID `8000`.
  Status clears when responses are queued. Address `0x28` is the default;
  verify the real production device's OTP configuration.
- Unsupported reads return `FFFF`; configuration writes are discarded and
  counted. Writes may be ACKed, which does not mean they were implemented.
- Partial/aborted read side effects, long bursts, address changes, watchdogs,
  ADC telemetry, configuration, reset behavior, and complete device timing
  are not modeled. Use the documented two-byte read sequence.
- No CHRG_OK/PROCHOT or PD voltage-selection GPIOs are driven. USB-C CC/PD
  signaling, power conversion, battery behavior, and protection need physical tests.

The existing production initialization uses I²C1 PB6/PB7, while the schematic
uses PB8/PB9. This harness uses PB8/PB9. The production mismatch remains to
be addressed when integrating power-controller firmware.

## Bench acceptance

1. Check board models, voltage translation, ground, and wiring. For Mega,
   expect the documented eight ACK addresses and inspect alias counters.
2. Load both Uno roles or the combined Mega, then the Nucleo. Confirm both IDs.
3. Inject each scenario; check raw values and latches. Unsupported/rejected
   counts should remain zero during the normal host test.
4. Run ten minutes, record errors, reset each board, and repeat a full power cycle.
5. With power off, remove one device's signal wires. On next power-up verify
   offline indication and continued display/other-device operation. With one
   Mega, removing it takes both roles offline; independent unplug testing
   needs the two-Unos arrangement.
6. Perform the ADC guide's resistor/open/short tests, and the display guide's
   checkerboard/reset tests. If available, inspect 100 kHz clocks and rise times.

Build checks on October 20, 2026:

| Target | Result |
| --- | --- |
| Uno BQ / AVR core 1.8.6 | Pass; 4,130 bytes Flash, 400 bytes static RAM |
| Uno PD / AVR core 1.8.6 | Pass; 5,000 bytes Flash, 414 bytes static RAM |
| Mega combined / AVR core 1.8.6 | Pass; 5,212 bytes Flash, 234 bytes static RAM |
| Nucleo host / Arm GCC 14.2.1 | Compiles and links |
| Physical programming and scenario tests | Pending |

Arduino core and unused Nucleo `nosys` stub warnings are from bundled library
code. **No physical harness test has been completed.** This starter does not
clear the UPS power design for fabrication.

Sources: [TI BQ25730 datasheet](https://www.ti.com/lit/ds/symlink/bq25730.pdf);
[MPS MPQ5031 datasheet](https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MPQ5031GRE-AEC1/document_id/8999/);
[ST UM1724](https://www.st.com/resource/en/user_manual/um1724-stm32-nucleo64-boards-mb1136-stmicroelectronics.pdf);
[Arduino Mega 2560](https://docs.arduino.cc/hardware/mega-2560/);
[Microchip ATmega2560 TWDR/TWAMR documentation](https://ww1.microchip.com/downloads/en/DeviceDoc/ATmega640-1280-1281-2560-2561-Datasheet-DS40002211A.pdf);
[AVR Wire implementation](https://github.com/arduino/ArduinoCore-avr/blob/1.8.6/libraries/Wire/src/utility/twi.c).
