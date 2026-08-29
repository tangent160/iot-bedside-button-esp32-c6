# iot-bedside-button-esp32-c6

This is a bedside button that toggles a TP-Link Kasa HS200 light switch over the local network. It is built on an M5Stack NanoC6 (ESP32-C6) with PlatformIO and Arduino.

This firmware is a port of [iot-bedside-button](https://github.com/tangent160/iot-bedside-button), which is for the M5Stack Atom Lite (ESP32-PICO-D4). In use, the two behave the same: the same two gestures, and the same serial-only status. The board support, the button handling, and the network recovery are different. The network recovery has much more work in it.

## Hardware

- [M5Stack NanoC6](https://docs.m5stack.com/en/core/M5NanoC6) — ESP32-C6FH4, 4MB flash, button on GPIO9.
- TP-Link Kasa HS200 smart switch — hardware v2.0, legacy TP-Link Smart Home protocol on port 9999.

## Setup

1. Install [PlatformIO](https://platformio.org/) with `pipx install platformio`.
2. Copy `.env.example` to `.env`. Then put your WiFi credentials in the new file:
   ```
   WIFI_SSID=your-ssid
   WIFI_PASSWORD=your-password
   ```
   The build compiles the credentials into the firmware. The file `.env` is gitignored.
3. Build the firmware and flash the device:
   ```
   pio run -t upload && pio device monitor
   ```

   The first build downloads the ESP32-C6 toolchain, which is a few hundred MB. The official `espressif32` platform has no Arduino support for the C6. Thus `platformio.ini` pins the [pioarduino](https://github.com/pioarduino/platform-espressif32) fork instead.

   The NanoC6 shows as a USB CDC device (`/dev/ttyACM*`). If other USB serial devices are attached, name the port explicitly. Do not let autodetect pick the port:
   ```
   pio run -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00
   ```
   If the upload cannot find the device at all, hold the button and connect the cable at the same time. This forces download mode, because GPIO9 is the BOOT strapping pin.

## How it works

At the first boot the device connects to WiFi and broadcasts a discovery request. It then pairs with the first HS200 that it finds. The device saves the pairing (the IP address and the device ID) to flash, thus the pairing survives a power cycle.

At a later boot the device contacts the saved switch directly. At every button press it makes sure that the device ID is still the same. If the address now belongs to a different device, or if the switch does not answer, the device discovers again. It does not toggle the wrong light.

Recovery is automatic. While the device is unpaired, it retries discovery every 10 seconds for the first two minutes, and every 60 seconds after that time. A failed toggle also starts discovery again. After a power cut that reboots both devices, the pair repairs itself in a few seconds. If WiFi goes down and stays down for 10 minutes, the device reboots itself.

A press that cannot get to the switch stops after a few seconds. It does not hang. The background retries continue. Thus the button stays responsive on an unstable network, and it does not lock up in mid-press.

The device stays fully awake and connected at all times, thus a button press sends the toggle immediately. The device reboots itself every 24 hours.

## Usage

- **Short press** — Toggle the light.
- **Hold for 5 seconds** — Forget the saved switch and pair again. The action starts as soon as the hold passes 5 seconds, thus you can release the button at any time after that.

The device reports its status and its errors on the serial console (`pio device monitor`, 115200 baud).

## Changes

[CHANGELOG.md](CHANGELOG.md) records the changes of each version.

## Licensing

This project is under the MIT license. [LICENSE.md](LICENSE.md) gives the text.

[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists the third-party components. One of them is the Arduino ESP32 core. That core is under the LGPL, and it is linked into the firmware. The upstream project is in the public domain, and the material from it stays in the public domain.
