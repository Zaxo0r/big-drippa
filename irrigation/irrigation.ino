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

#include <esp_task_wdt.h>

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
static unsigned long     lastFirebaseOkMs   = 0;  // last successful Firebase op (0 = none yet)
static int               wifiRetryCount     = 0;  // consecutive reconnect attempts while offline

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
  ssl_client.setTimeout(FIREBASE_SOCKET_TIMEOUT_S);  // bound SSL reads so a dead socket can't hang the loop

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
    lastFirebaseOkMs = millis();
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
  lastFirebaseOkMs = millis();  // confirmed round-trip — Firebase is reachable

  if (manualRun) {
    Serial.println("[firebase] Manual run command received");
    // Clear the command FIRST and confirm it stuck. On a poor link this write
    // can fail or hang; if we started the pump anyway, a watchdog reboot would
    // re-read the still-set command and water again — repeatedly, risking an
    // overflow / dry reservoir. Skipping is the safe failure (under-water,
    // never flood); the command stays queued and we retry next poll.
    Database.set<bool>(aClient, "/commands/manualRun", false);
    if (aClient.lastError().code() != 0) {
      Serial.printf("[firebase] Could not clear manualRun (code=%d) — skipping start to avoid re-trigger\n",
                    aClient.lastError().code());
      return;
    }

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
// Full radio power-cycle — more thorough than reconnect(). WIFI_OFF tears down
// the WiFi driver, which can clear a wedged PHY/MAC state that a plain reconnect
// leaves stuck (the kind that otherwise needs a manual power cycle).
void resetWifiRadio() {
  WiFi.disconnect(true, true);   // disconnect, radio off, forget stored AP
  WiFi.mode(WIFI_OFF);
  delay(300);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
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

    // Silent link death: WiFi still reports "connected" but nothing is getting
    // through (a half-open link the driver hasn't noticed). WiFi.status() can't
    // be trusted, so fall back to Firebase liveness and force a hard radio reset.
    if (app.isInitialized() && lastFirebaseOkMs != 0 &&
        millis() - lastFirebaseOkMs > FIREBASE_STALL_MS) {
      Serial.printf("[recover] No Firebase success in %lus — forcing WiFi reset\n",
                    (millis() - lastFirebaseOkMs) / 1000UL);
      lastFirebaseOkMs = millis();             // don't re-trigger every iteration
      firebaseReady = false;
      resetWifiRadio();                        // offline path takes over next loop
    }
    return;
  }

  unsigned long now = millis();
  if (offlineSinceMs == 0) {                   // transition: link just dropped
    offlineSinceMs = now;
    firebaseReady  = false;                    // force a Firebase rebuild when we return
    wifiRetryCount = 0;
    Serial.println("[wifi] Offline");
  }

  if (now - lastWifiAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiAttemptMs = now;
    wifiRetryCount++;
    // Plain reconnect() usually suffices; if it keeps failing the radio may be
    // wedged, so escalate to a full WIFI_OFF power-cycle every ~6th attempt.
    if (wifiRetryCount % 6 == 0) {
      Serial.println("[wifi] Reconnect failing — deep radio reset");
      resetWifiRadio();
    } else {
      Serial.println("[wifi] Reconnecting...");
      WiFi.reconnect();
    }
  }

  if (WIFI_OFFLINE_REBOOT_MS > 0 && now - offlineSinceMs >= WIFI_OFFLINE_REBOOT_MS) {
    Serial.println("[wifi] Offline too long — clean WiFi shutdown + restart");
    WiFi.disconnect(true, true);   // leave the radio clean for the next boot
    WiFi.mode(WIFI_OFF);
    delay(300);
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
  unsigned long fbAge = lastFirebaseOkMs ? (now - lastFirebaseOkMs) / 1000UL : 0;
  Serial.printf("[diag] up=%lus wifi=%s rssi=%d heap=%u min=%u fbAge=%lus\n",
                now / 1000UL,
                WiFi.status() == WL_CONNECTED ? "connected" : "DOWN",
                (int)WiFi.RSSI(),
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(),
                fbAge);
}

// -----------------------------------------------------------------------------
// Hardware task watchdog — the true last resort. If loop() ever stalls longer
// than WDT_TIMEOUT_S (e.g. a network call wedges despite the socket timeout),
// the chip resets itself. Armed AFTER the blocking bring-up so the initial
// connect sequence can't trip it.
void setupWatchdog() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t cfg = {
    .timeout_ms     = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic  = true,
  };
  esp_task_wdt_reconfigure(&cfg);   // core 3.x already inits the TWDT
  esp_task_wdt_add(NULL);
#else
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
#endif
  Serial.printf("[wdt] Task watchdog armed (%ds)\n", WDT_TIMEOUT_S);
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
  setupWatchdog();   // arm the watchdog only after the blocking bring-up above
}

// -----------------------------------------------------------------------------
void loop() {
  esp_task_wdt_reset();   // feed the watchdog; if the loop ever wedges, the chip resets
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
