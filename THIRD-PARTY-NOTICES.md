# Third-party notices

This project is under the MIT license. [LICENSE.md](LICENSE.md) gives the text.

The material that follows is the work of other people. Some of it is in this
repository. The rest is linked into the firmware at build time.

## In this repository

### iot-bedside-button (the upstream project)

The files `src/kasa.cpp`, `src/kasa.h`, `load_env.py`, and `.env.example` are
copied without change from
[tangent160/iot-bedside-button](https://github.com/tangent160/iot-bedside-button).
The files `src/main.cpp`, `README.md`, and `CLAUDE.md` are derived from that
project.

The upstream project is in the public domain under the
[Unlicense](https://unlicense.org). The Unlicense gives no conditions, thus this
note is a courtesy and not a legal requirement. The public-domain dedication
stays with the upstream material.

### pioarduino platform-espressif32

The file `boards/m5stack-nanoc6.json` is adapted from
`boards/esp32-c6-devkitc-1.json` in
[pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32),
which is a community fork of `platformio/platform-espressif32`. That project is
under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
Only the board definition is copied here. PlatformIO downloads the platform
itself at build time.

## Linked into the firmware at build time

These components are not in this repository. PlatformIO fetches them. They are
compiled into every binary that you flash or that you redistribute.

| Component | License | Notes |
| --- | --- | --- |
| [ArduinoJson](https://arduinojson.org/) 7.x — Copyright © 2014-2026, Benoit Blanchon | MIT | A redistributed copy must include the copyright notice and the permission notice. `src/kasa.cpp` uses this library to parse the Kasa responses. |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) (the Arduino core for the ESP32) | LGPL-2.1-or-later | Some bundled components have their own, more permissive licenses. If you distribute firmware binaries, the relinking obligations of the LGPL apply. |
| [ESP-IDF](https://github.com/espressif/esp-idf) and the RISC-V toolchain | Apache-2.0, and third-party components under their own licenses | The Arduino core pulls these in as its SDK. |

## Trademarks and protocols

- **M5Stack** and **NanoC6** are trademarks of M5Stack Technology Co., Ltd.
  This project has no affiliation with M5Stack, and M5Stack does not endorse it.
  The pin assignments come from the public
  [NanoC6 documentation](https://docs.m5stack.com/en/core/M5NanoC6).
- **TP-Link** and **Kasa** are trademarks of TP-Link Technologies Co., Ltd.
  This project has no affiliation with TP-Link, and TP-Link does not endorse it.
  The HS200 client speaks the legacy TP-Link Smart Home protocol. This protocol
  has no documentation, and other people reverse-engineered it in public. The
  code in `src/kasa.cpp` contains no TP-Link code.
