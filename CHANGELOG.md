# Changelog

This file records the changes that a user of this firmware can see.

The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the
version numbers obey [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The change groups are Added, Changed, Deprecated, Removed, Fixed, and Security.
Put every new entry under `## [Unreleased]`. The user starts a version bump and
makes the release. There is no release at this time.

## [Unreleased]

The first firmware for the M5Stack NanoC6. This is the port of the Atom Lite
original, and it is the content of the first release.

### Added

- The board definition `boards/m5stack-nanoc6.json` for the NanoC6, because
  pioarduino has none.
- A direct read of GPIO9 with debounce, in place of `M5.BtnA` of M5Unified.
- A check of the saved device ID at every toggle. Thus a DHCP reassignment
  cannot make the button toggle a different device.
- Retries inside `kasaToggle()`. The function retries the status read one time,
  then retries the absolute target state, and then makes sure that the state is
  correct.
- A deadline for each press, `TOGGLE_BUDGET_MS`. The internal retries obey it.
- Backoff for the discovery retries: every 10s for the first 2 minutes, and
  every 60s after that time.
- A reboot after 10 minutes of WiFi failure, but only after WiFi connected one
  time or more.
- The 5s hold, which forgets the saved switch and discovers again. The action
  starts at the moment that the hold passes 5s.
- MIT license, in `LICENSE.md`.
- Third-party notices, in `THIRD-PARTY-NOTICES.md`.
- This changelog.

### Changed

- The TCP timeout is now 1.5s, because all operations block the main loop.
- WiFi credentials come from `.env` at build time, through `load_env.py`.
- The license of this project is now MIT. It was the Unlicense before. The
  material from the upstream project stays in the public domain.

### Removed

- The dependency on M5Unified.
- All LED code. The status goes only to the serial log.
- `ATTRIBUTIONS.md`. `THIRD-PARTY-NOTICES.md` replaces it.
