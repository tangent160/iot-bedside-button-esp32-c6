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

// Read current state and set the opposite. Returns new state (0/1) or -1 on error.
int kasaToggle(const IPAddress &ip);
