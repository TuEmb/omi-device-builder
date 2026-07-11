#include "battery.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery, CONFIG_LOG_DEFAULT_LEVEL);

/* Battery ADC channel comes from the board overlay's zephyr,user node:
 *   / { zephyr,user { io-channels = <&adc CH>; }; };
 * The on-board resistor divider ratio must be set per board (see overlay /
 * BATTERY_DIVIDER_* below). */
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#define HAS_BATTERY_ADC 1
static const struct adc_dt_spec batt_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#endif

/* Optional GPIO to enable the battery-measurement divider (Xiao nRF52840:
 * P0.14 low = enable read). Provided via the "batt-enable-gpios" property on
 * zephyr,user if present. */
#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), batt_enable_gpios)
#define HAS_BATTERY_ENABLE 1
static const struct gpio_dt_spec batt_enable = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), batt_enable_gpios);
#endif

/* Resistor-divider multiplier: Vbatt = Vadc * (R1 + R2) / R2.
 * Default 2.0 (x1000 fixed point). Override per board with BATTERY_DIVIDER_NUM/DEN
 * in the board .conf via CFLAGS if the divider differs. */
#ifndef BATTERY_DIVIDER_MILLI
#define BATTERY_DIVIDER_MILLI 2000
#endif

int battery_charge_start(void)
{
#ifdef HAS_BATTERY_ADC
    if (!adc_is_ready_dt(&batt_adc)) {
        LOG_ERR("Battery ADC not ready");
        return -ENODEV;
    }
    int ret = adc_channel_setup_dt(&batt_adc);
    if (ret) {
        LOG_ERR("adc_channel_setup failed: %d", ret);
        return ret;
    }
#ifdef HAS_BATTERY_ENABLE
    if (gpio_is_ready_dt(&batt_enable)) {
        gpio_pin_configure_dt(&batt_enable, GPIO_OUTPUT_ACTIVE);
    }
#endif
    LOG_INF("Battery ADC ready");
    return 0;
#else
    LOG_WRN("No battery ADC channel configured for this board");
    return 0;
#endif
}

int battery_get_millivolt(uint16_t *battery_millivolt)
{
#ifdef HAS_BATTERY_ADC
    int16_t sample = 0;
    struct adc_sequence sequence = {
        .buffer = &sample,
        .buffer_size = sizeof(sample),
    };
    int ret = adc_sequence_init_dt(&batt_adc, &sequence);
    if (ret) {
        return ret;
    }
    ret = adc_read_dt(&batt_adc, &sequence);
    if (ret) {
        return ret;
    }

    int32_t mv = sample;
    ret = adc_raw_to_millivolts_dt(&batt_adc, &mv);
    if (ret) {
        return ret;
    }

    mv = (mv * BATTERY_DIVIDER_MILLI) / 1000;
    *battery_millivolt = (uint16_t) mv;
    return 0;
#else
    *battery_millivolt = 0;
    return -ENOTSUP;
#endif
}

int battery_get_percentage(uint8_t *battery_percentage, uint16_t battery_millivolt)
{
    /* Coarse single-cell LiPo curve. */
    static const struct {
        uint16_t mv;
        uint8_t pct;
    } curve[] = {
        {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3800, 60}, {3700, 45},
        {3600, 25},  {3500, 12}, {3400, 5},  {3300, 0},
    };

    if (battery_millivolt >= curve[0].mv) {
        *battery_percentage = 100;
        return 0;
    }
    for (size_t i = 0; i < ARRAY_SIZE(curve) - 1; i++) {
        if (battery_millivolt <= curve[i].mv && battery_millivolt > curve[i + 1].mv) {
            /* Linear interpolate between points. */
            uint16_t span_mv = curve[i].mv - curve[i + 1].mv;
            uint8_t span_pct = curve[i].pct - curve[i + 1].pct;
            *battery_percentage = curve[i + 1].pct + ((battery_millivolt - curve[i + 1].mv) * span_pct) / span_mv;
            return 0;
        }
    }
    *battery_percentage = 0;
    return 0;
}
