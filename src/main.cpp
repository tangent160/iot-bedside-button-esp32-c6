#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

#include "kasa.h"

// NanoC6 hardware: BOOT button on GPIO9, active low with an internal pullup.
// (RGB LED on GPIO20 + power enable GPIO19, and the blue LED on GPIO7, are
// deliberately unused — status is serial-log only, as on the Atom Lite.)
static constexpr int BUTTON_PIN = 9;
static constexpr uint32_t DEBOUNCE_MS = 25;
static constexpr uint32_t LONG_PRESS_MS = 5000;  // forget, then rediscover
// Every discovery window, boot included: short enough that the button is never
// unresponsive for more than a few seconds. The background retry below does the
// persistent work instead of one long blocking scan.
static constexpr uint32_t DISCOVERY_MS = 5000;
// How long a single press may spend before it gives up and leaves the rest to
// the background retry. Checked between every network step, inside kasaToggle
// as well as here, so the real bound is this plus one command's timeout.
static constexpr uint32_t TOGGLE_BUDGET_MS = 8000;
// Retry fast at first, then back off. A shared power cut leaves the switch still
// booting while we are already up, and one 5s window only gets two broadcasts —
// so a flat 60s interval would leave the button dead for a minute in exactly the
// case this loop exists for.
static constexpr uint32_t RETRY_FAST_INTERVAL_MS = 10000;
static constexpr uint32_t RETRY_FAST_FOR_MS = 120000;
static constexpr uint32_t RETRY_INTERVAL_MS = 60000;
static constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
// If WiFi stays down this long, reboot. connectWifi() blocks, so without this
// the 24h reboot below is unreachable in exactly the case it's meant for.
static constexpr uint32_t WIFI_REBOOT_AFTER_MS = 10UL * 60 * 1000;
static constexpr uint32_t SERIAL_WAIT_MS = 2000;
static constexpr uint32_t REBOOT_INTERVAL_MS = 24UL * 60 * 60 * 1000;

static Preferences prefs;
static IPAddress kasaIp;
static String kasaDeviceId;  // checked on every toggle, not just at boot
static bool paired = false;

static bool longPressHandled = false;
static uint32_t lastPairAttemptMs = 0;
static uint32_t unpairedSinceMs = 0;  // 0 while paired; drives the retry backoff

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
    // Date the press from the raw edge, not from when debouncing confirmed it,
    // so hold durations aren't reported DEBOUNCE_MS short.
    btnChangedMs = btnRawChangedMs;
    if (!btnDown) btnReleased = true;
  }
}

// True once the button has been held down for at least ms.
static bool buttonPressedFor(uint32_t ms) {
  return btnDown && millis() - btnChangedMs >= ms;
}

static bool buttonWasReleased() { return btnReleased; }

// Blocks until connected, retrying after each timeout; reboots rather than
// blocking forever, so a wedged radio can't strand us outside the 24h reboot.
static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  uint32_t firstAttempt = millis();
  // Deliberately not persisted across restarts: ESP.restart() resets this to
  // false, so an outage costs at most one reboot. After it the radio is freshly
  // initialised — there is nothing left for a second reboot to fix, and waiting
  // it out beats a reboot loop until the AP comes back.
  static bool wifiEverConnected = false;
  while (true) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(250);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) break;
    // Only reboot out of a connection that used to work: that's a wedged radio,
    // which a restart fixes. Never having connected is more likely a wrong SSID
    // or password, where rebooting fixes nothing and wipes the serial log you'd
    // be reading to diagnose it.
    if (wifiEverConnected && millis() - firstAttempt > WIFI_REBOOT_AFTER_MS) {
      Serial.println("\nWiFi down too long, rebooting");
      ESP.restart();
    }
    Serial.println("\nWiFi connect failed, retrying");
    WiFi.disconnect();
    delay(1000);
  }
  wifiEverConnected = true;
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());
}

static void savePairing(const KasaDevice &dev) {
  prefs.putString("ip", dev.ip.toString());
  prefs.putString("deviceId", dev.deviceId);
  Serial.printf("Saved Kasa device %s (%s) to NVS\n",
                dev.ip.toString().c_str(), dev.deviceId.c_str());
}

// Discover an HS200 on the LAN, save it, and update state.
static bool pairDevice(uint32_t timeoutMs = DISCOVERY_MS) {
  lastPairAttemptMs = millis();
  KasaDevice dev;
  if (!kasaDiscover(dev, timeoutMs)) {
    paired = false;
    return false;
  }
  kasaIp = dev.ip;
  kasaDeviceId = dev.deviceId;
  paired = true;
  savePairing(dev);
  return true;
}

// Try the saved device. Requires the deviceId to match, so a DHCP reassignment
// can't leave us toggling whatever else has taken over that address.
static bool trySavedDevice() {
  String savedIp = prefs.getString("ip", "");
  String savedId = prefs.getString("deviceId", "");
  IPAddress candidate;
  // Work on a candidate and only publish kasaIp once it checks out, so a failed
  // attempt can't leave a stale address in the global.
  if (savedIp.isEmpty() || !candidate.fromString(savedIp)) return false;

  Serial.printf("Trying saved Kasa device at %s\n", savedIp.c_str());
  String seenId;
  int state = kasaGetStatus(candidate, &seenId);
  if (state < 0) {
    // Same cheap retry kasaToggle does: one dropped packet shouldn't cost a
    // full rediscovery that only re-finds the device we already had saved.
    state = kasaGetStatus(candidate, &seenId);
  }
  if (state < 0) {
    Serial.println("Saved device unreachable");
    return false;
  }
  if (!savedId.isEmpty() && seenId != savedId) {
    Serial.printf("Device at %s is %s, expected %s\n", savedIp.c_str(),
                  seenId.c_str(), savedId.c_str());
    return false;
  }
  // A pairing saved without an ID can still be guarded from here on: adopt what
  // the switch reports and persist it.
  if (savedId.isEmpty() && !seenId.isEmpty()) {
    savedId = seenId;
    prefs.putString("deviceId", savedId);
  }
  kasaIp = candidate;
  kasaDeviceId = savedId;
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

// Toggle, falling back to rediscovery in case the switch moved to a new address.
static void handleToggle() {
  // Every step here blocks the main loop — the button isn't sampled and WiFi
  // isn't checked until we return — so one deadline covers the whole press and
  // is honoured inside kasaToggle too, not just between the stages here. Once
  // it passes we give up and let the background retry carry on.
  uint32_t deadline = millis() + TOGGLE_BUDGET_MS;
  if (!paired && !pairDevice()) {
    Serial.println("Not paired; hold button 5s to clear and pair again");
    return;
  }

  // No blind retry here: kasaToggle already retries and verifies internally,
  // and calling it again would re-read the state and toggle back if the first
  // attempt actually landed. Escalate straight to rediscovery instead — for a
  // wrong-device answer that's the only thing that can help anyway.
  int result = kasaToggle(kasaIp, kasaDeviceId, deadline);
  if (result < 0 && (int32_t)(millis() - deadline) < 0) {
    Serial.println("Toggle failed, rediscovering");
    if (pairDevice()) result = kasaToggle(kasaIp, kasaDeviceId, deadline);
  }

  if (result < 0) {
    Serial.println("Toggle failed");
    paired = false;  // let the background retry keep trying
  } else {
    Serial.printf("Toggled, relay=%d\n", result);
  }
}

// Forget the saved device unconditionally, then look for whatever is out there.
// The pairing is dropped even if discovery then finds nothing — that is the
// point, for when the old switch is gone for good; the background retry keeps
// looking afterwards.
static void handleForget() {
  Serial.println("Long press: clearing saved Kasa device");
  prefs.clear();
  kasaIp = IPAddress();
  kasaDeviceId = "";
  paired = false;
  pairDevice();
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  // USB CDC only exists once the host enumerates it; without this pause the
  // whole boot log is gone before a monitor can attach. Bounded, so a headless
  // power-on still proceeds.
  uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < SERIAL_WAIT_MS) delay(10);

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

  // Fires as soon as the hold passes 5s, so you can let go any time after.
  if (buttonPressedFor(LONG_PRESS_MS) && !longPressHandled) {
    longPressHandled = true;
    handleForget();
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
  if (paired) {
    unpairedSinceMs = 0;
  } else {
    if (unpairedSinceMs == 0) unpairedSinceMs = millis();
    uint32_t interval = millis() - unpairedSinceMs < RETRY_FAST_FOR_MS
                            ? RETRY_FAST_INTERVAL_MS
                            : RETRY_INTERVAL_MS;
    if (millis() - lastPairAttemptMs > interval) {
      Serial.println("Unpaired, retrying discovery");
      pairDevice();
    }
  }

  // Daily reboot for long-term robustness; button handlers above have
  // completed, so no Kasa command is in flight here.
  if (millis() > REBOOT_INTERVAL_MS) {
    Serial.println("24h uptime reached, rebooting");
    ESP.restart();
  }

  delay(10);
}
