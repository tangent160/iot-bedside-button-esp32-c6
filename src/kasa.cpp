#include "kasa.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static constexpr uint16_t KASA_PORT = 9999;
// A switch on the same LAN answers in milliseconds; if it hasn't replied in
// 1.5s it isn't going to. Kept tight because these timeouts stack — several
// commands can chain inside one button press, all of it blocking the main loop.
static constexpr uint32_t TCP_TIMEOUT_MS = 1500;
static const char SYSINFO_CMD[] = "{\"system\":{\"get_sysinfo\":{}}}";

// XOR autokey cipher, initial key 171. In-place; same buffer layout for
// encrypt and decrypt but the key chains differently.
static void kasaEncrypt(uint8_t *data, size_t len) {
  uint8_t key = 171;
  for (size_t i = 0; i < len; i++) {
    data[i] = key ^ data[i];
    key = data[i];
  }
}

static void kasaDecrypt(uint8_t *data, size_t len) {
  uint8_t key = 171;
  for (size_t i = 0; i < len; i++) {
    uint8_t c = data[i];
    data[i] = key ^ c;
    key = c;
  }
}

// Send a JSON command over TCP (4-byte big-endian length prefix) and return
// the decrypted JSON reply, or empty string on failure.
static String kasaCommand(const IPAddress &ip, const char *json) {
  WiFiClient client;
  if (!client.connect(ip, KASA_PORT, TCP_TIMEOUT_MS)) {
    Serial.printf("[kasa] TCP connect to %s failed\n", ip.toString().c_str());
    return String();
  }

  size_t len = strlen(json);
  uint8_t header[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16),
                       (uint8_t)(len >> 8), (uint8_t)len};
  std::unique_ptr<uint8_t[]> payload(new uint8_t[len]);
  memcpy(payload.get(), json, len);
  kasaEncrypt(payload.get(), len);
  // Short writes would send a truncated ciphertext the switch never answers,
  // costing a full read timeout for no reason. Bail out immediately instead.
  if (client.write(header, 4) != 4 || client.write(payload.get(), len) != len) {
    Serial.println("[kasa] short write sending command");
    return String();
  }

  uint32_t start = millis();
  while (client.available() < 4) {
    if (millis() - start > TCP_TIMEOUT_MS || !client.connected()) {
      Serial.println("[kasa] timeout waiting for reply header");
      return String();
    }
    delay(10);
  }
  uint8_t rh[4];
  client.readBytes(rh, 4);
  uint32_t rlen = ((uint32_t)rh[0] << 24) | ((uint32_t)rh[1] << 16) |
                  ((uint32_t)rh[2] << 8) | rh[3];
  if (rlen == 0 || rlen > 8192) {
    Serial.printf("[kasa] bad reply length %u\n", rlen);
    return String();
  }

  std::unique_ptr<uint8_t[]> reply(new uint8_t[rlen]);
  size_t got = 0;
  while (got < rlen) {
    if (millis() - start > TCP_TIMEOUT_MS * 2) {
      Serial.println("[kasa] timeout reading reply body");
      return String();
    }
    size_t n = client.readBytes(reply.get() + got, rlen - got);
    if (n == 0) {
      // No progress: either the peer closed mid-body, or readBytes hit its own
      // stream timeout. Retrying a closed socket just burns the timeout above.
      if (!client.connected() && !client.available()) {
        Serial.println("[kasa] connection closed mid-reply");
        return String();
      }
      delay(10);
    }
    got += n;
  }
  kasaDecrypt(reply.get(), rlen);
  String result;
  result.reserve(rlen);
  for (size_t i = 0; i < rlen; i++) result += (char)reply[i];
  return result;
}

static int parseRelayState(const String &json, String *deviceId) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return -1;
  JsonVariant info = doc["system"]["get_sysinfo"];
  if (deviceId) *deviceId = (const char *)(info["deviceId"] | "");
  JsonVariant rs = info["relay_state"];
  return rs.isNull() ? -1 : rs.as<int>();
}

bool kasaDiscover(KasaDevice &out, uint32_t timeoutMs) {
  WiFiUDP udp;
  udp.begin(KASA_PORT);

  size_t cmdLen = strlen(SYSINFO_CMD);
  uint8_t packet[64];
  uint32_t start = millis();
  uint32_t lastBroadcast = 0;

  Serial.printf("[kasa] discovering (up to %us)...\n", timeoutMs / 1000);
  while (millis() - start < timeoutMs) {
    if (lastBroadcast == 0 || millis() - lastBroadcast > 3000) {
      memcpy(packet, SYSINFO_CMD, cmdLen);
      kasaEncrypt(packet, cmdLen);
      udp.beginPacket(IPAddress(255, 255, 255, 255), KASA_PORT);
      udp.write(packet, cmdLen);
      udp.endPacket();
      lastBroadcast = millis();
    }

    int size = udp.parsePacket();
    if (size > 0) {
      std::unique_ptr<uint8_t[]> buf(new uint8_t[size + 1]);
      int n = udp.read(buf.get(), size);
      if (n <= 0) continue;
      kasaDecrypt(buf.get(), n);
      buf[n] = 0;

      JsonDocument doc;
      if (deserializeJson(doc, (const char *)buf.get())) continue;
      JsonVariant info = doc["system"]["get_sysinfo"];
      if (info.isNull()) continue;

      const char *model = info["model"] | "";
      if (strncmp(model, "HS200", 5) != 0) {
        Serial.printf("[kasa] ignoring non-HS200 device: %s (%s)\n", model,
                      udp.remoteIP().toString().c_str());
        continue;
      }

      out.ip = udp.remoteIP();
      out.deviceId = (const char *)(info["deviceId"] | "");
      out.model = model;
      out.hwVer = (const char *)(info["hw_ver"] | "");
      out.relayState = info["relay_state"] | -1;
      udp.stop();
      Serial.printf("[kasa] found %s hw %s at %s (relay=%d)\n",
                    out.model.c_str(), out.hwVer.c_str(),
                    out.ip.toString().c_str(), out.relayState);
      return true;
    }
    delay(20);
  }
  udp.stop();
  Serial.println("[kasa] discovery timed out");
  return false;
}

int kasaGetStatus(const IPAddress &ip, String *deviceId) {
  String reply = kasaCommand(ip, SYSINFO_CMD);
  if (reply.isEmpty()) return -1;
  return parseRelayState(reply, deviceId);
}

bool kasaSetRelay(const IPAddress &ip, int state) {
  char cmd[64];
  snprintf(cmd, sizeof(cmd),
           "{\"system\":{\"set_relay_state\":{\"state\":%d}}}", state ? 1 : 0);
  String reply = kasaCommand(ip, cmd);
  if (reply.isEmpty()) return false;
  JsonDocument doc;
  if (deserializeJson(doc, reply)) return false;
  int errCode = doc["system"]["set_relay_state"]["err_code"] | -1;
  return errCode == 0;
}

// Signed comparison so the check survives a millis() wrap.
static bool pastDeadline(uint32_t deadlineMs) {
  return deadlineMs != 0 && (int32_t)(millis() - deadlineMs) >= 0;
}

int kasaToggle(const IPAddress &ip, const String &expectedId,
               uint32_t deadlineMs) {
  String seenId;
  int current = kasaGetStatus(ip, &seenId);
  if (current < 0 && !pastDeadline(deadlineMs)) {
    // Reads are idempotent, so retrying one is always safe — and a dropped
    // packet here is the common transient failure. Worth one cheap retry
    // before the caller escalates to a blocking rediscovery.
    Serial.println("[kasa] status read failed, retrying");
    current = kasaGetStatus(ip, &seenId);
  }
  if (current < 0) return -1;
  // The status read above doubles as the identity check: refuse to flip a relay
  // that isn't the switch we paired with. An empty seenId means the reply
  // carried no ID at all — unknown, not proof of a different device, so treat
  // it the same way the saved-pairing path does and let it through.
  if (!expectedId.isEmpty() && !seenId.isEmpty() && seenId != expectedId) {
    Serial.printf("[kasa] %s is %s, expected %s; not toggling\n",
                  ip.toString().c_str(), seenId.c_str(), expectedId.c_str());
    return KASA_WRONG_DEVICE;
  }

  // Retry the *absolute* target state rather than re-toggling: repeating
  // set_relay_state is harmless, whereas a second toggle would undo a write
  // that landed but whose reply was lost, turning a press into a no-op.
  int next = current ? 0 : 1;
  if (kasaSetRelay(ip, next)) return next;
  if (pastDeadline(deadlineMs)) return -1;
  if (kasaSetRelay(ip, next)) return next;
  if (pastDeadline(deadlineMs)) return -1;

  // Both writes went unacknowledged. One may still have taken effect, so ask
  // the switch what it actually did before reporting failure.
  if (kasaGetStatus(ip) == next) {
    Serial.println("[kasa] set unacknowledged but relay is in the target state");
    return next;
  }
  return -1;
}
