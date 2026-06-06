// =============================================================================
// Big Drippa — ESP32 Drip Irrigation Controller
// Remote-only: pump is triggered via Firebase RTDB from the web app.
//
// Independent hardware safety net: a MOS hardware timer wired in SERIES with the
// pump cuts power even if this firmware hangs. The software cap below
// (MAX_PUMP_ON_SECONDS) is only a SECONDARY belt to that hardware suspenders —
// never the sole protection.
//
// Board: ESP32 WROOM DevKit.
// =============================================================================

#include "config.h"
#include "secrets.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE
#include <FirebaseClient.h>

// ---- Pump state -------------------------------------------------------------
static bool          pumpRunning   = false;
static unsigned long pumpStartedMs = 0;
static const char   *lastTrigger   = "";
static time_t        pumpStartedEpoch = 0;

static const unsigned long MAX_ON_MS = (unsigned long)MAX_PUMP_ON_SECONDS * 1000UL;

// ---- Firebase state ---------------------------------------------------------
static WiFiClientSecure  ssl_client;
static AsyncClientClass  aClient(ssl_client);
static UserAuth          user_auth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD, 3000);
static FirebaseApp       app;
static RealtimeDatabase  Database;
static bool              firebaseReady      = false;
static unsigned long     lastFirebasePollMs = 0;

// ---- Time -------------------------------------------------------------------
static bool timeReady = false;

void syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("[ntp] Syncing time");
  int retries = 0;
  while (time(nullptr) < 100000 && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();
  timeReady = time(nullptr) >= 100000;
  if (timeReady) {
    Serial.printf("[ntp] Time synced: %ld\n", (long)time(nullptr));
  } else {
    Serial.println("[ntp] FAILED — timestamps will be 0");
  }
}

time_t nowEpoch() {
  return timeReady ? time(nullptr) : 0;
}

// -----------------------------------------------------------------------------
// Pump control
// -----------------------------------------------------------------------------
void startPump(const char *triggerSource) {
  pumpRunning      = true;
  pumpStartedMs    = millis();
  lastTrigger      = triggerSource;
  pumpStartedEpoch = nowEpoch();
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, HIGH);

  // Arm the MOS hardware timer with a short pulse.
  digitalWrite(TIMER_TRIGGER_PIN, HIGH);
  delay(100);
  digitalWrite(TIMER_TRIGGER_PIN, LOW);

  Serial.printf("[pump] ON  (trigger=%s)\n", triggerSource);

  // Write running status immediately
  if (firebaseReady) {
    Database.set<bool>(aClient, "/status/pumpRunning", true);
  }
}

void stopPump(const char *reason) {
  unsigned long ranForS = (millis() - pumpStartedMs) / 1000UL;
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);
  pumpRunning = false;
  Serial.printf("[pump] OFF (reason=%s, ran=%lus)\n", reason, ranForS);

  if (firebaseReady) {
    Database.set<bool>(aClient, "/status/pumpRunning", false);

    // Write history entry
    String json = "{";
    json += "\"trigger\":\"" + String(lastTrigger) + "\",";
    json += "\"startedAt\":" + String((long)pumpStartedEpoch) + ",";
    json += "\"durationSec\":" + String((unsigned long)ranForS) + ",";
    json += "\"stoppedReason\":\"" + String(reason) + "\"";
    json += "}";
    String path = "/history/" + String((long)nowEpoch());
    Database.set<object_t>(aClient, path.c_str(), object_t(json.c_str()));
  }
}

// ---- WiFi -------------------------------------------------------------------
void connectWiFi() {
  Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] FAILED — pump can only be triggered via Firebase");
  }
}

// ---- Firebase ---------------------------------------------------------------
void initFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[firebase] Skipped — no WiFi");
    return;
  }

  ssl_client.setInsecure();

  Serial.println("[firebase] Initializing...");
  initializeApp(aClient, app, getAuth(user_auth));

  unsigned long start = millis();
  while (app.isInitialized() && !app.ready() && millis() - start < 10000) {
    delay(100);
  }

  firebaseReady = app.ready();
  if (firebaseReady) {
    app.getApp<RealtimeDatabase>(Database);
    Database.url(FIREBASE_DATABASE_URL);
    Serial.println("[firebase] Authenticated and ready");
  } else {
    Serial.println("[firebase] Auth FAILED");
  }
}

void writeStatus() {
  String json = "{";
  json += "\"pumpRunning\":" + String(pumpRunning ? "true" : "false") + ",";
  json += "\"lastUpdated\":" + String((long)nowEpoch()) + ",";
  json += "\"uptimeMs\":" + String(millis()) + ",";
  json += "\"wifiRSSI\":" + String(WiFi.RSSI());
  json += "}";
  Database.set<object_t>(aClient, "/status", object_t(json.c_str()));
}

void syncFirebase() {
  if (!firebaseReady || WiFi.status() != WL_CONNECTED || !app.ready()) return;

  app.loop();

  unsigned long now = millis();
  if (now - lastFirebasePollMs < FIREBASE_POLL_INTERVAL_MS) return;
  lastFirebasePollMs = now;

  // Poll for manual run command
  bool manualRun = Database.get<bool>(aClient, "/commands/manualRun");

  if (aClient.lastError().code() != 0) return;

  if (manualRun) {
    Serial.println("[firebase] Manual run command received");
    Database.set<bool>(aClient, "/commands/manualRun", false);

    if (!pumpRunning) {
      startPump("firebase");
    } else {
      Serial.println("[firebase] Pump already running — ignoring");
    }
  }

  // Write device status
  writeStatus();
}

// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  digitalWrite(TIMER_TRIGGER_PIN, LOW);
  pinMode(TIMER_TRIGGER_PIN, OUTPUT);
  digitalWrite(TIMER_TRIGGER_PIN, LOW);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.println();
  Serial.println("Big Drippa - ESP32 Drip Irrigation Controller");
  Serial.printf("Safety cap: %d s\n", MAX_PUMP_ON_SECONDS);

  connectWiFi();
  syncTime();
  initFirebase();
}

// -----------------------------------------------------------------------------
void loop() {
  syncFirebase();

  if (pumpRunning) {
    unsigned long onFor = millis() - pumpStartedMs;
    if (onFor >= MAX_ON_MS) {
      stopPump("SAFETY-CAP");
    }
  }
}
