# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an M5Stack NanoC6 (ESP32-C6) bedside button that toggles a TP-Link Kasa HS200 (hw v2.0) light switch over the LAN. PlatformIO + Arduino framework.

This is a port of the Atom Lite (ESP32-PICO-D4) original. The *user-facing* behaviour is intentionally identical — same two gestures, same serial-only status — and should stay that way. The reliability internals have since diverged on purpose (per-toggle identity checks, idempotent retries, press deadlines, discovery backoff, WiFi-outage reboot); don't "resync" those to the original without reading why they changed.

`research/` holds read-only reference clones (the upstream project, the pioarduino platform). It is gitignored, so it is absent in a fresh checkout; re-clone as needed. Never edit anything under it.

Third-party licenses are tracked in `ATTRIBUTIONS.md` — update it when adding or removing a dependency.

## Commands

- `pio run` — build (requires `.env` in repo root; fails with a clear message otherwise)
- `pio run -t upload` — flash the device (shows up as `/dev/ttyACM0`, USB CDC)
- `pio device monitor` — serial monitor at 115200 baud

## Configuration

WiFi credentials come from `.env` (gitignored; see `.env.example`). `load_env.py` is a PlatformIO pre-script that injects `WIFI_SSID` / `WIFI_PASSWORD` as compile-time defines — there is no runtime config.

## Architecture

- `src/kasa.{h,cpp}` — Kasa legacy protocol client, essentially the original: XOR "autokey" cipher (key 171), UDP broadcast discovery on port 9999 (no length prefix), TCP commands on port 9999 (4-byte big-endian length prefix). Discovery filters to models starting with `HS200`. Note: newer Kasa hardware uses the KLAP protocol, which this does not speak.
- `src/main.cpp` — boot flow (WiFi → saved device from NVS via `Preferences` namespace `kasa`, else discover and save), button handling, 24h `ESP.restart()`.
- Exactly two gestures, matching the original: short press toggles; a 5s hold forgets the saved device and rediscovers, firing the moment the hold passes 5s (latched, so the release doesn't also toggle). Deliberately no third gesture — a second threshold would force gestures to resolve on release instead, since the 5s action fires before a longer hold could complete.
- `connectWifi()` blocks, so it reboots after 10 minutes of failure — but only once WiFi has connected at least once. That's a wedged radio, which a restart fixes; never having connected points at bad credentials, where rebooting fixes nothing and wipes the serial log you'd diagnose it from. Without the reboot the 24h restart is unreachable in exactly the case it exists for.
- **Identity**: the saved `deviceId` is verified on *every* toggle, not just at boot, so a mid-uptime DHCP reassignment can't leave the button flipping a different device. `kasaToggle()` folds the check into the status read it already does and returns `KASA_WRONG_DEVICE` (-2) on a non-empty mismatch. An *empty* reported ID means "unknown", not "wrong", and is allowed through.
- **Retries live in `kasaToggle()`, not above it.** It retries the status read once (idempotent; a dropped packet is the common transient failure), then retries the *absolute* target state and verifies before failing. `handleToggle()` therefore has no retry of its own — calling `kasaToggle()` again would re-read the state and undo a write whose reply was merely lost. It only escalates to rediscovery, then clears `paired`.
- **Everything blocks the main loop** — the button isn't sampled and WiFi isn't checked while a Kasa command is in flight. Hence a tight 1.5s `TCP_TIMEOUT_MS`, and one `TOGGLE_BUDGET_MS` deadline per press that `handleToggle()` threads into `kasaToggle()` so its internal retries honour it too. Known gap: neither `kasaToggle()`'s *first* status read nor `kasaDiscover()`'s window is deadline-checked, so the real worst case is ~17.5s, not the ~12.5s the budget implies. Guarding those two is the fix if it ever matters.
- **Recovery**: `handleForget()` clears NVS unconditionally and rediscovers. Every discovery window is 5s, boot included; `loop()` then retries every 10s for the first 2 minutes and every 60s after, because one 5s window is only two broadcasts and a shared power cut leaves the switch still booting. Discovery sits after the blocking WiFi-reconnect check so it never runs without a network.
- Deliberately no sleep/low-power mode: modem sleep is disabled (`WiFi.setSleep(false)`). Status is serial-log only; also deliberately no LED code (removed from the original after reliability problems, and not reintroduced here).

## Platform notes (ESP32-C6 specific)

- The official `platform = espressif32` has no Arduino support for the C6. `platformio.ini` pins the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork by release URL. Bump that URL deliberately, not casually — it also pins the Arduino core and ESP-IDF versions. The fork is cloned to `research/platform-espressif32/` for reference.
- pioarduino ships no NanoC6 board definition, so `boards/m5stack-nanoc6.json` is a project-local board (PlatformIO picks up a `boards/` dir automatically). It is `esp32-c6-devkitc-1` with the flash size corrected to 4MB and the two USB defines added.
- Serial is the hardware USB-Serial/JTAG peripheral, not a USB-UART bridge: the port is `/dev/ttyACM*` and disappears/reappears across resets. It needs **both** `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`. With only the first, the core aliases `Serial` to `USBSerial` — the native USB-OTG device, which the C6 does not have — and every `Serial.*` call fails to compile. Setting both makes `Serial` the HW CDC device. `setup()` waits up to 2s for the host to enumerate it, otherwise the boot log is gone before a monitor can attach; the wait is bounded so headless power-on still proceeds.
- Pick the upload port explicitly (`--upload-port`, ideally a `/dev/serial/by-id/` path). Autodetect scans all ACM devices and can pick some other USB serial gadget.
- `pio device monitor` needs an interactive terminal; it throws in a non-TTY shell. To capture output non-interactively, read the port with pyserial (pulse DTR/RTS first to reset, or you only see steady state — the firmware logs on events, so silence is normal once it is running).
- Verified end to end on hardware — builds, flashes, connects to WiFi, reaches the HS200, and both gestures work — but that was **before** the reliability rework above (retries, deadlines, 1.5s timeout, backoff, forget-on-hold). None of that has run on the device yet; re-test both gestures plus one deliberate failure case (toggle with the switch unplugged) after the next flash.

## Hardware facts

- Board: M5Stack NanoC6 (ESP32-C6FH4, single-core RISC-V @160MHz, 4MB flash)
- Button: GPIO9, **active low** with `INPUT_PULLUP` — note the inversion versus the Atom Lite's GPIO39. Also the BOOT strapping pin: held at reset it enters download mode.
- M5Unified is **not** used. The original relied on it only for `M5.BtnA` debouncing; on the C6 it is a thin, less-tested layer, so `main.cpp` reads GPIO9 directly with a small debounce (`buttonUpdate()` / `buttonPressedFor()` / `buttonWasReleased()` mirror the `M5.BtnA` calls they replaced).
- RGB LED (data GPIO20, power enable GPIO19) and the blue LED (GPIO7) exist but are intentionally unused.
