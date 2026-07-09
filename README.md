# omi-device-builder

A single firmware that builds Omi-compatible BLE audio devices for the
**Seeed Studio XIAO Sense** family. It's a port of the Omi wearable firmware
with all mass-storage functionality removed (most XIAO boards have no SD card
or NAND flash), keeping the parts that matter for a streaming device.

Each board produces an artifact named `omi-<device>` and advertises the same
BLE name, e.g. `omi-xiao52`, `omi-xiao54l`.

## What's included vs. the omi firmware

| Kept | Dropped |
|---|---|
| Live BLE audio streaming (Omi audio service `19B10000…`, Opus/`CODEC_ID 21`) | Offline SD / NAND ring storage + BLE sync protocol |
| PDM microphone capture (standard Zephyr `dmic` driver) | T5838 hardware AAD (T5838-specific) |
| Battery monitoring (ADC + BLE Battery Service) | External OTA flash ring |
| User button (long-press power off) | Haptic motor, speaker |
| RGB status LED (PWM) | IMU / accelerometer |
| Settings + Features + Time-sync GATT services (Omi-app compatible) | Dual-core nRF5340 sysbuild / net-core |

The audio service UUIDs, the Opus packet framing, and the codec ID are kept
identical to the omi firmware, so the **Omi app talks to these devices** for
live streaming.

## Supported boards

**Goal: support every XIAO Sense board that has an onboard microphone.** The C3
and RP2040 XIAOs have no mic and are out of scope.

Build status verified against the pinned Zephyr **v4.4.1** (Zephyr SDK 1.0.1):

| Device (`omi-<device>`) | Zephyr board target | SoC | Build | Mic |
|---|---|---|---|---|
| `xiao52`      | `xiao_ble/nrf52840/sense`         | nRF52840 | ✅ FLASH 34% / RAM 64% | ✅ PDM + LED + battery |
| `xiao54l`     | `xiao_nrf54l15/nrf54l15/cpuapp`   | nRF54L15 | ✅ FLASH 17% / RAM 61% | ✅ PDM |
| `xiaomg24`    | `xiao_mg24/efr32mg24b220f1536im48` | EFR32MG24 | ✅ FLASH 16% / RAM 23% | ⛔ pending (analog mic → ADC backend) |
| `xiaoesp32s3` | `xiao_esp32s3/esp32s3/procpu`     | ESP32-S3 | ✅ (build-only, no blobs) | ⛔ pending (PDM, no ESP32 driver) |

All four **build** on Zephyr v4.4.1. The two Nordic boards are fully functional
(mic + BLE audio). The other two build as **BLE placeholders** with the mic
disabled (`CONFIG_OMI_ENABLE_MIC=n`) — see below.

**Why mg24 / esp32s3 have no working mic yet (a missing *driver*, not a missing
overlay).** An overlay only binds pins to a driver that already exists; reading
the Seeed schematics:

- **xiaoesp32s3** — onboard PDM mic MSM261D3526H1CPM, **CLK=GPIO42, DIN=GPIO41**.
  But Zephyr v4.4.1's ESP32 I2S driver has no PDM-RX mode and there's no `dmic`
  backend for ESP32. Needs a Zephyr PDM driver first (extend `i2s_esp32` for
  PDM-RX + mpxxdtyy, or wrap ESP-IDF I2S-PDM). Also: the ESP32 BT controller
  needs Espressif blobs (`west blobs fetch hal_espressif`); until those are
  present this board uses `CONFIG_BUILD_ONLY_NO_BLOBS=y` (builds, BLE not
  functional on device). See `boards/xiaoesp32s3.{conf,overlay}`.
- **xiaomg24** — onboard mic is the **MSM381ACT001, an ANALOG MEMS mic**
  (DATA=PC9, PWR=PC8), not PDM. It can't use the DMIC path at all; it needs a
  16 kHz ADC-sampling mic backend feeding `codec_receive_pcm()`. BLE is fully
  functional (Silabs blobs fetched). See `boards/xiaomg24.overlay`.

Both mics are real firmware work (a PDM driver / an ADC backend) plus on-device
testing — the exact pins are recorded in the overlay files for whoever does it.

Add another board by dropping `boards/<device>.conf` + `boards/<device>.overlay`
and one line in `scripts/build_all.*`.

## Requirements

- **west** + a Python venv, and the **Zephyr SDK ≥ 1.0** (required by Zephyr
  v4.4.x — the arm toolchain covers nRF/MG24, xtensa covers ESP32-S3). Install it
  with `west sdk install` (or `west sdk install -t arm-zephyr-eabi` for just the
  Nordic/MG24 boards). Older bundled SDKs (0.16/0.17) are **not** accepted by v4.4.
- This repo is a **west manifest repo** (`west.yml`) that pulls a **pinned Zephyr
  release** (currently `v4.4.1`) and all vendor HALs. Change the `revision:` in
  `west.yml` to move versions.

## Setup (one-time)

Create a workspace with this repo as the manifest, then pull Zephyr + modules:

```sh
# put this repo at <workspace>/omi-device-builder, then from <workspace>:
python -m venv .venv && . .venv/bin/activate   # (Windows: .venv\Scripts\activate)
pip install west

west init -l omi-device-builder
west update                       # clones Zephyr + all HAL modules (takes a while)
west zephyr-export
pip install -r zephyr/scripts/requirements.txt

# Zephyr SDK >= 1.0 (required by v4.4). ARM-only is enough for xiao52/xiao54l/mg24:
west sdk install -t arm-zephyr-eabi

# ESP32-S3 only: fetch Espressif binary blobs
west blobs fetch hal_espressif
```

Confirm board names are present: `west boards | grep -iE 'xiao|mg24'`.

## Build

From `omi-device-builder/`, with the venv active:

```sh
# all boards
./scripts/build_all.sh            # or: powershell -File scripts/build_all.ps1
# one board
./scripts/build_all.sh xiao52
```

Or a single board manually (board overlay + optional user overlay):

```sh
west build -b xiao_ble/nrf52840/sense -d build/omi-xiao52 -p always -- \
  -DEXTRA_CONF_FILE=boards/xiao52.conf \
  -DEXTRA_DTC_OVERLAY_FILE="boards/xiao52.overlay;overlays/xiao52.overlay"
```

Artifacts are copied to `build/omi-<device>.hex`.

## User button pins — `overlays/`

Most XIAO boards have no on-board user button, so the button GPIO is declared
per wiring in `overlays/<device>.overlay` (defines a `gpio-keys` node + the `sw0`
alias). The build scripts apply it automatically on top of the board overlay.
Edit the `gpios = <...>` line to your pin; if the file is absent, the button
just compiles out. See [overlays/README.md](overlays/README.md).

## ⚠️ Before you flash — verify the board overlays

The overlays in `boards/*.overlay` encode **board-specific pins** (PDM CLK/DIN,
RGB LED channels, battery-sense ADC channel + divider). They were written from
the Seeed variant references and are marked with `TODO(verify)`:

- `boards/xiao52.overlay` — PDM DATA P0.16 / CLK P1.00, RGB LED P0.26/P0.30/P0.06,
  battery on AIN7 (P0.31) with read-enable P0.14. Confirm against your NCS
  `xiao_ble` DTS; if it already defines the Sense mic, delete the `&pdm0` block.
- `boards/xiao54l.overlay` — **placeholders**. Fill in the real pins from the
  `xiao_nrf54l15` DTS.

Also tune `BATTERY_DIVIDER_MILLI` in `src/battery.c` to your board's divider.

## Project layout

```
omi-device-builder/
├── CMakeLists.txt / Kconfig / prj.conf   # build + common config
├── boards/<device>.{conf,overlay}        # per-board name, pins, features
├── scripts/build_all.{sh,ps1}            # build every board
└── src/
    ├── main.c            # init + mic->codec->transport wiring + LED status
    ├── mic.c             # standard nRF PDM (dmic) capture
    ├── codec.c           # Opus encode (from omi, unchanged)
    ├── transport.c       # BLE audio/settings/features/time-sync + battery
    ├── battery.c / button.c / led.c / settings.c
    └── opus-1.2.1/       # vendored Opus (from omi)
```
