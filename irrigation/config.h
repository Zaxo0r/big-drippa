#pragma once
// =============================================================================
// config.h — Big Drippa / ESP32 Drip Irrigation Controller
//
// Pin assignments, safety limits, and Firebase polling. Everything here is a
// #define constant so values are easy to tune without hunting through the .ino.
// =============================================================================

// ---- Pin assignments --------------------------------------------------------
//
// The relay module is a HIGH-LEVEL TRIGGER: the pump runs when RELAY_PIN is
// HIGH and is OFF when it is LOW. The firmware keeps this pin LOW at boot (see
// irrigation.ino) so the pump can never pulse on power-up.
#define RELAY_PIN            23   // pump relay control output
#define TIMER_TRIGGER_PIN    22   // MOS hardware timer — short HIGH pulse arms it
#define STATUS_LED_PIN        2   // onboard LED on most ESP32 DevKits (lit while pump runs)

// ---- Safety -----------------------------------------------------------------
// Software belt to the MOS hardware timer's suspenders: an absolute ceiling on
// how long the pump may stay on in a single cycle. If the stop is ever missed,
// loop() force-stops the pump once this many seconds pass.
//
// Keep this comfortably BELOW the MOS hardware-timer cutoff so the independent
// hardware timer always remains the last line of defence.
//
// This also acts as the effective per-cycle run length. If you tune it, update
// WATERING_DURATION_SECONDS in web/index.html so the web app's live count-up /
// progress bar still matches.
#define MAX_PUMP_ON_SECONDS        120

// ---- Firebase polling -------------------------------------------------------
// How often the ESP32 checks Firebase RTDB for a manual-run command.
#define FIREBASE_POLL_INTERVAL_MS  3000

// ---- WiFi reconnection ------------------------------------------------------
// The ESP32 can lose its WiFi association after a while (e.g. a router dropping
// an idle station). These control how the firmware recovers without a manual
// power-cycle. See ensureConnectivity() in irrigation.ino.
#define WIFI_RECONNECT_INTERVAL_MS  5000        // how often to retry while offline
#define WIFI_OFFLINE_REBOOT_MS      600000UL    // reboot after ~10 min offline (0 = never)
