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
)

$only = $args[0]

foreach ($d in $devices) {
    if ($only -and $d.name -ne $only) { continue }

    $outdir = "build/omi-$($d.name)"
    Write-Host "==> Building omi-$($d.name)  (board $($d.board))" -ForegroundColor Cyan

    west build -b $d.board -d $outdir -p always -- `
        "-DEXTRA_CONF_FILE=boards/$($d.name).conf" `
        "-DEXTRA_DTC_OVERLAY_FILE=boards/$($d.name).overlay"

    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build FAILED for omi-$($d.name)" -ForegroundColor Red
        exit 1
    }
    Copy-Item "$outdir/zephyr/zephyr.hex" "build/omi-$($d.name).hex" -Force
    Write-Host "    -> build/omi-$($d.name).hex" -ForegroundColor Green
}

Write-Host "All builds done." -ForegroundColor Green
