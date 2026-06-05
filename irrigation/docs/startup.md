# First-time startup checklist — Big Drippa

> **Placeholder.** Flesh out with real values and gotchas after the first live run.

## Before power-on

- [ ] Reservoir filled (30 L); pump suction tube submerged.
- [ ] Pump sits dry on the reservoir edge (membrane pump — do not submerge the pump body).
- [ ] Drippers positioned **above** the water line (prevents siphoning when off).
- [ ] MOS hardware timer set to a safe maximum run time (the failsafe cutoff).
- [ ] Relay wired to `RELAY_PIN` (high-level trigger); MOS timer + relay in series with pump.

## Flash

- [ ] `config.h` reviewed: pins, `WATERING_INTERVAL_HOURS`, `WATERING_DURATION_SECONDS`,
      `MAX_PUMP_ON_SECONDS` (kept below the MOS timer cutoff).
- [ ] Upload `irrigation.ino` over USB (see [../README.md](../README.md)).
- [ ] Serial Monitor @ 115200 — confirm the boot banner and schedule print.

## First run

- [ ] **Confirm the pump is OFF at boot** (relay LED off, no pump hum).
- [ ] Trigger one cycle (temporarily lower the interval, or wait for the schedule) and watch
      the pump start/stop in the serial log.
- [ ] Check flow at **each dripper**; adjust the per-plant drippers as needed.
- [ ] Verify the cycle stops at `WATERING_DURATION_SECONDS`.
- [ ] Verify the MOS timer cuts power if the pump ever overruns.

## Tune

- [ ] Set the real watering interval and duration in `config.h`.
- [ ] Re-flash and leave running.
