#include "settings.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_settings, CONFIG_LOG_DEFAULT_LEVEL);

/* RAM-backed settings. This build has no external flash / NVS by requirement,
 * so values reset on reboot. Swap for the Zephyr settings subsystem (NVS on
 * internal flash) if persistence is needed. */
static uint8_t dim_ratio = 100; /* LED brightness %, full by default */
static uint8_t mic_gain = 4;    /* neutral (1x) */

int app_settings_init(void)
{
    return 0;
}

int app_settings_save_dim_ratio(uint8_t new_ratio)
{
    dim_ratio = new_ratio > 100 ? 100 : new_ratio;
    return 0;
}

uint8_t app_settings_get_dim_ratio(void)
{
    return dim_ratio;
}

int app_settings_save_mic_gain(uint8_t new_gain)
{
    mic_gain = new_gain > 8 ? 8 : new_gain;
    return 0;
}

uint8_t app_settings_get_mic_gain(void)
{
    return mic_gain;
}
