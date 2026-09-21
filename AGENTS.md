# Repository Guidelines

BYTE-90 hardware test firmware for the Seeed XIAO ESP32-S3. This branch,
`byte90-demo`, is stripped down from `byte90-core`. It boots the DOS animation
and startup GIF, then cycles one test per peripheral on each button click.

**Full docs:** [`docs/README.md`](docs/README.md) routes by task.

---

## Orientation

| Need | Read |
| --- | --- |
| Test order, controls, pass criteria, troubleshooting | `docs/HARDWARE_TEST_GUIDE.md` |
| Pins, buses, I2C addresses, power | `docs/BYTE90_HARDWARE_SPECS.md` |
| Style rules | `docs/CODING_STYLE_GUIDE.md` |

---

## Skills

Agent skills ship with the repo under `.agents/skills/`. Assume they are
available; load one when its subject comes up rather than reasoning from
memory.

| Skill | Load when |
| --- | --- |
| `embedded-systems` | Firmware, RTOS, or power work: FreeRTOS tasks and queues, interrupts, DMA, memory, real-time constraints |
| `esp-idf` | ESP-IDF and ESP32-S3 APIs: I2S, GPIO, peripherals, and the IDF layer beneath Arduino |

Both carry `references/` subdirectories with deeper material; read those on
demand rather than up front. `gemini-live-api-dev` is also vendored but is
irrelevant on this branch.

---

## Layout

| Path | Holds |
| --- | --- |
| `src/main.cpp` | Entry point; starts `HwTestApp` |
| `src/hwtest/` | `HwTestApp` (bring-up, boot animation), `HwTestRunner` (order, button, serial), `TestScreen` (layout), `HardwareTest` (contract) |
| `src/hwtest/tests/` | One class per test; pass thresholds are constants at the top of each `.cpp` |
| `lib/` | Drivers: `i2c`, `power`, `display`, `haptics`, `adxl`, `clock`, `audio`, `gif_player`, `storage` (LittleFS, WiFi credentials), `ui` (boot animation), `system` |
| `include/StartupImage.h` | Splash bitmap shown after the DOS animation |
| `data/` | LittleFS assets: `gifs/state_startup.gif`, `sounds/startup-95.mp3`, `sounds/ding.mp3` |
| `test/` | Unity tests for the `seeed_xiao_esp32s3_test` environment |

To add a test, subclass `HardwareTest`, add it to `_tests` in
`HwTestRunner.cpp`, and bump `TEST_COUNT`.

---

## Build And Test

The toolchain is **pioarduino**, not PlatformIO; see the README for why the two
are not interchangeable. The CLI still installs under `~/.platformio/`:

```sh
PIO=~/.platformio/penv/bin/platformio

$PIO run -e seeed_xiao_esp32s3              # build
$PIO run -e seeed_xiao_esp32s3 -t upload    # build and flash
$PIO run -e seeed_xiao_esp32s3 -t uploadfs  # flash data/ to the assets partition
$PIO device monitor -b 115200               # serial monitor
$PIO test -e seeed_xiao_esp32s3_test        # unit tests
```

Tests must use `-e seeed_xiao_esp32s3_test`. The default environment sets
`test_ignore = *`, so running tests against it silently does nothing.

---

## Code Style

Full rules in `docs/CODING_STYLE_GUIDE.md`; the summary table at its top covers
most cases.

| Thing | Convention |
| --- | --- |
| Files, classes | `PascalCase` |
| Methods | `camelCase()` |
| Locals | `snake_case` |
| Members | `_snake_case` |
| Constants, enum values | `SCREAMING_SNAKE_CASE` |
| Indentation | 4 spaces, no tabs; K&R braces |
| Line length | 80–100 chars, max 120 |
| Headers | `#pragma once` |

Test pages have 9 body rows of 21 characters; `TestScreen` clips anything longer.

---

## Branch Rules

`byte90-demo` diverges from `byte90-core` on purpose: the app shell, module
seam, portal, and most shared services are removed. Do not merge it back into
`byte90-core` or the module branches, and do not sync those branches'
`src/module/` rules onto it.

Driver fixes in `lib/` that apply to real hardware should be ported to
`byte90-core` separately.

---

## Commits And PRs

Conventional Commits: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`
(e.g. `feat: add gif player`, `fix: status bar drawing`). Present tense,
imperative, subject under ~72 characters.

PRs should carry a summary, testing notes (which tests were run on which
board), references to any updated docs or assets, and photos of the screen for
UI changes.

---

## Configuration

- board, build flags, and `FIRMWARE_VERSION` live in `platformio.ini`; document
  new flags there
- pins and shared constants live in `lib/system/DeviceConfig.h`
- flash layout is `partitions.csv`; a change there requires a full reflash
- new assets go under `data/` and are referenced by explicit path in code
