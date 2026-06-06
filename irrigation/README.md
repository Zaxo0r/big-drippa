# Big Drippa — ESP32 Firmware (`irrigation`)

Firmware for the Big Drippa drip-irrigation controller: the pump is triggered remotely via
Firebase RTDB from a web app. See the [repo overview](../README.md) for the big picture.

> **Why this folder is named `irrigation/`:** the Arduino toolchain requires a sketch to live
> in a folder with the same name as its `.ino`, so `irrigation.ino` must sit in `irrigation/`.

## Goal

Keep a balcony garden alive during multi-day absences. One zone waters all plants
simultaneously; per-plant flow is tuned with adjustable drippers.

## Hardware

| Part | Notes |
|------|-------|
| ESP32 WROOM DevKit | Main controller (chosen over an Uno R3 because Phase 2 needs WiFi) |
| 12 V membrane pump (RUNCCI-YUN R385 type, 1.5–1.8 L/min) | Sits dry on the reservoir edge; suction tube into the water |
| 3 V relay module (SRD-03VDC, **high-level trigger**) | Switches pump power |
| MOS hardware timer (DC 5–36 V, up to 999 min) | **Independent safety cutoff, in series with the pump** — fires even if the ESP32 hangs |
| 12 V / 3 A wall adapter + DC barrel-jack screw terminal | Pump power |
| 30 L reservoir | Placed low; drippers sit **above** the water line to prevent siphoning |
| VooGenzek 4/7 mm dripper kit | Per-plant flow tuning |

## Pin table

| Signal | ESP32 GPIO | Connects to | Notes |
|--------|-----------|-------------|-------|
| Pump relay | `GPIO 23` | Relay IN | High-level trigger; HIGH = pump on |
| Timer trigger | `GPIO 22` | MOS timer trigger | Short HIGH pulse arms the hardware timer |
| Status LED | `GPIO 2` | Onboard LED | Lit while the pump runs |

Pins are defined in [`config.h`](config.h).

## Wiring summary

- The pump is powered from the 12 V adapter. The **MOS hardware timer and the relay are in
  series** with the pump, so either one can cut power independently.
- The relay is a **high-level trigger**: the pump runs when `RELAY_PIN` is HIGH. The firmware
  forces the pin LOW at boot so the pump can never pulse on power-up.
- The pump sits dry on the edge of the reservoir with its suction tube in the water; the
  drippers are positioned **higher than the water level** to prevent siphoning when the pump
  is off.

Full notes: [`docs/wiring.md`](docs/wiring.md). First-time bring-up:
[`docs/startup.md`](docs/startup.md).

## Configuration

Tune everything in [`config.h`](config.h):

- `RELAY_PIN`, `TIMER_TRIGGER_PIN`, `STATUS_LED_PIN`
- `MAX_PUMP_ON_SECONDS` — software safety cap; keep it **below** the MOS hardware-timer cutoff.
- `FIREBASE_POLL_INTERVAL_MS` — how often the ESP32 checks Firebase for commands (default 3 s).

## Build & flash

1. Install the **ESP32 Arduino core** (Boards Manager → "esp32" by Espressif).
2. Open `irrigation/irrigation.ino` in the Arduino IDE.
3. Select board **ESP32 Dev Module** (ESP32 WROOM DevKit) and the correct serial port.
4. Upload over USB.
5. Open Serial Monitor at **115200 baud** to watch logs.

Copy `secrets.h.example` → `secrets.h` and fill in your WiFi and Firebase credentials
(`secrets.h` is gitignored). See the example file for details on each field.

## Libraries

- **ESP32 Arduino core** (built-in WiFi)
- [**FirebaseClient** (Mobizt)](https://github.com/mobizt/FirebaseClient) — install via Arduino
  Library Manager (search "FirebaseClient"). This replaces the deprecated Firebase-ESP-Client.

## Safety

- The **MOS hardware timer in series with the pump** is the primary safety net — it cuts the
  pump even if the firmware locks up.
- `MAX_PUMP_ON_SECONDS` in `config.h` is a secondary, software belt: `loop()` force-stops the
  pump if a cycle ever runs past it.
- The relay defaults **OFF at boot** via a glitch-safe init sequence (the pin is driven LOW
  before it is switched to an output).

## Scope

| Feature | Status |
|---|---|
| Watering | Manual trigger via Firebase `/commands` |
| Network | WiFi |
| Cloud | Firebase RTDB, Google Sign-In |
| Updates | USB flashing |
| Web app | Static site in sibling [`web/`](../web/) folder |
