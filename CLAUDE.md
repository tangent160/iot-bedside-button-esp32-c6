# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for an M5Stack NanoC6 (ESP32-C6) bedside button that toggles a TP-Link Kasa HS200 (hw v2.0) light switch over the LAN. PlatformIO + Arduino framework.

This is a port of the Atom Lite (ESP32-PICO-D4) original. Behaviour is intentionally identical — when changing logic, keep the two in sync rather than diverging.

`research/` holds read-only reference clones (the upstream project, the pioarduino platform). It is gitignored, so it is absent in a fresh checkout; re-clone as needed. Never edit anything under it.

Third-party licenses are tracked in `ATTRIBUTIONS.md` — update it when adding or removing a dependency.

## Commands

- `pio run` — build (requires `.env` in repo root; fails with a clear message otherwise)
- `pio run -t upload` — flash the device (shows up as `/dev/ttyACM0`, USB CDC)
- `pio device monitor` — serial monitor at 115200 baud

## Configuration

WiFi credentials come from `.env` (gitignored; see `.env.example`). `load_env.py` is a PlatformIO pre-script that injects `WIFI_SSID` / `WIFI_PASSWORD` as compile-time defines — there is no runtime config.

## Architecture

- `src/kasa.{h,cpp}` — Kasa legacy protocol client, carried over from the original unchanged: XOR "autokey" cipher (key 171), UDP broadcast discovery on port 9999 (no length prefix), TCP commands on port 9999 (4-byte big-endian length prefix). Discovery filters to models starting with `HS200`. Note: newer Kasa hardware uses the KLAP protocol, which this does not speak.
- `src/main.cpp` — boot flow (WiFi → saved device from NVS via `Preferences` namespace `kasa`, else discover for 30s and save), button handling (short press = toggle; 5s hold = clear NVS + rediscover, latched so the release doesn't also toggle), 24h `ESP.restart()`.
- Pairing recovery: `trySavedDevice()` requires the saved `deviceId` to match what the address answers with, so a DHCP reassignment can't leave the button toggling a different device. `handleToggle()` escalates retry → rediscover → retry, then clears `paired`; `loop()` retries discovery every 60s while unpaired using a short 5s window (the 30s window is only for boot and manual pairing). Discovery sits after the blocking WiFi-reconnect check so it never runs without a network. Deliberately no sleep/low-power mode: modem sleep is disabled (`WiFi.setSleep(false)`). Status is serial-log only; also deliberately no LED code (removed from the original after reliability problems, and not reintroduced here).

## Platform notes (ESP32-C6 specific)

- The official `platform = espressif32` has no Arduino support for the C6. `platformio.ini` pins the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork by release URL. Bump that URL deliberately, not casually — it also pins the Arduino core and ESP-IDF versions. The fork is cloned to `research/platform-espressif32/` for reference.
- pioarduino ships no NanoC6 board definition, so `boards/m5stack-nanoc6.json` is a project-local board (PlatformIO picks up a `boards/` dir automatically). It is `esp32-c6-devkitc-1` with the flash size corrected to 4MB and the two USB defines added.
- Serial is the hardware USB-Serial/JTAG peripheral, not a USB-UART bridge: the port is `/dev/ttyACM*` and disappears/reappears across resets. It needs **both** `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`. With only the first, the core aliases `Serial` to `USBSerial` — the native USB-OTG device, which the C6 does not have — and every `Serial.*` call fails to compile. Setting both makes `Serial` the HW CDC device.
- Pick the upload port explicitly (`--upload-port`, ideally a `/dev/serial/by-id/` path). Autodetect scans all ACM devices and can pick some other USB serial gadget.
- `pio device monitor` needs an interactive terminal; it throws in a non-TTY shell. To capture output non-interactively, read the port with pyserial (pulse DTR/RTS first to reset, or you only see steady state — the firmware logs on events, so silence is normal once it is running).
- Verified on hardware: builds, flashes, connects to WiFi, and reaches the HS200. The **button has not been tested** — GPIO9 polarity is the first suspect if press handling misbehaves.

## Hardware facts

- Board: M5Stack NanoC6 (ESP32-C6FH4, single-core RISC-V @160MHz, 4MB flash)
- Button: GPIO9, **active low** with `INPUT_PULLUP` — note the inversion versus the Atom Lite's GPIO39. Also the BOOT strapping pin: held at reset it enters download mode.
- M5Unified is **not** used. The original relied on it only for `M5.BtnA` debouncing; on the C6 it is a thin, less-tested layer, so `main.cpp` reads GPIO9 directly with a small debounce (`buttonUpdate()` / `buttonPressedFor()` / `buttonWasReleased()` mirror the `M5.BtnA` calls they replaced).
- RGB LED (data GPIO20, power enable GPIO19) and the blue LED (GPIO7) exist but are intentionally unused.
