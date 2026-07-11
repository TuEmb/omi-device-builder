# Build omi-device-builder for every supported XIAO board.
# Run from the project root inside your nRF Connect SDK (latest) environment:
#   powershell -ExecutionPolicy Bypass -File scripts/build_all.ps1
# Optionally pass a single device name to build just one: ... build_all.ps1 xiao52

$ErrorActionPreference = "Stop"

# device name -> Zephyr board target
$devices = @(
    @{ name = "xiao52";       board = "xiao_ble/nrf52840/sense" }
    @{ name = "xiao54l";      board = "xiao_nrf54l15/nrf54l15/cpuapp" }
    @{ name = "xiaoesp32s3";  board = "xiao_esp32s3/esp32s3/procpu" }
    @{ name = "xiaomg24";     board = "xiao_mg24/efr32mg24b220f1536im48" }
)

$only = $args[0]

foreach ($d in $devices) {
    if ($only -and $d.name -ne $only) { continue }

    $outdir = "build/omi-$($d.name)"
    $dir = "boards/$($d.name)"
    Write-Host "==> Building omi-$($d.name)  (board $($d.board))" -ForegroundColor Cyan

    # Everything for a board lives in boards/<name>/ .
    west build -b $d.board -d $outdir -p always -- `
        "-DEXTRA_CONF_FILE=$dir/$($d.name).conf" `
        "-DEXTRA_DTC_OVERLAY_FILE=$dir/$($d.name).overlay"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build FAILED for omi-$($d.name)" -ForegroundColor Red
        exit 1
    }
    # Nordic/Silabs produce zephyr.hex; ESP32-S3 produces zephyr.bin (esptool).
    if (Test-Path "$outdir/zephyr/zephyr.hex") {
        Copy-Item "$outdir/zephyr/zephyr.hex" "build/omi-$($d.name).hex" -Force
        Write-Host "    -> build/omi-$($d.name).hex" -ForegroundColor Green
    } elseif (Test-Path "$outdir/zephyr/zephyr.bin") {
        Copy-Item "$outdir/zephyr/zephyr.bin" "build/omi-$($d.name).bin" -Force
        Write-Host "    -> build/omi-$($d.name).bin" -ForegroundColor Green
    } else {
        Write-Host "ERROR: no zephyr.hex/.bin produced for omi-$($d.name)" -ForegroundColor Red
        exit 1
    }
}

Write-Host "All builds done." -ForegroundColor Green
