# iot-bedside-button-esp32-c6

A bedside button that toggles a TP-Link Kasa HS200 light switch over the local network. Built on an M5Stack NanoC6 (ESP32-C6) with PlatformIO + Arduino.

This is a port of [iot-bedside-button](https://github.com/tangent160/iot-bedside-button), which targets the M5Stack Atom Lite (ESP32-PICO-D4). It behaves the same in use — same two gestures, same serial-only status — with different board support, button handling, and rather more work on network recovery.

## Hardware

- [M5Stack NanoC6](https://docs.m5stack.com/en/core/M5NanoC6) — ESP32-C6FH4, 4MB flash, button on GPIO9
- TP-Link Kasa HS200 smart switch (hardware v2.0, legacy TP-Link Smart Home protocol on port 9999)

## Setup

1. Install [PlatformIO](https://platformio.org/) (`pipx install platformio`).
2. Copy `.env.example` to `.env` and set your WiFi credentials:
   ```
   WIFI_SSID=your-ssid
   WIFI_PASSWORD=your-password
   ```
   Credentials are compiled into the firmware at build time; `.env` is gitignored.
3. Build and flash:
   ```
   pio run -t upload && pio device monitor
   ```

   The first build downloads the ESP32-C6 toolchain (a few hundred MB) — the official `espressif32` platform does not support Arduino on the C6, so `platformio.ini` pins the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork instead.

   The NanoC6 enumerates as a USB CDC device (`/dev/ttyACM*`). If you have other USB serial devices attached, name the port explicitly rather than letting autodetect choose:
   ```
   pio run -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00
   ```
   If upload can't find the device at all, hold the button (GPIO9 is the BOOT strapping pin) while plugging it in to force download mode.

## How it works

- On first boot the device connects to WiFi, broadcasts a discovery request, and pairs with the first HS200 it finds. The pairing (IP + device ID) is saved to flash so it survives power cycles.
- On later boots it contacts the saved switch directly, and every button press re-checks that the device ID still matches — if the address was reassigned to a different device, or the switch is unreachable, it rediscovers rather than toggling the wrong light.
- Recovery is automatic. While unpaired it retries discovery every 10 seconds for the first couple of minutes and every 60 seconds after that, and a failed toggle rediscovers. A power cut that reboots both devices sorts itself out in a few seconds; if WiFi drops and stays down for 10 minutes the device reboots itself.
- A press that can't reach the switch gives up after several seconds rather than hanging, and leaves the retrying to the background — so the button stays responsive on a flaky network instead of locking up mid-press.
- The device stays fully awake and connected at all times, so a button press sends the toggle immediately. It reboots itself every 24 hours.

## Usage

- **Short press** — toggle the light.
- **Hold 5 seconds** — forget the saved switch and pair again. This takes effect as soon as you pass 5 seconds, so you can let go any time after.

Status and errors are reported on the serial console (`pio device monitor`, 115200 baud).

## Licensing

This project is released into the public domain (see `LICENSE`). Third-party components — including the LGPL-licensed Arduino ESP32 core linked into the firmware — are listed in [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
