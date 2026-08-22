# Attributions

This project's own source is released under the Unlicense (see `LICENSE`). The
material below is other people's work, either included in this repository or
linked into the firmware at build time.

## Included in this repository

### iot-bedside-button (upstream project)

`src/kasa.cpp`, `src/kasa.h`, `load_env.py`, and `.env.example` are copied
verbatim from [tangent160/iot-bedside-button](https://github.com/tangent160/iot-bedside-button);
`src/main.cpp`, `README.md`, and `CLAUDE.md` are derived from it. That project
is released into the public domain under the [Unlicense](https://unlicense.org),
so no attribution is required — this note is a courtesy.

### pioarduino platform-espressif32

`boards/m5stack-nanoc6.json` is adapted from `boards/esp32-c6-devkitc-1.json` in
[pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32)
(a community fork of `platformio/platform-espressif32`), which is licensed under
the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0). The
platform itself is downloaded at build time rather than vendored here; only the
board definition is adapted.

## Linked into the firmware at build time

These are fetched by PlatformIO and are not present in this repository, but they
are compiled into any binary you flash or redistribute.

| Component | License | Notes |
| --- | --- | --- |
| [ArduinoJson](https://arduinojson.org/) 7.x — Copyright © 2014-2026, Benoit Blanchon | MIT | Requires the copyright and permission notice to be included in redistributed copies. Used by `src/kasa.cpp` to parse Kasa responses. |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) (Arduino core for ESP32) | LGPL-2.1-or-later | Some bundled components carry their own, more permissive licenses. If you distribute firmware binaries, the LGPL's relinking obligations apply. |
| [ESP-IDF](https://github.com/espressif/esp-idf) and the RISC-V toolchain | Apache-2.0, plus third-party components under their own licenses | Pulled in as the Arduino core's underlying SDK. |

## Trademarks and protocols

- **M5Stack** and **NanoC6** are trademarks of M5Stack Technology Co., Ltd. This
  project is not affiliated with or endorsed by M5Stack. Pin assignments were
  taken from the [public NanoC6 documentation](https://docs.m5stack.com/en/core/M5NanoC6).
- **TP-Link** and **Kasa** are trademarks of TP-Link Technologies Co., Ltd. This
  project is not affiliated with or endorsed by TP-Link. The HS200 client speaks
  the legacy TP-Link Smart Home protocol, which is undocumented and was
  publicly reverse-engineered by others; the implementation in `src/kasa.cpp`
  contains no TP-Link code.
