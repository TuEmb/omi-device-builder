#include "button.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

LOG_MODULE_REGISTER(button, CONFIG_LOG_DEFAULT_LEVEL);

/* User button from devicetree alias sw0 (gpio-keys). Optional: if the board
 * has no sw0, this compiles out gracefully. */
#if DT_NODE_EXISTS(DT_ALIAS(sw0))
#define HAS_BUTTON 1
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

#define LONG_PRESS_MS 2000
static int64_t press_started_ms;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int val = gpio_pin_get_dt(&button);
    if (val > 0) {
        press_started_ms = k_uptime_get();
    } else if (press_started_ms != 0) {
        int64_t held = k_uptime_get() - press_started_ms;
        press_started_ms = 0;
        LOG_INF("Button released after %lld ms", held);
        if (held >= LONG_PRESS_MS) {
            turnoff_all();
        }
    }
}
#endif

int button_init(void)
{
#ifdef HAS_BUTTON
    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("Button GPIO not ready");
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret) {
        return ret;
    }
    ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
    if (ret) {
        return ret;
    }
    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);
    LOG_INF("Button initialized");
    return 0;
#else
    LOG_INF("No user button on this board");
    return 0;
#endif
}

void activate_button_work(void)
{
    /* Button uses GPIO interrupts directly; nothing extra to start. */
}

void register_button_service(void)
{
    /* No BLE button service in this build. */
}

void turnoff_all(void)
{
    LOG_INF("Powering off");
    k_msleep(100);
    sys_poweroff();
}
