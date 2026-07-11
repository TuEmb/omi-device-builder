# Contributing / Developer guide

Everything you need to change features, add a board, or understand the build.
For the user-facing overview see [README.md](README.md).

## How it relates to the omi firmware

A port of the Omi wearable firmware for the Seeed XIAO Sense family. The audio
service UUIDs, Opus packet framing, and codec ID are kept **identical** to the omi
firmware, so the Omi app talks to these devices for live streaming.

| Kept | Dropped |
|---|---|
| Live BLE audio streaming (audio service `19B10000…`, Opus/`CODEC_ID 21`) | T5838 hardware AAD (T5838-specific) |
| PDM mic capture (Zephyr `dmic` driver) | Haptic motor, speaker |
| Battery monitoring (ADC + BLE Battery Service) | IMU / accelerometer |
| User button, RGB status LED | Dual-core nRF5340 sysbuild / net-core |
| Settings + Features + Time-sync GATT services | |

Offline recording/sync to mass storage is being re-added as an optional
`storage` component (SD / external flash); see below.

## Architecture — components & features

The firmware is **component-driven**: a board's devicetree declares *what hardware
it has*, and each component auto-enables from that. Features are toggled in
Kconfig, but a feature can only be enabled when the board actually has the
hardware (its Kconfig default is gated on a devicetree signal).

| Component | Enable symbol | Auto-on when the devicetree has… | Backends |
|---|---|---|---|
| Mic (required) | `OMI_MIC` | alias `dmic-dev` (PDM) | `OMI_MIC_BACKEND_PDM` \| `_ADC` |
| LED (required) | `OMI_LED` | a `gpio-leds` or `pwm-leds` node | — |
| Codec | `OMI_CODEC_OPUS` | (default on) | opus |
| Battery | `OMI_BATTERY` | (default on; ADC divider) | — |
| Button | `OMI_BUTTON` | alias `sw0` | — |
| Storage | `OMI_STORAGE` | nodelabel `sdmmc_disk` (SD) or `omi_storage` (flash) | `_SD` (FATFS) \| `_NAND` (littlefs) |

Source layout:

```
src/
├── main.c                            # orchestration (enabled components only)
├── components/
│   ├── mic/     mic.h · mic_pdm.c · mic_adc.c   (adc is a skeleton)
│   ├── codec/   codec.h · codec.c   (Opus)
│   ├── led/ · battery/ · button/
│   └── storage/ storage.h · storage_sd.c · storage_nand.c
├── services/    transport.c · settings.c        # BLE-facing
└── opus-1.2.1/                        # vendored Opus
```

Each component is a stable `<name>.h` interface plus one `.c` per backend
(selected by a Kconfig `choice`). Optional feature bundles (BLE OTA, USB-CDC
console) are Zephyr **snippets** under `snippets/`; the MCUboot bootloader image
is built via sysbuild (`sysbuild.conf`, `sysbuild/`).

## Adding a board

1. Copy the templates:
   ```sh
   cp boards/template.overlay boards/<board>.overlay
   cp boards/template.conf    boards/<board>.conf
   ```
2. In the `.overlay`, declare **only** the hardware your board has (uncomment and
   fill the blocks — mic `dmic-dev`, LED node, button `sw0`, battery ADC, storage
   `sdmmc_disk`/`omi_storage`). Anything you omit compiles out automatically.
3. In the `.conf`, set `CONFIG_BT_DEVICE_NAME="omi-<board>"` and any backend
   override (e.g. `CONFIG_OMI_MIC_BACKEND_ADC=y` for an analog mic).
4. Build: `west build -b <board target>` (or add a line to `scripts/build_all.*`).

Minimum for a functional device is **mic + LED**; BLE-only placeholder boards may
disable both.

### Devicetree / Kconfig gotchas

- Kconfig's `dt_compat_enabled` takes a single argument, so it can't match a
  comma-containing compatible like `zephyr,sdmmc-disk`. Presence is therefore
  keyed on **nodelabels** (`sdmmc_disk`, `omi_storage`) via `dt_nodelabel_enabled`.
- `OMI_CODEC_OPUS` is intentionally independent of `OMI_MIC`: the codec framing
  constants are part of the BLE audio protocol used by the always-built transport,
  so mic-less boards still need them.

## Build system

Requirements: **west** + a Python venv + **Zephyr SDK ≥ 1.0** (v4.4.x needs it;
older 0.16/0.17 SDKs are rejected). This is a **west manifest repo** (`west.yml`)
pinned to Zephyr **v4.4.1**; change `revision:` in `west.yml` to move versions.

See [README.md](README.md#-get-it-on-your-device) for one-time workspace setup.

```sh
# Standard firmware, all boards (or pass one board name):
./scripts/build_all.sh [xiao52]        # powershell -File scripts/build_all.ps1

# Full firmware with MCUboot + BLE OTA + USB-C DFU/console (xiao_ble):
./scripts/build_ota.sh                 # powershell -File scripts/build_ota.ps1
```

The OTA build uses sysbuild with per-image snippets — `-S` alone would leak
mcumgr into the MCUboot image, so `scripts/build_ota.*` scopes the `ota` /
`usb-console` snippets to the application image only.

ESP32-S3 additionally needs Espressif blobs (`west blobs fetch hal_espressif`);
until present it builds with `CONFIG_BUILD_ONLY_NO_BLOBS=y` (BLE not functional).

## Boards needing work

- **Verify board pins before flashing.** `boards/*.overlay` pins were written from
  Seeed variant references and are marked `TODO(verify)`. Also tune
  `BATTERY_DIVIDER_MILLI` in `src/components/battery/battery.c`.
- **xiaomg24** — onboard mic is the **analog** MSM381ACT001 (DATA=PC9, PWR=PC8),
  not PDM. Needs a 16 kHz ADC-sampling backend (`mic_adc.c` is a skeleton) feeding
  `codec_receive_pcm()`. BLE otherwise works.
- **xiaoesp32s3** — onboard PDM mic (CLK=GPIO42, DIN=GPIO41), but Zephyr v4.4.1
  has no ESP32 PDM-RX `dmic` backend. Needs a driver first.
