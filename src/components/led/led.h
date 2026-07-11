#ifndef LED_H
#define LED_H

#include <stdbool.h>

/* Simple on/off RGB status LED over GPIO (portable across XIAO boards, which
 * expose led0/led1/led2 gpio-led aliases). */
int led_start(void);
void set_led_red(bool on);
void set_led_green(bool on);
void set_led_blue(bool on);
void led_off(void);

#endif // LED_H
