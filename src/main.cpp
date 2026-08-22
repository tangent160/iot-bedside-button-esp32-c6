#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "kasa.h"

// NanoC6 hardware: BOOT button on GPIO9, active low with an internal pullup.
// (RGB LED on GPIO20 + power enable GPIO19, and the blue LED on GPIO7, are
// deliberately unused — status is serial-log only, as on the Atom Lite.)
static constexpr int BUTTON_PIN = 9;
static constexpr uint32_t DEBOUNCE_MS = 25;
static constexpr uint32_t LONG_PRESS_MS = 5000;
static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 30000;
// Shorter window for automatic background retries, so an unpaired button is
// only unresponsive for a few seconds at a time.
static constexpr uint32_t RETRY_DISCOVERY_MS = 5000;
static constexpr uint32_t RETRY_INTERVAL_MS = 60000;
static constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
static constexpr uint32_t REBOOT_INTERVAL_MS = 24UL * 60 * 60 * 1000;

static Preferences prefs;
static IPAddress kasaIp;
static bool paired = false;
static int relayState = -1;

static bool longPressHandled = false;
static uint32_t lastPairAttemptMs = 0;

// Debounced view of the button, replacing M5Unified's M5.BtnA. Only the two
// queries main loop needs: how long it has been held, and release edges.
static bool btnDown = false;         // debounced state
static bool btnReleased = false;     // release edge, consumed by loop()
static uint32_t btnChangedMs = 0;    // when the debounced state last changed
static bool btnRawDown = false;      // last raw reading
static uint32_t btnRawChangedMs = 0; // when the raw reading last changed

static void buttonUpdate() {
  bool raw = digitalRead(BUTTON_PIN) == LOW;  // active low
  uint32_t now = millis();

  if (raw != btnRawDown) {
    btnRawDown = raw;
    btnRawChangedMs = now;
  }

  btnReleased = false;
  if (raw != btnDown && now - btnRawChangedMs >= DEBOUNCE_MS) {
    btnDown = raw;
    btnChangedMs = now;
    if (!btnDown) btnReleased = true;
  }
}

// True once the button has been held down for at least ms.
static bool buttonPressedFor(uint32_t ms) {
  return btnDown && millis() - btnChangedMs >= ms;
}

static bool buttonWasReleased() { return btnReleased; }

// Blocks until connected; retries after each timeout.
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  while (true) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(250);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) break;
    Serial.println("\nWiFi connect failed, retrying");
    WiFi.disconnect();
    delay(1000);
  }
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}

static void savePairing(const KasaDevice &dev) {
  prefs.putString("ip", dev.ip.toString());
  prefs.putString("deviceId", dev.deviceId);
  Serial.printf("Saved Kasa device %s (%s) to NVS\n",
                dev.ip.toString().c_str(), dev.deviceId.c_str());
}

// Discover an HS200 on the LAN, save it, and update state.
static bool pairDevice(uint32_t timeoutMs = DISCOVERY_TIMEOUT_MS) {
  lastPairAttemptMs = millis();
  KasaDevice dev;
  if (!kasaDiscover(dev, timeoutMs)) {
    paired = false;
    return false;
  }
  kasaIp = dev.ip;
  relayState = dev.relayState;
  paired = true;
  savePairing(dev);
  return true;
}

// Try the saved device. Requires the deviceId to match, so a DHCP reassignment
// can't leave us toggling whatever else has taken over that address.
static bool trySavedDevice() {
  String savedIp = prefs.getString("ip", "");
  String savedId = prefs.getString("deviceId", "");
  if (savedIp.isEmpty() || !kasaIp.fromString(savedIp)) return false;

  Serial.printf("Trying saved Kasa device at %s\n", savedIp.c_str());
  String seenId;
  int state = kasaGetStatus(kasaIp, &seenId);
  if (state < 0) {
    Serial.println("Saved device unreachable");
    return false;
  }
  if (!savedId.isEmpty() && seenId != savedId) {
    Serial.printf("Device at %s is %s, expected %s\n", savedIp.c_str(),
                  seenId.c_str(), savedId.c_str());
    return false;
  }
  relayState = state;
  paired = true;
  Serial.printf("Saved device OK, relay=%d\n", state);
  return true;
}

// Boot: use the saved device if it checks out; otherwise discover.
static void initKasa() {
  if (trySavedDevice()) return;
  Serial.println("Rediscovering");
  pairDevice();
}

// Toggle, retrying once for a dropped packet, then rediscovering in case the
// switch moved to a new address.
static void handleToggle() {
  if (!paired && !pairDevice(RETRY_DISCOVERY_MS)) {
    Serial.println("Not paired; hold button 5s to pair");
    return;
  }

  int result = kasaToggle(kasaIp);
  if (result < 0) {
    Serial.println("Toggle failed, retrying");
    result = kasaToggle(kasaIp);
  }
  if (result < 0) {
    Serial.println("Toggle failed, rediscovering");
    if (pairDevice(RETRY_DISCOVERY_MS)) result = kasaToggle(kasaIp);
  }

  if (result < 0) {
    Serial.println("Toggle failed");
    relayState = -1;
    paired = false;  // let the background retry keep trying
  } else {
    Serial.printf("Toggled, relay=%d\n", result);
    relayState = result;
  }
}

static void handlePairingReset() {
  Serial.println("Long press: clearing saved Kasa device");
  prefs.clear();
  paired = false;
  relayState = -1;
  pairDevice();
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);

  prefs.begin("kasa", false);
  connectWifi();
  initKasa();
}

void loop() {
  buttonUpdate();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    connectWifi();
  }

  if (buttonPressedFor(LONG_PRESS_MS) && !longPressHandled) {
    longPressHandled = true;
    handlePairingReset();
  }

  if (buttonWasReleased()) {
    if (longPressHandled) {
      longPressHandled = false;  // don't also toggle after a long hold
    } else {
      handleToggle();
    }
  }

  // Recover on our own from a switch that was offline at boot (e.g. a power cut
  // that rebooted both devices). WiFi is connected by this point: the check
  // above blocks until it is, so discovery never runs without a network.
  if (!paired && millis() - lastPairAttemptMs > RETRY_INTERVAL_MS) {
    Serial.println("Unpaired, retrying discovery");
    pairDevice(RETRY_DISCOVERY_MS);
  }

  // Daily reboot for long-term robustness; button handlers above have
  // completed, so no Kasa command is in flight here.
  if (millis() > REBOOT_INTERVAL_MS) {
    Serial.println("24h uptime reached, rebooting");
    ESP.restart();
  }

  delay(10);
}
