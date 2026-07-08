#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/** Initialize and start battery measurement. */
int battery_charge_start(void);

/** Read battery voltage in millivolts. */
int battery_get_millivolt(uint16_t *battery_millivolt);

/** Convert millivolts to a 0-100% estimate (single-cell LiPo). */
int battery_get_percentage(uint8_t *battery_percentage, uint16_t battery_millivolt);

#endif // BATTERY_H
