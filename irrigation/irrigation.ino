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

// ---- WiFi connectivity ------------------------------------------------------
static unsigned long     offlineSinceMs     = 0;  // 0 = currently online
static unsigned long     lastWifiAttemptMs  = 0;

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
// Logs link transitions. The disconnect reason code is the key diagnostic if
// connectivity ever drops again — it tells us *why* the AP let go of us.
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] Got IP: %s (RSSI %d)\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[wifi] Disconnected (reason %d)\n",
                    info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

void connectWiFi() {
  Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // disable modem sleep — keeps the AP from dropping us
  WiFi.setAutoReconnect(true);   // let the core re-associate on its own
  WiFi.persistent(false);        // don't wear flash re-storing credentials
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
  if (aClient.lastError().code() != 0) {
    Serial.printf("[firebase] set(status) error: code=%d, msg=%s\n",
                  aClient.lastError().code(), aClient.lastError().message().c_str());
  }
}

void syncFirebase() {
  if (WiFi.status() != WL_CONNECTED || !app.isInitialized()) return;

  app.loop();                 // keep auth/token state machine alive so it can self-heal
  if (!app.ready()) return;   // not authenticated yet — skip DB I/O this cycle
  firebaseReady = true;

  unsigned long now = millis();
  if (now - lastFirebasePollMs < FIREBASE_POLL_INTERVAL_MS) return;
  lastFirebasePollMs = now;

  // Poll for manual run command
  bool manualRun = Database.get<bool>(aClient, "/commands/manualRun");

  if (aClient.lastError().code() != 0) {
    Serial.printf("[firebase] get(manualRun) error: code=%d, msg=%s\n",
                  aClient.lastError().code(), aClient.lastError().message().c_str());
    return;
  }

  if (manualRun) {
    Serial.println("[firebase] Manual run command received");
    Database.set<bool>(aClient, "/commands/manualRun", false);

    if (!pumpRunning) {
      startPump("firebase");
    } else {
      Serial.println("[firebase] Pump already running — ignoring");
    }
  }

  // Poll for stop command (remote "Stop pump" from the web app)
  bool stopCmd = Database.get<bool>(aClient, "/commands/stop");

  if (aClient.lastError().code() != 0) {
    Serial.printf("[firebase] get(stop) error: code=%d, msg=%s\n",
                  aClient.lastError().code(), aClient.lastError().message().c_str());
    return;
  }

  if (stopCmd) {
    Serial.println("[firebase] Stop command received");
    Database.set<bool>(aClient, "/commands/stop", false);

    if (pumpRunning) {
      stopPump("manual-stop");
    } else {
      Serial.println("[firebase] Pump not running — nothing to stop");
    }
  }

  // Write device status
  writeStatus();
}

// -----------------------------------------------------------------------------
// Non-blocking connectivity watchdog. Runs every loop: re-associates WiFi when
// it drops, rebuilds the Firebase session once the link returns, and reboots as
// a last resort if we stay offline too long. Reboot is safe — the relay defaults
// LOW at boot and the MOS hardware timer is independent, so it can never start
// the pump.
void ensureConnectivity() {
  if (WiFi.status() == WL_CONNECTED) {
    if (offlineSinceMs != 0) {                 // transition: link just came back
      offlineSinceMs = 0;
      Serial.println("[wifi] Link restored");
      if (!app.ready()) {                      // cloud session didn't survive — rebuild it
        Serial.println("[firebase] Re-initializing after reconnect");
        initFirebase();
      }
    }
    return;
  }

  unsigned long now = millis();
  if (offlineSinceMs == 0) {                   // transition: link just dropped
    offlineSinceMs = now;
    firebaseReady  = false;                    // force a Firebase rebuild when we return
    Serial.println("[wifi] Offline");
  }

  if (now - lastWifiAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiAttemptMs = now;
    Serial.println("[wifi] Reconnecting...");
    WiFi.reconnect();
  }

  if (WIFI_OFFLINE_REBOOT_MS > 0 && now - offlineSinceMs >= WIFI_OFFLINE_REBOOT_MS) {
    Serial.println("[wifi] Offline too long — restarting");
    delay(100);
    ESP.restart();
  }
}

// -----------------------------------------------------------------------------
// Periodic health heartbeat. A fading signal or a slow heap leak will show up
// here before a drop; if these lines simply STOP, the loop has frozen.
void logDiagnostics() {
  static unsigned long lastDiagMs = 0;
  unsigned long now = millis();
  if (now - lastDiagMs < 5000) return;
  lastDiagMs = now;
  Serial.printf("[diag] up=%lus wifi=%s rssi=%d heap=%u min=%u\n",
                now / 1000UL,
                WiFi.status() == WL_CONNECTED ? "connected" : "DOWN",
                (int)WiFi.RSSI(),
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap());
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
  logDiagnostics();
  ensureConnectivity();
  syncFirebase();

  if (pumpRunning) {
    unsigned long onFor = millis() - pumpStartedMs;
    if (onFor >= MAX_ON_MS) {
      stopPump("SAFETY-CAP");
    }
  }
}
