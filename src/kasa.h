// Kasa (TP-Link Smart Home) legacy protocol client for HS200-class switches.
// JSON commands XOR-obfuscated with autokey 171; UDP 9999 discovery, TCP 9999 commands.
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct KasaDevice {
  IPAddress ip;
  String deviceId;
  String model;
  String hwVer;
  int relayState = -1;
};

// Broadcast get_sysinfo for up to timeoutMs; returns true and fills `out` with
// the first device whose model starts with "HS200".
bool kasaDiscover(KasaDevice &out, uint32_t timeoutMs = 30000);

// Query relay state of a device. Returns 0/1, or -1 on error.
// If deviceId is non-null it receives the device's ID, so callers can confirm
// the address still belongs to the device they paired with.
int kasaGetStatus(const IPAddress &ip, String *deviceId = nullptr);

// Set relay state (0/1). Returns true on success.
bool kasaSetRelay(const IPAddress &ip, int state);

// Returned by kasaToggle when the address answers as a different device.
static constexpr int KASA_WRONG_DEVICE = -2;

// Read current state and set the opposite. Retries the write internally (the
// absolute target state, so a repeat can't undo a write whose reply was lost)
// and verifies before giving up. Returns the new state (0/1), -1 on error, or
// KASA_WRONG_DEVICE if expectedId is non-empty and the device at this address
// reports a different, non-empty ID (e.g. the DHCP lease moved on).
//
// deadlineMs is an absolute millis() timestamp, checked between steps so a
// degraded network can't chain four timeouts back to back; 0 means no limit.
// It can only stop further steps, never abandon one already in flight, so the
// call can still overrun by up to one command's timeout.
int kasaToggle(const IPAddress &ip, const String &expectedId = String(),
               uint32_t deadlineMs = 0);
