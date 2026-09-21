# BYTE90 Hardware Specs

**Scope:** components, GPIO assignments, shared buses, and power rails.

**Source of truth:** `lib/system/DeviceConfig.h`. If a pin changes there, update
this document.

**Read this when:** wiring a peripheral, checking a pin conflict, or reasoning
about power and wake behavior.

| Related | Covers |
| --- | --- |
| [`HARDWARE_TEST_GUIDE.md`](./HARDWARE_TEST_GUIDE.md) | Which test exercises each peripheral |

Pin assignments use the Arduino `D0`-`D10` aliases. GPIO numbers below come from
the `XIAO_ESP32S3` variant that `platformio.ini` selects via
`board = seeed_xiao_esp32s3`.

## MCU Module

| Property | Value |
| --- | --- |
| Module | Seeed Studio XIAO ESP32-S3 |
| SoC | ESP32-S3 (Xtensa LX7 dual-core) |
| Clock | 240 MHz |
| Flash | 8 MB (QIO) |
| PSRAM | 16 MB, octal (OPI); enabled via `BOARD_HAS_PSRAM` |
| USB | Native CDC on boot (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`) |
| Framework | Arduino via `pioarduino` platform-espressif32 |
| Partition table | `partitions.csv` |
| Arduino variant | `XIAO_ESP32S3` |

Flash layout from `partitions.csv`:

| Partition | Type | Offset | Size |
| --- | --- | --- | --- |
| `nvs` | data/nvs | `0x9000` | 16 KB |
| `otadata` | data/ota | `0xd000` | 8 KB |
| `phy_init` | data/phy | `0xf000` | 4 KB |
| `ota_0` | app | `0x10000` | 2496 KB |
| `ota_1` | app | `0x280000` | 2432 KB |
| `coredump` | data/coredump | `0x4e0000` | 128 KB |
| `assets` | data/spiffs (LittleFS) | `0x500000` | 3072 KB |

The `assets` partition holds `data/`: the startup GIF and the two sounds the
hardware tests play.

### PSRAM Usage

Internal SRAM is 320 KB, so the memory-hungry paths run out of PSRAM instead.

`platformio.ini` enables `CONFIG_SPIRAM_USE_MALLOC`,
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, and
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, so the general allocator, large BSS
segments, and the WiFi/lwIP stacks can all draw from external RAM. TLS buffers
are also pushed outward through `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` and the
dynamic-buffer options.

Deliberate PSRAM consumers:

- `AudioCodec` allocates its I2S working buffers with `MALLOC_CAP_SPIRAM`
- `Mp3Player` allocates PCM buffers there, and preloads a whole MP3 into PSRAM
  when the free PSRAM exceeds the file size
- `Mp3Decoder` uses `heap_caps_malloc_prefer()` to take PSRAM first for bulk
  allocations and internal RAM first for latency-sensitive ones

The firmware never queries total PSRAM size, only free size before and after
allocation. Size is detected by the bootloader at startup and reported in the
boot log.

## Component To GPIO Map

One row per component. Pins are written as `signal alias/GPIO`, so `SCK D8/7`
means signal `SCK` on alias `D8`, which is GPIO 7.

| Component | Part | Bus | Pins |
| --- | --- | --- | --- |
| Display | SSD1351, 128x128 OLED | SPI | `SCK D8/7`, `MOSI D10/9`, `CS D7/44`, `DC D6/43` |
| PMIC | AXP2101 | I2C | Shared bus, plus `IRQ D9/8` |
| RTC | PCF8563 | I2C | Shared bus |
| Accelerometer | ADXL345 | I2C | Shared bus |
| Haptics | DRV2605L | I2C | Shared bus |
| Speaker amp | MAX98357A | I2S | `BCLK D3/4`, `LRCLK D1/2`, `DIN D2/3` |
| Microphone | ICS-43434 | I2S | `BCLK D3/4`, `LRCLK D1/2`, `DOUT D0/1` |

"Shared bus" means SDA on `D4`/GPIO 5 and SCL on `D5`/GPIO 6, common to all
four I2C devices. The AXP2101 additionally drives a dedicated interrupt line.
Device addresses are listed under Shared Buses below.

The display has a fifth signal, `RESET`, with no GPIO assigned. It is driven
by a hardware supervisor rather than firmware; see Display Reset Supervisor.

The speaker and microphone share `BCLK` and `LRCLK` because they run on one
full-duplex I2S peripheral. Only their data lines differ.

### Pin Usage Summary

Every GPIO the firmware drives, in alias order:

| Alias | GPIO | Assigned to |
| --- | --- | --- |
| `D0` | 1 | I2S microphone data in |
| `D1` | 2 | I2S LRCLK, shared by mic and speaker |
| `D2` | 3 | I2S speaker data out |
| `D3` | 4 | I2S BCLK, shared by mic and speaker |
| `D4` | 5 | I2C SDA, shared bus |
| `D5` | 6 | I2C SCL, shared bus |
| `D6` | 43 | Display DC |
| `D7` | 44 | Display CS |
| `D8` | 7 | Display SPI SCK |
| `D9` | 8 | AXP2101 interrupt |
| `D10` | 9 | Display SPI MOSI |

`D0` through `D10` are fully allocated. There are no free pins on the standard
XIAO ESP32-S3 header, which is why the display reset is handled by a hardware
supervisor instead of a GPIO. See Display Reset Supervisor below.

## Shared Buses

### I2C

One bus serves four peripherals, arbitrated by `lib/i2c/SharedI2cBus.*`.

| Device | Address | Role |
| --- | --- | --- |
| AXP2101 | `0x34` | Power management, charging, battery telemetry, power button |
| PCF8563 | `0x51` | Real-time clock, time survives power loss |
| ADXL345 | `0x53` | Accelerometer for motion, orientation, tap, and light-sleep wake |
| DRV2605L | `0x5A` | Haptic feedback driver |

- SDA `D4` / GPIO 5, SCL `D5` / GPIO 6
- Default clock 400 kHz
- `SharedI2cBus::scanDevices()` identifies these four addresses by name; anything else
  logs as an unknown device

### I2S

The microphone and speaker share a single full-duplex I2S peripheral
(`I2S_NUM_FULLDUPLEX`), so BCLK and LRCLK are common and only the data lines
differ.

| Property | Value |
| --- | --- |
| Mode | Master, RX and TX simultaneously |
| Input sample rate | 16 kHz |
| Output sample rate | 16 kHz |
| Shared clocks | BCLK `D3`, LRCLK `D1` |
| Mic data in | `D0` |
| Speaker data out | `D2` |

Because the channel is full-duplex, RX and TX cannot be stopped independently.
Disabling the speaker does not free the microphone, and vice versa.

### SPI

The display is the only SPI device, and the SSD1351 is the only display this
board carries.

| Property | Value |
| --- | --- |
| Controller | SSD1351 |
| Resolution | 128 x 128 |
| Clock | 18 MHz |
| MISO | Unused; the bus is initialized with MISO as `-1` because the panel is write-only |
| Reset | No GPIO; see Display Reset Supervisor below |

## Display Reset Supervisor

`DISPLAY_RESET_PIN` is `-1` because the SSD1351 reset is generated in
hardware, not by firmware. `D0`-`D10` are fully allocated, so no GPIO was left
for a display reset line, and the function was moved onto a dedicated
supervisor IC.

Circuit, reference designator `U12`:

| Pin | Signal | Connection |
| --- | --- | --- |
| 1 | `RESET#` | `RST` net, driving the display reset |
| 2 | `GND` | Ground |
| 3 | `MR#` | Pulled to 3V3 through `R23`, 10 kOhm |
| 4 | `WDI` | Not connected |
| 5 | `VDD` | 3V3, decoupled by `C31`, 100 nF |

| Property | Value |
| --- | --- |
| Part | TPS3823-33DBVR |
| Package | SOT-23-5 (`DBVR`) |
| Monitored rail | 3V3 (the `-33` suffix denotes the 3.3 V variant) |
| Reset output | Active low, asserted while the rail is below threshold and for a fixed delay after it recovers |
| Manual reset | Available on `MR#`, idle high through `R23` |
| Watchdog | Disabled; `WDI` is intentionally left floating |

Consequences for firmware:

- the panel is already out of reset by the time `ArduinoSSD1351::begin()`
  runs, so the driver is constructed with a reset pin of `-1` and never
  toggles one
- firmware cannot reset the display on demand; recovering a wedged panel means
  re-running the SSD1351 init sequence or power-cycling the board
- brownouts on the 3V3 rail reset the display through the supervisor rather
  than through any code path

Consult the TPS3823 datasheet for exact threshold and reset-timeout figures;
the values above describe the wiring, not the part's electrical
specifications.

## Power

Managed by the AXP2101 PMIC over I2C, configured in `lib/power/Axp2101.cpp`.

| Setting | Value |
| --- | --- |
| VBUS voltage limit | 4.36 V |
| VBUS current limit | 900 mA |
| Charge target voltage | 4.2 V |
| Constant charge current | 400 mA |
| Precharge current | 75 mA |
| Termination current | 25 mA |
| System power-down voltage | 2800 mV |
| Low-battery shutdown threshold | 5 % |
| Thermal threshold | 80 °C |
| TS pin measurement | Disabled |

### Power Rails

**`DCDC1` is the only rail available in hardware on BYTE-90. Every other AXP2101
output is unconnected and must be disabled.** This is a hardware constraint,
not a power optimization: the remaining rails go nowhere, so enabling one
regulates into an open circuit and wastes quiescent current for no benefit.

| Rail | State | Notes |
| --- | --- | --- |
| `DCDC1` | Enabled | The only wired rail; supplies the system |
| `DCDC2` - `DCDC5` | Must be disabled | Not connected |
| `ALDO1` - `ALDO4` | Must be disabled | Not connected |
| `BLDO1`, `BLDO2` | Must be disabled | Not connected |
| `DLDO1`, `DLDO2` | Must be disabled | Not connected |

`AXP2101::begin()` disables all twelve unused rails on every boot, then clears
low-voltage turn-off for `DCDC2`-`DCDC5` so those disabled rails cannot trip
brownout handling.

Firmware never writes `DCDC1`. It is enabled by the PMIC's own power-on
sequence and its voltage is left at the hardware default; the driver only
reads `isEnableDC1()` and `getDC1Voltage()` back for a startup log line.

Any change here must keep `DCDC1` as the sole enabled output. Enabling another
rail requires a board revision that actually connects it.

### Power Button And Wake

The power button is wired to the AXP2101, not to an MCU GPIO. The PMIC reports
presses over I2C and asserts its interrupt line on `D9` / GPIO 8.

`AXP2101::begin()` enables the short-press, long-press, and release IRQs.
The long-press IRQ fires after about 1 s. The PMIC's own hard power-off
(REG 0x27 OFFLEVEL, with long-press shutdown enabled in REG 0x22) triggers
after a 6 s hold by default. The hardware test firmware disables it with
`AXP2101::setHardwarePowerOffEnabled(false)` and instead opens a software
power menu after a 6 s hold, which powers off through `AXP2101::shutdown()`.
The test firmware polls these IRQs over I2C and does not use light sleep.

## Related Documents

- `docs/HARDWARE_TEST_GUIDE.md`
