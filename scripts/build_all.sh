#!/usr/bin/env bash
# Build omi-device-builder for every supported XIAO board.
# Run from the project root inside your nRF Connect SDK (latest) environment:
#   ./scripts/build_all.sh            # build all
#   ./scripts/build_all.sh xiao52     # build one
set -euo pipefail

# device name : Zephyr board target
declare -A BOARDS=(
    [xiao52]="xiao_ble/nrf52840/sense"
    [xiao54l]="xiao_nrf54l15/nrf54l15/cpuapp"
    [xiaoesp32s3]="xiao_esp32s3/esp32s3/procpu"
    [xiaomg24]="xiao_mg24/efr32mg24b220f1536im48"
)

only="${1:-}"

for name in "${!BOARDS[@]}"; do
    if [ -n "$only" ] && [ "$name" != "$only" ]; then
        continue
    fi
    board="${BOARDS[$name]}"
    outdir="build/omi-$name"
    dir="boards/$name"
    echo "==> Building omi-$name (board $board)"
    # Everything for a board lives in boards/<name>/ .
    west build -b "$board" -d "$outdir" -p always -- \
        "-DEXTRA_CONF_FILE=$dir/$name.conf" \
        "-DEXTRA_DTC_OVERLAY_FILE=$dir/$name.overlay"
    # Nordic/Silabs produce zephyr.hex; ESP32-S3 produces zephyr.bin (esptool).
    if [ -f "$outdir/zephyr/zephyr.hex" ]; then
        cp "$outdir/zephyr/zephyr.hex" "build/omi-$name.hex"
        echo "    -> build/omi-$name.hex"
    elif [ -f "$outdir/zephyr/zephyr.bin" ]; then
        cp "$outdir/zephyr/zephyr.bin" "build/omi-$name.bin"
        echo "    -> build/omi-$name.bin"
    else
        echo "ERROR: no zephyr.hex/.bin produced for $name" >&2
        exit 1
    fi
done

echo "All builds done."
