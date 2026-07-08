# overlays/ — per-board user overlays (button pin, etc.)

Most XIAO boards have **no on-board user button** (only RESET), so if you want
the button feature you must declare the GPIO your button is wired to. Put that
here, one file per device: `overlays/<device>.overlay`.

These are applied **on top of** `boards/<device>.overlay` by the build scripts
(`build_all.*` passes both via `EXTRA_DTC_OVERLAY_FILE`). Keeping them separate
means you can tweak the button/pin wiring without touching the board's mic/LED/
battery definitions.

Each template defines a `gpio-keys` node and the `sw0` alias that `src/button.c`
looks for (`DT_ALIAS(sw0)`). Edit the `gpios = <...>` line to your wired pin.
If a board has no `overlays/<device>.overlay`, the button simply compiles out.

GPIO controller labels differ per SoC:
- nRF52840 / nRF54L15: `&gpio0`, `&gpio1` (nRF54L also `&gpio2`)
- ESP32-S3: `&gpio0`
- EFR32MG24 (Silabs): `&gpioa`, `&gpiob`, `&gpioc`, `&gpiod`
