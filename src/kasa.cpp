#include "kasa.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static constexpr uint16_t KASA_PORT = 9999;
static constexpr uint32_t TCP_TIMEOUT_MS = 3000;
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
  client.write(header, 4);
  client.write(payload.get(), len);

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
    got += client.readBytes(reply.get() + got, rlen - got);
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

int kasaToggle(const IPAddress &ip) {
  int current = kasaGetStatus(ip);
  if (current < 0) return -1;
  int next = current ? 0 : 1;
  return kasaSetRelay(ip, next) ? next : -1;
}
