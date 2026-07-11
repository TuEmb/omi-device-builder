#include "led.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, CONFIG_LOG_DEFAULT_LEVEL);

/* RGB status LED via the standard led0/led1/led2 gpio-led aliases that XIAO
 * boards provide (red/green/blue, usually active-low — handled by the DT flags).
 * GPIO_DT_SPEC_GET_OR keeps missing colors as {0} so single-LED boards still
 * build; those channels become no-ops. */
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0});
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0});

static void cfg(const struct gpio_dt_spec *s)
{
    if (s->port != NULL && gpio_is_ready_dt(s)) {
        gpio_pin_configure_dt(s, GPIO_OUTPUT_INACTIVE);
    }
}

static void set(const struct gpio_dt_spec *s, bool on)
{
    if (s->port != NULL && gpio_is_ready_dt(s)) {
        gpio_pin_set_dt(s, on ? 1 : 0);
    }
}

int led_start(void)
{
    cfg(&led_r);
    cfg(&led_g);
    cfg(&led_b);
    LOG_INF("LEDs (GPIO) started");
    return 0;
}

void set_led_red(bool on)
{
    set(&led_r, on);
}

void set_led_green(bool on)
{
    set(&led_g, on);
}

void set_led_blue(bool on)
{
    set(&led_b, on);
}

void led_off(void)
{
    set_led_red(false);
    set_led_green(false);
    set_led_blue(false);
}
