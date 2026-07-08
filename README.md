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

| Device (`omi-<device>`) | Zephyr board target | SoC | Status |
|---|---|---|---|
| `xiao52`      | `xiao_ble/nrf52840/sense`         | nRF52840 | ✅ primary (mic + RGB LED + battery) |
| `xiao54l`     | `xiao_nrf54l15/nrf54l15/cpuapp`   | nRF54L15 | ⚠️ verify board is in the pinned Zephyr tag + overlay pins |
| `xiaoesp32s3` | `xiao_esp32s3/esp32s3/procpu`     | ESP32-S3 | 🧪 experimental; see board conf TODOs (blobs, PDM driver) |
| `xiaomg24`    | `xiao_mg24/efr32mg24b220f1536im48` | EFR32MG24 | 🧪 verify board qualifier + Silabs BLE/PDM in pinned tag |

Notes:
- **Nordic boards** (`xiao52`, `xiao54l`) are the fully-supported path — same
  BLE controller family as the omi firmware.
- **ESP32-S3** and **MG24** use different BLE controllers/toolchains; the common
  `prj.conf` is SoC-agnostic and controller-specific config lives in the Nordic
  board confs, so these boards can be added without breaking the Nordic builds.
  ESP32-S3 needs Espressif blobs and a working PDM/DMIC path (see its `.conf`).
- **MG24** (`xiao_mg24`) and **nRF54L15** (`xiao_nrf54l15`) are recent additions;
  confirm they exist in the pinned Zephyr tag (`west boards | grep -iE 'xiao|mg24'`)
  and bump `west.yml` if not.

Add another board by dropping `boards/<device>.conf` + `boards/<device>.overlay`
and one line in `scripts/build_all.*`.

## Requirements

- **west** + a Python venv, and the **Zephyr SDK** toolchain (covers arm for
  nRF/MG24 and xtensa/riscv for ESP32-S3). NCS's bundled toolchain also works
  for the Nordic boards.
- This repo is a **west manifest repo** (`west.yml`) that pulls a **pinned Zephyr
  release** (currently `v4.2.0`) and all vendor HALs. Change the `revision:` in
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
