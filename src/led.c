#include "led.h"

#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "settings.h"
#include "utils.h"

LOG_MODULE_REGISTER(led, CONFIG_LOG_DEFAULT_LEVEL);

/* Each board overlay provides led_red / led_green / led_blue PWM nodelabels.
 * The Xiao boards expose an onboard RGB LED (active-low); the overlay maps
 * these to PWM channels. */
static const struct pwm_dt_spec led_red = PWM_DT_SPEC_GET(DT_NODELABEL(led_red));
static const struct pwm_dt_spec led_green = PWM_DT_SPEC_GET(DT_NODELABEL(led_green));
static const struct pwm_dt_spec led_blue = PWM_DT_SPEC_GET(DT_NODELABEL(led_blue));

int led_start(void)
{
    ASSERT_TRUE(pwm_is_ready_dt(&led_red));
    ASSERT_TRUE(pwm_is_ready_dt(&led_green));
    ASSERT_TRUE(pwm_is_ready_dt(&led_blue));
    LOG_INF("LEDs (PWM) started");
    return 0;
}

static void set_led_on_off(const struct pwm_dt_spec *led, bool on)
{
    if (!pwm_is_ready_dt(led)) {
        return;
    }
    uint32_t pulse_width_ns = 0;
    if (on) {
        uint8_t ratio = app_settings_get_dim_ratio();
        if (ratio > 100) {
            ratio = 100;
        }
        pulse_width_ns = (led->period * ratio) / 100;
    }
    pwm_set_pulse_dt(led, pulse_width_ns);
}

void set_led_red(bool on)
{
    set_led_on_off(&led_red, on);
}

void set_led_green(bool on)
{
    set_led_on_off(&led_green, on);
}

void set_led_blue(bool on)
{
    set_led_on_off(&led_blue, on);
}

void set_led_pwm(led_color_t color, uint8_t level)
{
    const struct pwm_dt_spec *led;
    switch (color) {
    case LED_RED:
        led = &led_red;
        break;
    case LED_GREEN:
        led = &led_green;
        break;
    case LED_BLUE:
        led = &led_blue;
        break;
    default:
        return;
    }
    if (!pwm_is_ready_dt(led)) {
        return;
    }
    if (level > 100) {
        level = 100;
    }
    pwm_set_pulse_dt(led, (led->period * level) / 100);
}

void led_off(void)
{
    set_led_red(false);
    set_led_green(false);
    set_led_blue(false);
}
