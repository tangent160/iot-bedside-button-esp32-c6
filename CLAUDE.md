# CLAUDE.md

This file gives guidance to Claude Code (claude.ai/code) for work on the code in this repository.

## Project

This is the firmware for an M5Stack NanoC6 (ESP32-C6) bedside button. The button toggles a TP-Link Kasa HS200 (hardware v2.0) light switch over the LAN. The project uses PlatformIO and the Arduino framework.

This firmware is a port of the Atom Lite (ESP32-PICO-D4) original. The user-facing behavior is the same on purpose: the same two gestures, and the same serial-only status. Keep it the same.

The reliability internals are different from the original on purpose. The differences are the identity check at each toggle, the idempotent retries, the press deadlines, the discovery backoff, and the reboot after a WiFi outage. Do not make these parts the same as the original again. First read why they changed.

The directory `research/` holds read-only reference clones. These clones are the upstream project and the pioarduino platform. The directory is gitignored, thus it is not in a new checkout. Clone it again if you need it. Never edit a file under this directory.

## Documentation files

- `LICENSE.md` holds the MIT license of this project. The upstream project is in the public domain under the Unlicense, thus the material from it stays in the public domain.
- `THIRD-PARTY-NOTICES.md` records the third-party licenses. If you add a dependency or remove a dependency, update this file.
- `CHANGELOG.md` uses the Keep a Changelog format. Put every user-visible change under `## [Unreleased]`. Do not make a version heading and do not set a release date. The user starts the version bump and makes the release.
- `README.md` is for a human on the repository page. Keep it free of internal detail.

## Commands

- `pio run` — Build the firmware. The build needs `.env` in the root of the repository. If the file is absent, the build stops with a clear message.
- `pio run -t upload` — Flash the device. The device shows as `/dev/ttyACM0` (USB CDC).
- `pio device monitor` — Open the serial monitor at 115200 baud.

## Configuration

The WiFi credentials come from `.env`. This file is gitignored. `.env.example` shows the format. `load_env.py` is a PlatformIO pre-script. The script puts `WIFI_SSID` and `WIFI_PASSWORD` into the build as compile-time defines. There is no runtime configuration.

## Architecture

- `src/kasa.{h,cpp}` is the client for the legacy Kasa protocol. It is almost the same as the original. It uses the XOR "autokey" cipher (key 171). Discovery is a UDP broadcast on port 9999 with no length prefix. Commands go over TCP on port 9999 with a 4-byte big-endian length prefix. Discovery accepts only models with the prefix `HS200`. Newer Kasa hardware uses the KLAP protocol, which this client does not speak.
- `src/main.cpp` holds the boot flow, the button handling, and the 24h `ESP.restart()`. The boot flow connects to WiFi. Then it reads the saved device from NVS with `Preferences` in the namespace `kasa`. If there is no saved device, it discovers one and saves it.
- There are exactly two gestures, as in the original. A short press toggles the light. A hold of 5s makes the firmware forget the saved device and discover again. This action starts at the moment the hold passes 5s. The action is latched, thus the release does not also toggle. There is no third gesture, on purpose. A second threshold makes all gestures resolve on release, because the 5s action starts before a longer hold can complete.
- `connectWifi()` blocks. Thus it reboots the device after 10 minutes of failure, but only after WiFi connected one time or more. A failure after a good connection is a wedged radio, and a restart repairs it. A failure with no good connection points to bad credentials. A reboot repairs nothing in this condition, and it erases the serial log that you need for the diagnosis. Without this reboot, the 24h restart is out of reach in the exact condition that it exists for.
- **Identity**: the firmware makes a check of the saved `deviceId` at every toggle, not only at boot. Thus a DHCP reassignment in mid-uptime cannot make the button flip a different device. `kasaToggle()` puts this check into the status read that it already does. It returns `KASA_WRONG_DEVICE` (-2) on a mismatch that is not empty. An empty device ID means "unknown", not "wrong", and the code lets it through.
- **The retries are in `kasaToggle()`, not above it.** The function retries the status read one time. The read is idempotent, and a lost packet is the usual transient failure. Then it retries the absolute target state and makes sure that the state is correct before it reports a failure. Thus `handleToggle()` has no retry of its own. A second call to `kasaToggle()` reads the state again and reverses a write whose reply was only lost. `handleToggle()` only escalates to discovery, and then clears `paired`.
- **All operations block the main loop.** The firmware does not sample the button and does not look at WiFi while a Kasa command is in flight. For this reason `TCP_TIMEOUT_MS` is a tight 1.5s. There is also one `TOGGLE_BUDGET_MS` deadline per press. `handleToggle()` passes this deadline into `kasaToggle()`, thus the internal retries obey it too. There is a known gap: the deadline does not cover the first status read in `kasaToggle()`, and it does not cover the window in `kasaDiscover()`. Thus the true worst case is approximately 17.5s, not the approximately 12.5s of the budget. If this becomes a problem, the fix is a deadline check in these two places.
- **Recovery**: `handleForget()` erases NVS always, and then discovers again. Every discovery window is 5s, and the window at boot is the same. Then `loop()` retries every 10s for the first 2 minutes, and every 60s after that time. One 5s window is only two broadcasts, and after a common power cut the switch is still in its boot sequence. Discovery comes after the blocking WiFi-reconnect check, thus it never operates without a network.
- There is no sleep mode and no low-power mode, on purpose. Modem sleep is off (`WiFi.setSleep(false)`). The status goes only to the serial log. There is also no LED code, on purpose. The original had LED code, but it was removed after reliability problems, and it is not here again.

## Platform notes (ESP32-C6 specific)

- The official `platform = espressif32` has no Arduino support for the C6. `platformio.ini` pins the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork by its release URL. This URL also pins the Arduino core version and the ESP-IDF version. Change the URL only with intention. The fork is cloned to `research/platform-espressif32/` for reference.
- pioarduino has no board definition for the NanoC6. Thus `boards/m5stack-nanoc6.json` is a board that is local to the project. PlatformIO finds a `boards/` directory automatically. The definition is `esp32-c6-devkitc-1` with a corrected flash size of 4MB and the two USB defines added.
- The serial port is the hardware USB-Serial/JTAG peripheral, not a USB-UART bridge. Thus the port is `/dev/ttyACM*`, and it goes away and comes back across a reset. It needs **both** `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`. With only the first define, the core makes `Serial` an alias of `USBSerial`. `USBSerial` is the native USB-OTG device, which the C6 does not have, thus every `Serial.*` call fails to compile. With both defines, `Serial` is the hardware CDC device. `setup()` waits up to 2s for the host to enumerate the device. Without this wait, the boot log is gone before a monitor can attach. The wait has a limit, thus a headless power-on continues.
- Name the upload port explicitly with `--upload-port`. Use a `/dev/serial/by-id/` path if you can. Autodetect scans all ACM devices and can pick a different USB serial device.
- `pio device monitor` needs an interactive terminal. In a shell that is not a TTY, it throws. To capture the output non-interactively, read the port with pyserial. Pulse DTR and RTS first to reset the device. Without this pulse you see only the steady state, because the firmware writes the log on events. Silence is normal after the firmware runs.
- The firmware was tested end to end on the hardware. It builds, it flashes, it connects to WiFi, it gets to the HS200, and both gestures work. But this test was **before** the reliability rework in the list above: the retries, the deadlines, the 1.5s timeout, the backoff, and forget-on-hold. None of this new code has run on the device. After the next flash, do a test of both gestures again, and one deliberate failure case: a toggle with the switch unplugged.

## Hardware facts

- Board: M5Stack NanoC6 (ESP32-C6FH4, single-core RISC-V at 160MHz, 4MB flash).
- Button: GPIO9, **active low** with `INPUT_PULLUP`. This is the inverse of GPIO39 on the Atom Lite. GPIO9 is also the BOOT strapping pin. If it is held at reset, the device enters download mode.
- The project does **not** use M5Unified. The original used it only for the debounce of `M5.BtnA`. On the C6 this library is a thin layer with less test coverage. Thus `main.cpp` reads GPIO9 directly with a small debounce. The functions `buttonUpdate()`, `buttonPressedFor()`, and `buttonWasReleased()` do the same work as the `M5.BtnA` calls that they replaced.
- The RGB LED (data on GPIO20, power enable on GPIO19) and the blue LED (GPIO7) are on the board, but the firmware does not use them, on purpose.
