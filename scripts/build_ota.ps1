# Build the MCUboot OTA firmware for omi-xiao52 (Seeed XIAO nRF52840 Sense).
#
# Produces a MCUboot bootloader + a signed application in slot0, with:
#   - BLE OTA        (mcumgr SMP over Bluetooth)              -> `ota` snippet
#   - USB-C DFU      (MCUboot serial recovery on RESET press) -> sysbuild/mcuboot.*
#   - USB-C log view (CDC ACM console)                        -> `usb-console` snippet
#
# Run from the project root inside your Zephyr environment:
#   powershell -ExecutionPolicy Bypass -File scripts/build_ota.ps1
#
# Flash both images over J-Link (VTref/3V3 wired):
#   nrfjprog -f NRF52 --recover
#   nrfjprog --program build/omi-xiao52-mcuboot/mcuboot/zephyr/zephyr.hex --sectorerase --verify -f NRF52
#   nrfjprog --program build/omi-xiao52-mcuboot/omi-device-builder/zephyr/zephyr.signed.hex --sectorerase --verify -f NRF52
#   nrfjprog -f NRF52 --reset

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot/..").Path

# The snippets live under <root>/snippets and are applied to the APPLICATION
# image only (the sysbuild image is named after this directory: "omi-device-builder").
# Applying them globally with -S would leak mcumgr into the MCUboot image and
# break its build, so they are scoped per-image here.
west build --sysbuild -b xiao_ble/nrf52840/sense -s $root -d "$root/build/omi-xiao52-mcuboot" -p always -- `
    "-DSNIPPET_ROOT=$root" `
    "-Domi-device-builder_SNIPPET=ota;usb-console" `
    "-DEXTRA_CONF_FILE=boards/xiao52.conf" `
    "-DEXTRA_DTC_OVERLAY_FILE=boards/xiao52.overlay;overlays/xiao52.overlay"

if ($LASTEXITCODE -ne 0) { Write-Host "OTA build FAILED" -ForegroundColor Red; exit 1 }
Write-Host "OTA build done:" -ForegroundColor Green
Write-Host "  bootloader : build/omi-xiao52-mcuboot/mcuboot/zephyr/zephyr.hex"
Write-Host "  app (signed): build/omi-xiao52-mcuboot/omi-device-builder/zephyr/zephyr.signed.hex"
