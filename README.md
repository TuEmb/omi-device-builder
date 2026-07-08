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

| Device (`omi-<device>`) | Zephyr board target | Notes |
|---|---|---|
| `xiao52`  | `xiao_ble/nrf52840/sense` | Seeed XIAO nRF52840 Sense (has onboard PDM mic) |
| `xiao54l` | `xiao_nrf54l15/nrf54l15/cpuapp` | Seeed XIAO nRF54L15 — **verify board name + overlay pins** |

Add another board by dropping `boards/<device>.conf` + `boards/<device>.overlay`
and one line in `scripts/build_all.*`.

## Requirements

- **nRF Connect SDK (latest)** — the Seeed XIAO nRF54L15 board is only present in
  recent NCS/Zephyr. `xiao_ble` has shipped for a long time.
- Verify board names in your SDK: `west boards | grep -i xiao`.

## Build

From the project root, inside your NCS environment:

```sh
# all boards
./scripts/build_all.sh            # or: powershell -File scripts/build_all.ps1
# one board
./scripts/build_all.sh xiao52
```

Or a single board manually:

```sh
west build -b xiao_ble/nrf52840/sense -d build/omi-xiao52 -p always -- \
  -DEXTRA_CONF_FILE=boards/xiao52.conf \
  -DEXTRA_DTC_OVERLAY_FILE=boards/xiao52.overlay
```

Artifacts are copied to `build/omi-<device>.hex`.

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
