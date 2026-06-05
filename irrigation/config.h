#pragma once
// =============================================================================
// config.h — Big Drippa / ESP32 Drip Irrigation Controller
//
// Pin assignments, watering schedule, and safety limits. Everything here is a
// #define constant so values are easy to tune without hunting through the .ino.
// =============================================================================

// ---- Pin assignments --------------------------------------------------------
// NOTE: placeholders — confirm against docs/wiring.md once the hardware is wired.
//
// The relay module is a HIGH-LEVEL TRIGGER: the pump runs when RELAY_PIN is
// HIGH and is OFF when it is LOW. The firmware keeps this pin LOW at boot (see
// irrigation.ino) so the pump can never pulse on power-up.
#define RELAY_PIN        26   // pump relay control output
#define STATUS_LED_PIN    2   // onboard LED on most ESP32 DevKits (lit while pump runs)

// ---- Watering schedule (Phase 1, local) -------------------------------------
// How often to water, and how long the pump runs each cycle. Tune to taste.
#define WATERING_INTERVAL_HOURS     12   // time between watering cycles
#define WATERING_DURATION_SECONDS   90   // pump-on time per cycle

// ---- Safety -----------------------------------------------------------------
// Software belt to the MOS hardware timer's suspenders: an absolute ceiling on
// how long the pump may stay on in a single cycle. If the normal end-of-cycle
// stop is ever missed, loop() force-stops the pump once this many seconds pass.
//
// Keep this comfortably BELOW the MOS hardware-timer cutoff so the independent
// hardware timer always remains the last line of defence.
#define MAX_PUMP_ON_SECONDS        180
