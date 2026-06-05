// =============================================================================
// Big Drippa — ESP32 Drip Irrigation Controller
// Phase 1: local-only scheduled watering. No network.
//
// Independent hardware safety net: a MOS hardware timer wired in SERIES with the
// pump cuts power even if this firmware hangs. The software cap below
// (MAX_PUMP_ON_SECONDS) is only a SECONDARY belt to that hardware suspenders —
// never the sole protection.
//
// Board: ESP32 WROOM DevKit. Phase 1 flashes over USB (Arduino IDE / arduino-cli).
// =============================================================================

#include "config.h"

// Phase 2: WiFi + Firebase credentials live here (copy secrets.h.example).
// Left commented so a fresh Phase 1 checkout compiles without a secrets.h.
// #include "secrets.h"

// Phase 2: over-the-air updates. Stubbed, not enabled — Phase 1 flashes over USB.
// #include <ArduinoOTA.h>

// ---- Pump state -------------------------------------------------------------
static bool          pumpRunning   = false;
static unsigned long pumpStartedMs = 0;   // millis() when the current cycle began
static unsigned long lastCycleMs   = 0;   // millis() of the last cycle's start

// Derived from config.h. Unsigned long math keeps the milli counts overflow-safe.
static const unsigned long INTERVAL_MS = (unsigned long)WATERING_INTERVAL_HOURS   * 60UL * 60UL * 1000UL;
static const unsigned long DURATION_MS = (unsigned long)WATERING_DURATION_SECONDS * 1000UL;
static const unsigned long MAX_ON_MS   = (unsigned long)MAX_PUMP_ON_SECONDS       * 1000UL;

// -----------------------------------------------------------------------------
// Pump control — every relay write goes through these two helpers so the
// HIGH/LOW polarity and logging live in exactly one place.
// -----------------------------------------------------------------------------
void startPump(const char *triggerSource) {
  pumpRunning   = true;
  pumpStartedMs = millis();
  digitalWrite(RELAY_PIN, HIGH);          // HIGH-level trigger → pump ON
  digitalWrite(STATUS_LED_PIN, HIGH);
  Serial.printf("[pump] ON  (trigger=%s)\n", triggerSource);
}

void stopPump(const char *reason) {
  unsigned long ranForS = (millis() - pumpStartedMs) / 1000UL;
  digitalWrite(RELAY_PIN, LOW);           // LOW → pump OFF
  digitalWrite(STATUS_LED_PIN, LOW);
  pumpRunning = false;
  Serial.printf("[pump] OFF (reason=%s, ran=%lus)\n", reason, ranForS);
}

// ---- Phase 2 stubs (intentionally empty for now) ----------------------------
void connectWiFi() {
  // Phase 2: join WiFi using WIFI_SSID / WIFI_PASSWORD from secrets.h.
}

void syncFirebase() {
  // Phase 2: read /commands (manual run / schedule changes) and write /history
  // telemetry (last run time, duration, trigger source) via the Mobizt
  // Firebase-ESP-Client library.
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  // --- Glitch-safe relay init (IMPORTANT) ---
  // The relay is a HIGH-level trigger, so the pump runs whenever RELAY_PIN is
  // HIGH. Drive the output latch LOW *before* switching the pin to OUTPUT so the
  // pin comes up LOW and never glitches HIGH — a stray HIGH here would briefly
  // energize the pump on every boot.
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);           // explicit OFF now that it is an output

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.println();
  Serial.println("Big Drippa - ESP32 Drip Irrigation Controller (Phase 1)");
  Serial.printf("Schedule: every %d h, run %d s (safety cap %d s)\n",
                WATERING_INTERVAL_HOURS, WATERING_DURATION_SECONDS, MAX_PUMP_ON_SECONDS);

  // connectWiFi();   // Phase 2
  // syncFirebase();  // Phase 2

  // First cycle happens one full interval from boot. To water immediately on
  // boot while bench-testing, use:  lastCycleMs = millis() - INTERVAL_MS;
  lastCycleMs = millis();
}

// -----------------------------------------------------------------------------
void loop() {
  unsigned long now = millis();

  // ArduinoOTA.handle();  // Phase 2
  // syncFirebase();       // Phase 2

  if (pumpRunning) {
    unsigned long onFor = now - pumpStartedMs;
    if (onFor >= MAX_ON_MS) {
      stopPump("SAFETY-CAP");             // hard ceiling — always checked first
    } else if (onFor >= DURATION_MS) {
      stopPump("duration-reached");       // normal end of cycle
    }
  } else {
    if (now - lastCycleMs >= INTERVAL_MS) {
      lastCycleMs = now;
      startPump("schedule");
    }
  }
}
