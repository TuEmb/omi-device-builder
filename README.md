# omi-device-builder

Turn a **Seeed Studio XIAO Sense** board into an **Omi-compatible** wearable that
streams live audio to the [Omi app](https://www.omi.me/) over Bluetooth LE.

It's open firmware you build once and flash onto your XIAO. After the first flash
you can update it wirelessly from your phone or over USB-C — no debugger needed.

<img width="632" height="523" alt="image" src="https://github.com/user-attachments/assets/2c583652-8ab4-469b-9e67-b60671505f4f" />

---

## ✨ What you get

- 🎙️ **Live audio streaming** to the Omi app (Opus, over BLE) — same protocol as
  the official Omi wearable, so the app just works.
- 🔋 **Battery level** reported to your phone.
- 🔘 **Button** — long-press to power off.
- 💡 **Status LED** — shows connected / advertising.
- 💾 **Storage** (optional, on boards that have an SD card or flash chip).
- ⬆️ **Easy updates** — over Bluetooth (from a phone app) or over USB-C.
- 🔌 **USB-C logs** — plug in and watch what the device is doing in any serial
- 💡 **Wake word** — small AI model on device for wake-word.
  terminal.

## 📟 Supported devices

| Device | Chip | Status |
|---|---|---|
| **XIAO nRF52840 Sense** | nRF52840 | ✅ Fully working (mic + audio + battery), tested |
| **XIAO nRF54L15 Sense** | nRF54L15 | ✅ Audio works |
| XIAO MG24 Sense | EFR32MG24 | 🚧 BLE only — mic not supported yet |
| XIAO ESP32-S3 Sense | ESP32-S3 | 🚧 BLE only — mic not supported yet |

The **XIAO nRF52840 Sense** is the recommended board and the one these
instructions assume.

---

## 🚀 Get it on your device

You build the firmware from source once, then flash it. You need a computer with
Python and (for the very first flash) a debug probe such as a **J-Link**.

### 1. One-time setup

```sh
# Create a workspace folder and put this repo inside it, then from the workspace:
python -m venv .venv
. .venv/bin/activate            # Windows: .venv\Scripts\activate
pip install west

west init -l omi-device-builder
west update                     # downloads Zephyr + drivers (takes a while)
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west sdk install -t arm-zephyr-eabi
```

### 2. Build

```sh
cd omi-device-builder
./scripts/build_ota.sh          # Windows: powershell -File scripts/build_ota.ps1
```

This builds the firmware with the bootloader, wireless updates, and USB-C logging
all included.

### 3. Flash it (first time)

Connect your J-Link to the board's SWD pads (and the board's 3V3 to the J-Link's
VREF pin), then:

```sh
nrfjprog -f NRF52 --recover
nrfjprog --program build/omi-xiao52-mcuboot/mcuboot/zephyr/zephyr.hex --sectorerase --verify -f NRF52
nrfjprog --program build/omi-xiao52-mcuboot/omi-device-builder/zephyr/zephyr.signed.hex --sectorerase --verify -f NRF52
nrfjprog -f NRF52 --reset
```

That's the only time you need the debugger — future updates are wireless or USB-C.

---

## 📱 Use it

1. Power the board (USB-C or battery).
2. Open the **Omi app** and scan — your device shows up as **`omi-xiao52`**.
3. Connect. The LED turns solid when connected; audio streams automatically.

**LED meaning:** blinking = advertising / waiting for a connection · solid = connected.

---

## ⬆️ Update the firmware later (no debugger)

- **Over Bluetooth:** use a mcumgr client such as **nRF Connect Device Manager**
  (iOS/Android) → connect to `omi-xiao52` → upload the new
  `zephyr.signed.bin` → the device swaps to it on reboot.
- **Over USB-C:** press the **RESET** button once to enter update mode, then push
  the new firmware with `mcumgr` over the USB-C port.

If an update ever goes wrong, the device automatically falls back to USB-C update
mode so it can't be bricked.

## 🔌 See the logs

Plug the board into USB-C and open the new serial port (COM… / `/dev/tty…`) in any
terminal — the firmware prints what it's doing (startup, connections, battery…).

---

## 🛠️ Troubleshooting

- **Not showing up in the app?** Make sure it's powered and the LED is blinking.
  Move closer; try toggling Bluetooth on your phone.
- **First flash fails / "target voltage low"?** The J-Link needs the board's 3V3
  wired to its VREF pin (not just SWDIO/SWCLK/GND).
- **Want to see what's wrong?** Plug in USB-C and read the logs (above).

---

## 👩‍💻 For developers

Want to add your own board, change features, or understand how it's built? See
**[CONTRIBUTING.md](CONTRIBUTING.md)** — it covers the component architecture, the
devicetree contract, adding a board, and the build system.
