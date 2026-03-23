# UPS Firmware Validation Checklist

This checklist is the primary validation runbook for the current UPS firmware in [STM32CubeIDE](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE). It is structured for three levels of depth:

1. Software-only validation
2. Attached-board validation with a debugger and the connected board
3. Bench extension tests for later, when instrumentation is available

For MCU-only bring-up before the PCB exists, there is also a host-side supervisor simulator:

```powershell
python .\scripts\run_supervisor_sim_tests.py -v
```

That simulator does not model analog power behavior, but it does model the firmware state machine, GPIO assumptions, device-presence paths, and major fault transitions.

The current firmware logic under test is centered around:

- [app_supervisor.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/app_supervisor.c)
- [bq25730.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/bq25730.c)
- [mpq5031.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/mpq5031.c)
- [board_config.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/board_config.c)
- [validation_debug.h](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Inc/validation_debug.h)

## Prerequisites

- STM32CubeIDE 1.19.0 is installed at `C:\ST\STM32CubeIDE_1.19.0`
- The board is reachable over ST-LINK/SWD
- The debug launch profile [ups_bringup.launch](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/ups_bringup.launch) is present
- Firmware is built from the `Debug` configuration
- A debugger watch/live expressions view is available for `g_validation_debug`

Machine-local tool paths discovered in this workspace:

- `C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe`
- `C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin\arm-none-eabi-gdb.exe`
- `C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107\tools\bin\STM32_Programmer_CLI.exe`

## Expected Phase-1 Configuration

These values come from [board_config.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/board_config.c) and must be treated as the validation baseline:

- BQ25730 address: `0x6B`
- MPQ5031 address: `0x28`
- Startup delay: `15 ms`
- Run poll interval: `250 ms`
- Fault poll interval: `1000 ms`
- BQ charge voltage: `4200 mV`
- BQ charge current: `1000 mA`
- BQ input current limit: `3000 mA`
- BQ input voltage limit: `4608 mV`
- BQ RAC: `10 mOhm`
- BQ RSR: `10 mOhm`
- MPQ PDO1 current: `5000 mA`

## Required Evidence

Capture this evidence for each run:

- Date/time and git revision: `git rev-parse HEAD`
- Build log
- Flash method used
- Debugger screenshots or copied values from `g_validation_debug`
- Register dump evidence before and after configuration
- Photos or notes for LED behavior
- Result of each fault scenario attempted

## Stage 1: Software-Only Validation

### 1. Record repo state

Run from the repo root:

```powershell
git status --short
git rev-parse HEAD
```

Pass criteria:

- Current revision is recorded
- Any unrelated dirty files are noted before validation starts

### 2. Clean rebuild from CLI

Run from the repo root:

```powershell
.\scripts\build_debug.ps1
```

Equivalent manual command, if you need it:

```powershell
$env:Path='C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344\tools\bin;'+$env:Path
& 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe' -C STM32CubeIDE\Debug clean
& 'C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845\tools\bin\make.exe' -C STM32CubeIDE\Debug all
```

Alternative:

- Open the project in STM32CubeIDE
- Use the `Debug` configuration
- Run a clean build

Pass criteria:

- Build completes successfully
- Expected outputs exist:
  - [ups_bringup.elf](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Debug/ups_bringup.elf)
  - [ups_bringup.map](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Debug/ups_bringup.map)
  - [ups_bringup.list](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Debug/ups_bringup.list)

### 3. Artifact review

Review:

- [ups_bringup.map](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Debug/ups_bringup.map) for section layout and unexpected size growth
- [ups_bringup.list](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Debug/ups_bringup.list) for symbol presence and disassembly generation

Pass criteria:

- New validation support symbols are present:
  - `g_validation_debug`
  - `validation_debug_init`
  - `validation_debug_capture`
- No obvious link failures or missing object files

### 4. Static configuration review

Verify in source that the baseline values match the checklist:

- [board_config.c](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Src/board_config.c)
- [app_supervisor.h](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/Core/Inc/app_supervisor.h)

Pass criteria:

- The configured values match the Expected Phase-1 Configuration section
- The supervisor states and faults required by this checklist exist in the code

## Stage 2: Attached-Board Validation

### 1. Flash the firmware

Use either STM32CubeIDE with [ups_bringup.launch](C:/Users/john/OneDrive/Documents/GitHub/UPS/STM32CubeIDE/ups_bringup.launch) or CubeProgrammer CLI.

CubeIDE path:

- Open the project
- Launch `ups_bringup`
- Confirm the target halts at `main`
- Resume execution

CLI path, if preferred:

```powershell
.\scripts\flash_debug.ps1
```

Pass criteria:

- Flash download verifies successfully
- The target does not remain trapped in `Error_Handler`

### 2. Add debugger watches

Add these live expressions or watch entries:

- `g_validation_debug.magic`
- `g_validation_debug.capture_count`
- `g_validation_debug.error_handler_entered`
- `g_validation_debug.state_name`
- `g_validation_debug.fault_name`
- `g_validation_debug.status.state`
- `g_validation_debug.status.last_fault`
- `g_validation_debug.status.bq_present`
- `g_validation_debug.status.mpq_present`
- `g_validation_debug.status.prochot_asserted`
- `g_validation_debug.status.chrg_ok`
- `g_validation_debug.status.vsel1_level`
- `g_validation_debug.status.vsel2_level`
- `g_validation_debug.bq_config_matches`
- `g_validation_debug.mpq_config_matches`
- `g_validation_debug.gpio_vsel_matches`
- `g_validation_debug.nominal_run_reached`

Sanity check:

- `magic` should equal `0x55505356`
- `capture_count` should increase while the firmware runs

### 3. Nominal boot validation

Observe power-up and runtime behavior.

Pass criteria:

- LED shows short startup blink behavior before steady runtime behavior
- `error_handler_entered == false`
- `state_name` progresses through:
  - `APP_BOOT`
  - `APP_WAIT_POWER_STABLE`
  - `APP_PROBE_DEVICES`
  - `APP_READ_BASELINE`
  - `APP_CONFIGURE_BQ`
  - `APP_CONFIGURE_MPQ`
  - `APP_RUN`
- `fault_name == APP_FAULT_NONE`
- `bq_present == true`
- `mpq_present == true`
- `nominal_run_reached == true`
- `last_successful_poll_ms` advances over time

### 4. Register/configuration validation

Inspect these watch values after the firmware reaches `APP_RUN`:

- `g_validation_debug.bq_expected.option0_matches`
- `g_validation_debug.bq_expected.option1_matches`
- `g_validation_debug.bq_expected.charge_current_matches`
- `g_validation_debug.bq_expected.charge_voltage_matches`
- `g_validation_debug.bq_expected.input_voltage_matches`
- `g_validation_debug.bq_expected.iin_host_matches`
- `g_validation_debug.mpq_expected.pdo_type_matches`
- `g_validation_debug.mpq_expected.pdo_i1_matches`
- `g_validation_debug.mpq_expected.ctl1_matches`
- `g_validation_debug.mpq_expected.ctl2_matches`
- `g_validation_debug.mpq_expected.ctl3_matches`
- `g_validation_debug.mpq_expected.ctl4_matches`

Pass criteria:

- `bq_config_matches == true`
- `mpq_config_matches == true`
- `gpio_vsel_matches == true`

Notes:

- `gpio_vsel_matches` currently expects both `VSEL1` and `VSEL2` to read high after MPQ configuration
- If the board intentionally inverts or delays these signals, record the discrepancy and treat it as a hardware/firmware follow-up

### 5. Device identity validation

Inspect:

- `g_validation_debug.status.bq.manufacturer_id`
- `g_validation_debug.status.bq.device_id`
- `g_validation_debug.status.bq.device_id_valid`
- `g_validation_debug.status.mpq.id`

Pass criteria:

- BQ manufacturer/device ID is valid
- MPQ ID passes the probe logic and the supervisor remains in `APP_RUN`

### 6. Idle stability validation

Let the system run for at least 2 minutes.

Pass criteria:

- State remains `APP_RUN`
- `fault_name` remains `APP_FAULT_NONE`
- `capture_count` continues increasing
- No spontaneous transition to `APP_FAULT_HOLD`
- Repeated register match flags stay true

## Stage 3: Fault-Path Validation

Perform only the scenarios that are safe with the currently attached hardware.

### 1. PROCHOT assertion

Method:

- Assert `PROCHOT_N` low if this can be done safely

Pass criteria:

- `fault_name == APP_FAULT_PROCHOT_ASSERTED`
- `state_name == APP_FAULT_HOLD`
- LED enters fast-blink fault behavior

### 2. BQ absent or inaccessible

Method options:

- Power up with the BQ device unavailable
- Hold the bus/device in a state that makes probe fail

Pass criteria:

- `fault_name == APP_FAULT_BQ_NOT_PRESENT`
- `state_name == APP_FAULT_HOLD`

### 3. MPQ absent or inaccessible

Method options:

- Power up with the MPQ device unavailable
- Hold the bus/device in a state that makes probe fail

Pass criteria:

- `fault_name == APP_FAULT_MPQ_NOT_PRESENT`
- `state_name == APP_FAULT_HOLD`

### 4. Runtime I2C failure

Method options:

- Disrupt the I2C bus only after nominal boot reaches `APP_RUN`

Pass criteria:

- `fault_name == APP_FAULT_I2C`
- `state_name == APP_FAULT_HOLD`
- Polling continues at the slower fault interval

### 5. BQ status fault

Method:

- Induce a charger-status fault only if there is a safe, understood way to do it on the hardware

Pass criteria:

- `fault_name == APP_FAULT_BQ_STATUS`
- `state_name == APP_FAULT_HOLD`

### 6. Fault latch behavior

For any fault scenario above:

Pass criteria:

- Fault remains latched in `APP_FAULT_HOLD`
- `last_fault` does not clear spontaneously
- The system uses the `fault_poll_interval_ms` cadence instead of the normal run interval

## Stage 4: Bench Extension Tests

These are deferred until instruments are available.

### Power-path and charger characterization

- Measure actual charge voltage against the programmed BQ charge voltage setting
- Measure charge current under controlled battery conditions
- Measure input current limiting behavior against the programmed limit
- Verify input voltage limit behavior under sagging source conditions

### USB-C / PD characterization

- Verify MPQ advertised current behavior under controlled sink/load conditions
- Verify `VSEL1` and `VSEL2` electrical behavior with scope or logic analyzer
- Capture control/status register behavior during source attach/detach

### Disturbance and recovery tests

- Brownout and source interruption
- Cable insertion/removal during runtime
- Repeated cold boots
- Long-duration soak test

Evidence:

- Scope captures
- Load settings
- Supply settings
- Exact fault/result notes

## Exit Criteria

The current validation pass is considered complete when:

- Software-only validation passes
- Attached-board nominal boot reaches `APP_RUN`
- Identity and configuration checks pass using `g_validation_debug`
- At least one safe fault-path validation has been exercised and captured
- Remaining unexecuted bench-only items are explicitly marked as deferred, not omitted
