#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_RED,
    LED_GREEN,
    LED_BLUE
} led_color_t;

int led_start(void);
void set_led_red(bool on);
void set_led_green(bool on);
void set_led_blue(bool on);
void set_led_pwm(led_color_t color, uint8_t level);
void led_off(void);

#endif // LED_H
