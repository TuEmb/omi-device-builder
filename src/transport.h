#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_OMI_ENABLE_BATTERY
extern uint8_t battery_percentage;
#endif

/** Initialize BLE (services + advertising + audio pusher thread). */
int transport_start(void);

/** Turn BLE fully off. */
int transport_off(void);

/** Queue one encoded audio frame for BLE notification. */
int broadcast_audio_packets(uint8_t *buffer, size_t size);

/** Current BLE connection, or NULL. */
struct bt_conn *get_current_connection(void);

/** Move the active link to low-power (slow) or fast streaming params. */
void transport_conn_set_lowpower(bool low);

#endif // TRANSPORT_H
