from __future__ import annotations

import argparse
import sys
import unittest
from dataclasses import dataclass, field
from enum import IntEnum


class AppState(IntEnum):
    APP_BOOT = 0
    APP_WAIT_POWER_STABLE = 1
    APP_PROBE_DEVICES = 2
    APP_READ_BASELINE = 3
    APP_CONFIGURE_BQ = 4
    APP_CONFIGURE_MPQ = 5
    APP_RUN = 6
    APP_FAULT_HOLD = 7


class AppFaultCode(IntEnum):
    APP_FAULT_NONE = 0
    APP_FAULT_PROCHOT_ASSERTED = 1
    APP_FAULT_BQ_NOT_PRESENT = 2
    APP_FAULT_MPQ_NOT_PRESENT = 3
    APP_FAULT_BQ_CONFIG = 4
    APP_FAULT_MPQ_CONFIG = 5
    APP_FAULT_BQ_STATUS = 6
    APP_FAULT_I2C = 7
    APP_FAULT_CONFIG_VERIFY = 8


class BqResult(IntEnum):
    BQ25730_OK = 0
    BQ25730_ERROR_I2C = 1
    BQ25730_ERROR_VERIFY = 2
    BQ25730_ERROR_INVALID_ARG = 3


class MpqResult(IntEnum):
    MPQ5031_OK = 0
    MPQ5031_ERROR_I2C = 1
    MPQ5031_ERROR_VERIFY = 2
    MPQ5031_ERROR_INVALID_ARG = 3


@dataclass
class BoardConfig:
    """Subset of board_config_t that affects the supervisor state machine timing."""

    startup_delay_ms: int = 15
    run_poll_interval_ms: int = 250
    fault_poll_interval_ms: int = 1000


@dataclass
class BqSnapshot:
    charger_status: int = 0


@dataclass
class MpqSnapshot:
    status1: int = 0
    status2: int = 0


@dataclass
class AppSupervisorStatus:
    state: AppState = AppState.APP_BOOT
    last_fault: AppFaultCode = AppFaultCode.APP_FAULT_NONE
    bq_present: bool = False
    mpq_present: bool = False
    prochot_asserted: bool = False
    chrg_ok: bool = False
    vsel1_level: bool = False
    vsel2_level: bool = False
    last_transition_ms: int = 0
    last_successful_poll_ms: int = 0
    bq: BqSnapshot = field(default_factory=BqSnapshot)
    mpq: MpqSnapshot = field(default_factory=MpqSnapshot)


@dataclass
class GpioInputs:
    """Digital pin levels as seen by the MCU, not schematic-level signal names."""

    prochot_n_high: bool = True
    chrg_ok_high: bool = True
    vsel1_high: bool = False
    vsel2_high: bool = False


class FakeBq:
    """Minimal BQ model that can inject probe, read, config, and status-fault outcomes."""

    def __init__(self) -> None:
        self.present = True
        self.configure_result = BqResult.BQ25730_OK
        self.probe_result = BqResult.BQ25730_OK
        self.read_result = BqResult.BQ25730_OK
        self.baseline_snapshot = BqSnapshot()
        self.runtime_snapshot = BqSnapshot()
        self.read_calls = 0

    def probe(self, snapshot: BqSnapshot) -> BqResult:
        if not self.present:
            return BqResult.BQ25730_ERROR_I2C
        if self.probe_result != BqResult.BQ25730_OK:
            return self.probe_result
        snapshot.charger_status = self.baseline_snapshot.charger_status
        return BqResult.BQ25730_OK

    def read_snapshot(self, snapshot: BqSnapshot) -> BqResult:
        self.read_calls += 1
        if self.read_result != BqResult.BQ25730_OK:
            return self.read_result
        snapshot.charger_status = self.runtime_snapshot.charger_status
        return BqResult.BQ25730_OK

    def configure_phase1(self, snapshot: BqSnapshot) -> BqResult:
        if self.configure_result != BqResult.BQ25730_OK:
            return self.configure_result
        snapshot.charger_status = self.runtime_snapshot.charger_status
        return BqResult.BQ25730_OK


class FakeMpq:
    """Minimal MPQ model used to exercise source-controller paths and runtime polling."""

    def __init__(self) -> None:
        self.present = True
        self.configure_result = MpqResult.MPQ5031_OK
        self.probe_result = MpqResult.MPQ5031_OK
        self.read_baseline_result = MpqResult.MPQ5031_OK
        self.read_runtime_result = MpqResult.MPQ5031_OK
        self.baseline_snapshot = MpqSnapshot()
        self.runtime_snapshot = MpqSnapshot()
        self.runtime_read_calls = 0

    def probe(self, snapshot: MpqSnapshot) -> MpqResult:
        if not self.present:
            return MpqResult.MPQ5031_ERROR_I2C
        if self.probe_result != MpqResult.MPQ5031_OK:
            return self.probe_result
        snapshot.status1 = self.baseline_snapshot.status1
        snapshot.status2 = self.baseline_snapshot.status2
        return MpqResult.MPQ5031_OK

    def read_baseline(self, snapshot: MpqSnapshot) -> MpqResult:
        if self.read_baseline_result != MpqResult.MPQ5031_OK:
            return self.read_baseline_result
        snapshot.status1 = self.baseline_snapshot.status1
        snapshot.status2 = self.baseline_snapshot.status2
        return MpqResult.MPQ5031_OK

    def read_runtime_status(self, snapshot: MpqSnapshot) -> MpqResult:
        self.runtime_read_calls += 1
        if self.read_runtime_result != MpqResult.MPQ5031_OK:
            return self.read_runtime_result
        snapshot.status1 = self.runtime_snapshot.status1
        snapshot.status2 = self.runtime_snapshot.status2
        return MpqResult.MPQ5031_OK

    def configure_phase1(self, snapshot: MpqSnapshot) -> MpqResult:
        if self.configure_result != MpqResult.MPQ5031_OK:
            return self.configure_result
        snapshot.status1 = self.runtime_snapshot.status1
        snapshot.status2 = self.runtime_snapshot.status2
        return MpqResult.MPQ5031_OK


def bq_snapshot_has_fault(snapshot: BqSnapshot) -> bool:
    """Mirror the firmware rule: any low-byte charger status fault bit trips the supervisor."""

    return (snapshot.charger_status & 0x00FF) != 0


class AppSupervisorSim:
    """Python mirror of app_supervisor.c for MCU-only and pre-PCB validation."""

    def __init__(self, cfg: BoardConfig, gpio: GpioInputs, bq: FakeBq, mpq: FakeMpq) -> None:
        self.cfg = cfg
        self.gpio = gpio
        self.bq = bq
        self.mpq = mpq
        self.status = AppSupervisorStatus()
        self.initialized = False
        self.last_chrg_ok_sample = False
        self.last_mpq_status1 = 0
        self.last_mpq_status2 = 0
        self.led_on = False

    def init(self) -> None:
        """Mirror app_supervisor_init by clearing state and sampling the initial GPIO levels."""

        self.status = AppSupervisorStatus()
        self.initialized = True
        self._sample_gpio()
        self.last_chrg_ok_sample = self.status.chrg_ok
        self.led_on = False

    def _sample_gpio(self) -> None:
        """Interpret raw pin levels exactly the way the firmware does."""

        self.status.prochot_asserted = not self.gpio.prochot_n_high
        self.status.chrg_ok = self.gpio.chrg_ok_high
        self.status.vsel1_level = self.gpio.vsel1_high
        self.status.vsel2_level = self.gpio.vsel2_high

    def _set_state(self, state: AppState, now_ms: int) -> None:
        self.status.state = state
        self.status.last_transition_ms = now_ms

    def _enter_fault(self, fault: AppFaultCode, now_ms: int) -> None:
        self.status.last_fault = fault
        self._set_state(AppState.APP_FAULT_HOLD, now_ms)

    def _apply_led(self, now_ms: int) -> None:
        """Keep the same LED cadence rules so visible bring-up behavior can be reasoned about."""

        if self.status.state == AppState.APP_RUN:
            self.led_on = True
        elif self.status.state == AppState.APP_FAULT_HOLD:
            self.led_on = (now_ms % 250) < 125
        else:
            self.led_on = (now_ms % 500) < 75

    def _update_bq_faults(self, now_ms: int) -> None:
        if bq_snapshot_has_fault(self.status.bq):
            self._enter_fault(AppFaultCode.APP_FAULT_BQ_STATUS, now_ms)

    def _handle_runtime_poll(self, now_ms: int) -> None:
        """Mirror the firmware's charger-first runtime poll ordering."""

        bq_result = self.bq.read_snapshot(self.status.bq)
        if bq_result != BqResult.BQ25730_OK:
            self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
            return

        self._update_bq_faults(now_ms)
        if self.status.state == AppState.APP_FAULT_HOLD:
            return

        mpq_result = self.mpq.read_runtime_status(self.status.mpq)
        if mpq_result != MpqResult.MPQ5031_OK:
            self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
            return

        self.last_mpq_status1 = self.status.mpq.status1
        self.last_mpq_status2 = self.status.mpq.status2
        self.status.last_successful_poll_ms = now_ms

    def step(self, now_ms: int) -> None:
        """Single supervisor tick equivalent to one app_supervisor_step call."""

        if not self.initialized:
            return

        self._sample_gpio()
        if self.status.prochot_asserted and self.status.state != AppState.APP_FAULT_HOLD:
            self._enter_fault(AppFaultCode.APP_FAULT_PROCHOT_ASSERTED, now_ms)

        if self.status.state == AppState.APP_BOOT:
            self._set_state(AppState.APP_WAIT_POWER_STABLE, now_ms)
        elif self.status.state == AppState.APP_WAIT_POWER_STABLE:
            if now_ms - self.status.last_transition_ms >= self.cfg.startup_delay_ms:
                self._set_state(AppState.APP_PROBE_DEVICES, now_ms)
        elif self.status.state == AppState.APP_PROBE_DEVICES:
            bq_result = self.bq.probe(self.status.bq)
            if bq_result != BqResult.BQ25730_OK:
                self.status.bq_present = False
                self._enter_fault(AppFaultCode.APP_FAULT_BQ_NOT_PRESENT, now_ms)
            else:
                self.status.bq_present = True
                mpq_result = self.mpq.probe(self.status.mpq)
                if mpq_result != MpqResult.MPQ5031_OK:
                    self.status.mpq_present = False
                    self._enter_fault(AppFaultCode.APP_FAULT_MPQ_NOT_PRESENT, now_ms)
                else:
                    self.status.mpq_present = True
                    self._set_state(AppState.APP_READ_BASELINE, now_ms)
        elif self.status.state == AppState.APP_READ_BASELINE:
            bq_result = self.bq.read_snapshot(self.status.bq)
            if bq_result != BqResult.BQ25730_OK:
                self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
            else:
                mpq_result = self.mpq.read_baseline(self.status.mpq)
                if mpq_result != MpqResult.MPQ5031_OK:
                    self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
                else:
                    self.last_mpq_status1 = self.status.mpq.status1
                    self.last_mpq_status2 = self.status.mpq.status2
                    self._update_bq_faults(now_ms)
                    if self.status.state != AppState.APP_FAULT_HOLD:
                        self._set_state(AppState.APP_CONFIGURE_BQ, now_ms)
        elif self.status.state == AppState.APP_CONFIGURE_BQ:
            bq_result = self.bq.configure_phase1(self.status.bq)
            if bq_result == BqResult.BQ25730_ERROR_VERIFY:
                self._enter_fault(AppFaultCode.APP_FAULT_CONFIG_VERIFY, now_ms)
            elif bq_result == BqResult.BQ25730_ERROR_I2C:
                self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
            elif bq_result != BqResult.BQ25730_OK:
                self._enter_fault(AppFaultCode.APP_FAULT_BQ_CONFIG, now_ms)
            else:
                self._update_bq_faults(now_ms)
                if self.status.state != AppState.APP_FAULT_HOLD:
                    self._set_state(AppState.APP_CONFIGURE_MPQ, now_ms)
        elif self.status.state == AppState.APP_CONFIGURE_MPQ:
            mpq_result = self.mpq.configure_phase1(self.status.mpq)
            if mpq_result == MpqResult.MPQ5031_ERROR_VERIFY:
                self._enter_fault(AppFaultCode.APP_FAULT_CONFIG_VERIFY, now_ms)
            elif mpq_result == MpqResult.MPQ5031_ERROR_I2C:
                self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)
            elif mpq_result != MpqResult.MPQ5031_OK:
                self._enter_fault(AppFaultCode.APP_FAULT_MPQ_CONFIG, now_ms)
            else:
                self.last_mpq_status1 = self.status.mpq.status1
                self.last_mpq_status2 = self.status.mpq.status2
                self.status.last_successful_poll_ms = now_ms
                self._set_state(AppState.APP_RUN, now_ms)
        elif self.status.state == AppState.APP_RUN:
            if now_ms - self.status.last_successful_poll_ms >= self.cfg.run_poll_interval_ms:
                self._handle_runtime_poll(now_ms)
        elif self.status.state == AppState.APP_FAULT_HOLD:
            if now_ms - self.status.last_successful_poll_ms >= self.cfg.fault_poll_interval_ms:
                self.bq.read_snapshot(self.status.bq)
                self.mpq.read_runtime_status(self.status.mpq)
                self.status.last_successful_poll_ms = now_ms
        else:
            self._enter_fault(AppFaultCode.APP_FAULT_I2C, now_ms)

        if self.status.chrg_ok != self.last_chrg_ok_sample:
            self.last_chrg_ok_sample = self.status.chrg_ok

        self._apply_led(now_ms)


def advance(sim: AppSupervisorSim, duration_ms: int, step_ms: int = 10) -> None:
    """Drive the simulator with the same 10 ms cadence used by the firmware main loop."""

    for now_ms in range(0, duration_ms + step_ms, step_ms):
        sim.step(now_ms)


class SupervisorSimulationTests(unittest.TestCase):
    def make_sim(self) -> AppSupervisorSim:
        cfg = BoardConfig()
        gpio = GpioInputs()
        bq = FakeBq()
        mpq = FakeMpq()
        sim = AppSupervisorSim(cfg, gpio, bq, mpq)
        sim.init()
        return sim

    def test_nominal_boot_reaches_run(self) -> None:
        sim = self.make_sim()
        advance(sim, 100)
        self.assertEqual(sim.status.state, AppState.APP_RUN)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_NONE)
        self.assertTrue(sim.status.bq_present)
        self.assertTrue(sim.status.mpq_present)

    def test_prochot_low_faults_immediately(self) -> None:
        sim = self.make_sim()
        sim.gpio.prochot_n_high = False
        advance(sim, 20)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_PROCHOT_ASSERTED)
        self.assertFalse(sim.status.bq_present)
        self.assertFalse(sim.status.mpq_present)

    def test_missing_bq_faults_before_mpq_probe(self) -> None:
        sim = self.make_sim()
        sim.bq.present = False
        advance(sim, 50)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_BQ_NOT_PRESENT)
        self.assertFalse(sim.status.bq_present)

    def test_missing_mpq_faults_after_bq_probe(self) -> None:
        sim = self.make_sim()
        sim.mpq.present = False
        advance(sim, 50)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_MPQ_NOT_PRESENT)
        self.assertTrue(sim.status.bq_present)
        self.assertFalse(sim.status.mpq_present)

    def test_runtime_i2c_fault_after_run(self) -> None:
        sim = self.make_sim()
        advance(sim, 100)
        self.assertEqual(sim.status.state, AppState.APP_RUN)
        sim.bq.read_result = BqResult.BQ25730_ERROR_I2C
        sim.step(sim.status.last_successful_poll_ms + sim.cfg.run_poll_interval_ms)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_I2C)

    def test_bq_status_fault_from_runtime_snapshot(self) -> None:
        sim = self.make_sim()
        advance(sim, 100)
        self.assertEqual(sim.status.state, AppState.APP_RUN)
        sim.bq.runtime_snapshot.charger_status = 0x0001
        sim.step(sim.status.last_successful_poll_ms + sim.cfg.run_poll_interval_ms)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.status.last_fault, AppFaultCode.APP_FAULT_BQ_STATUS)

    def test_fault_hold_uses_fault_poll_interval(self) -> None:
        sim = self.make_sim()
        advance(sim, 100)
        sim.gpio.prochot_n_high = False
        sim.step(110)
        self.assertEqual(sim.status.state, AppState.APP_FAULT_HOLD)
        self.assertEqual(sim.bq.read_calls, 1)
        self.assertEqual(sim.mpq.runtime_read_calls, 0)
        sim.step(sim.status.last_successful_poll_ms + 900)
        self.assertEqual(sim.bq.read_calls, 1)
        sim.step(sim.status.last_successful_poll_ms + 1000)
        self.assertGreaterEqual(sim.bq.read_calls, 2)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run host-side supervisor simulation tests.")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show verbose unittest output.")
    args = parser.parse_args()

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SupervisorSimulationTests)
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
