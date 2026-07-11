#include "transport.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>

#include "config.h"
#include "features.h"
#include "settings.h"
#ifdef CONFIG_OMI_MIC
#include "mic.h"
#endif
#ifdef CONFIG_OMI_BATTERY
#include "battery.h"
#endif
#ifdef CONFIG_OMI_BUTTON
#include "button.h"
#endif

LOG_MODULE_REGISTER(transport, CONFIG_LOG_DEFAULT_LEVEL);

extern bool is_connected;
#ifdef CONFIG_OMI_BATTERY
extern bool is_charging;
#endif

static atomic_t pusher_stop_flag;
/* Set while we deliberately hold the link at slow params (no audio to stream),
 * so the "interval too high -> re-request fast" logic stays quiet. */
static atomic_t conn_lowpower = ATOMIC_INIT(0);

struct bt_conn *current_connection = NULL;
uint16_t current_mtu = 0;

//
// --- Software wall clock (backs the Time Sync service, no RTC hardware) ---
//
static uint32_t time_epoch_base;
static int64_t time_uptime_at_set_ms;

static uint32_t get_utc_time(void)
{
    if (time_epoch_base == 0) {
        return 0;
    }
    return time_epoch_base + (uint32_t) ((k_uptime_get() - time_uptime_at_set_ms) / 1000);
}

//
// Forward declarations
//
static void audio_ccc_config_changed_handler(const struct bt_gatt_attr *attr, uint16_t value);
static ssize_t audio_data_read_characteristic(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr,
                                              void *buf,
                                              uint16_t len,
                                              uint16_t offset);
static ssize_t audio_codec_read_characteristic(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               void *buf,
                                               uint16_t len,
                                               uint16_t offset);
static ssize_t settings_dim_ratio_write_handler(struct bt_conn *conn,
                                                const struct bt_gatt_attr *attr,
                                                const void *buf,
                                                uint16_t len,
                                                uint16_t offset,
                                                uint8_t flags);
static ssize_t settings_dim_ratio_read_handler(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               void *buf,
                                               uint16_t len,
                                               uint16_t offset);
static ssize_t settings_mic_gain_write_handler(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               const void *buf,
                                               uint16_t len,
                                               uint16_t offset,
                                               uint8_t flags);
static ssize_t settings_mic_gain_read_handler(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr,
                                              void *buf,
                                              uint16_t len,
                                              uint16_t offset);
static void charging_status_ccc_config_changed_handler(const struct bt_gatt_attr *attr, uint16_t value);
static ssize_t settings_charging_status_read_handler(struct bt_conn *conn,
                                                     const struct bt_gatt_attr *attr,
                                                     void *buf,
                                                     uint16_t len,
                                                     uint16_t offset);
static int notify_charging_status(struct bt_conn *conn, bool force_notify);
static ssize_t
features_read_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset);

static void update_phy(struct bt_conn *conn);
static void update_data_length(struct bt_conn *conn);
static void update_mtu(struct bt_conn *conn);
static void update_conn_params(struct bt_conn *conn);
static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params);

static struct bt_gatt_exchange_params exchange_params;

//
// --- Audio service (19B10000) ---
//
static struct bt_uuid_128 audio_service_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10000, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 audio_characteristic_data_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10001, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 audio_characteristic_format_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10002, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

static struct bt_gatt_attr audio_service_attr[] = {
    BT_GATT_PRIMARY_SERVICE(&audio_service_uuid),
    BT_GATT_CHARACTERISTIC(&audio_characteristic_data_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           audio_data_read_characteristic,
                           NULL,
                           NULL),
    BT_GATT_CCC(audio_ccc_config_changed_handler, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&audio_characteristic_format_uuid.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           audio_codec_read_characteristic,
                           NULL,
                           NULL),
};
static struct bt_gatt_service audio_service = BT_GATT_SERVICE(audio_service_attr);

//
// --- Settings service (19B10010) ---
//
static struct bt_uuid_128 settings_service_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10010, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 settings_dim_ratio_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10011, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 settings_mic_gain_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10012, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 settings_charging_status_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10013, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

static struct bt_gatt_attr settings_service_attr[] = {
    BT_GATT_PRIMARY_SERVICE(&settings_service_uuid),
    BT_GATT_CHARACTERISTIC(&settings_dim_ratio_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           settings_dim_ratio_read_handler,
                           settings_dim_ratio_write_handler,
                           NULL),
    BT_GATT_CHARACTERISTIC(&settings_mic_gain_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           settings_mic_gain_read_handler,
                           settings_mic_gain_write_handler,
                           NULL),
    BT_GATT_CHARACTERISTIC(&settings_charging_status_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ,
                           settings_charging_status_read_handler,
                           NULL,
                           NULL),
    BT_GATT_CCC(charging_status_ccc_config_changed_handler, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};
static struct bt_gatt_service settings_service = BT_GATT_SERVICE(settings_service_attr);
/* Index of the charging-status CCC attribute (used for notify). */
#define SETTINGS_CHARGING_CCC_ATTR_IDX 6

//
// --- Features service (19B10020) ---
//
static struct bt_uuid_128 features_service_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10020, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 features_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10021, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

static struct bt_gatt_attr features_service_attr[] = {
    BT_GATT_PRIMARY_SERVICE(&features_service_uuid),
    BT_GATT_CHARACTERISTIC(&features_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           features_read_handler,
                           NULL,
                           NULL),
};
static struct bt_gatt_service features_service = BT_GATT_SERVICE(features_service_attr);

//
// --- Time Sync service (19B10030) ---
//
static struct bt_uuid_128 time_sync_service_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10030, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 time_sync_write_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10031, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));
static struct bt_uuid_128 time_sync_read_characteristic_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x19B10032, 0xE8F2, 0x537E, 0x4F6C, 0xD104768A1214));

static ssize_t time_sync_write_handler(struct bt_conn *conn,
                                       const struct bt_gatt_attr *attr,
                                       const void *buf,
                                       uint16_t len,
                                       uint16_t offset,
                                       uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (len != sizeof(uint32_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint32_t epoch_s;
    memcpy(&epoch_s, buf, sizeof(epoch_s));
    time_epoch_base = epoch_s;
    time_uptime_at_set_ms = k_uptime_get();
    LOG_INF("Time synchronized: %u", epoch_s);
    return len;
}

static ssize_t
time_sync_read_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    uint32_t epoch_s = get_utc_time();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &epoch_s, sizeof(epoch_s));
}

static struct bt_gatt_attr time_sync_service_attr[] = {
    BT_GATT_PRIMARY_SERVICE(&time_sync_service_uuid),
    BT_GATT_CHARACTERISTIC(&time_sync_write_characteristic_uuid.uuid,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL,
                           time_sync_write_handler,
                           NULL),
    BT_GATT_CHARACTERISTIC(&time_sync_read_characteristic_uuid.uuid,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           time_sync_read_handler,
                           NULL,
                           NULL),
};
static struct bt_gatt_service time_sync_service = BT_GATT_SERVICE(time_sync_service_attr);

//
// --- Advertising ---
//
static const struct bt_data bt_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_UUID128_ALL, audio_service_uuid.val, sizeof(audio_service_uuid.val)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static const struct bt_data bt_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
};

/* (Re)start connectable advertising. BT_LE_ADV_CONN was removed in Zephyr 4.4;
 * FAST_1 is the recommended connectable-advertising parameter set. Safe to call
 * from the disconnected callback to resume advertising after a peer drops. */
static void start_advertising(void)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, bt_ad, ARRAY_SIZE(bt_ad), bt_sd, ARRAY_SIZE(bt_sd));
    if (err == -EALREADY) {
        return;
    }
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
    } else {
        LOG_INF("Advertising started");
    }
}

//
// --- Characteristic handlers ---
//
static void audio_ccc_config_changed_handler(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    LOG_INF("Audio CCC: %s", value == BT_GATT_CCC_NOTIFY ? "subscribed" : "unsubscribed");
}

static void charging_status_ccc_config_changed_handler(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    if (value == BT_GATT_CCC_NOTIFY && current_connection != NULL) {
        (void) notify_charging_status(current_connection, true);
    }
}

static ssize_t audio_data_read_characteristic(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr,
                                              void *buf,
                                              uint16_t len,
                                              uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t audio_codec_read_characteristic(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               void *buf,
                                               uint16_t len,
                                               uint16_t offset)
{
    uint8_t value[1] = {CODEC_ID};
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(value));
}

static ssize_t settings_dim_ratio_write_handler(struct bt_conn *conn,
                                                const struct bt_gatt_attr *attr,
                                                const void *buf,
                                                uint16_t len,
                                                uint16_t offset,
                                                uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t new_ratio = ((const uint8_t *) buf)[0];
    if (new_ratio > 100) {
        new_ratio = 100;
    }
    (void) app_settings_save_dim_ratio(new_ratio);
    return len;
}

static ssize_t settings_dim_ratio_read_handler(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               void *buf,
                                               uint16_t len,
                                               uint16_t offset)
{
    uint8_t current_ratio = app_settings_get_dim_ratio();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &current_ratio, sizeof(current_ratio));
}

static ssize_t settings_mic_gain_write_handler(struct bt_conn *conn,
                                               const struct bt_gatt_attr *attr,
                                               const void *buf,
                                               uint16_t len,
                                               uint16_t offset,
                                               uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);
    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint8_t new_gain = ((const uint8_t *) buf)[0];
    if (new_gain > 8) {
        new_gain = 8;
    }
    (void) app_settings_save_mic_gain(new_gain);
#ifdef CONFIG_OMI_MIC
    mic_set_gain(new_gain);
#endif
    return len;
}

static ssize_t settings_mic_gain_read_handler(struct bt_conn *conn,
                                              const struct bt_gatt_attr *attr,
                                              void *buf,
                                              uint16_t len,
                                              uint16_t offset)
{
    uint8_t current_gain = app_settings_get_mic_gain();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &current_gain, sizeof(current_gain));
}

static ssize_t settings_charging_status_read_handler(struct bt_conn *conn,
                                                     const struct bt_gatt_attr *attr,
                                                     void *buf,
                                                     uint16_t len,
                                                     uint16_t offset)
{
#ifdef CONFIG_OMI_BATTERY
    uint8_t charging_status = is_charging ? 1U : 0U;
#else
    uint8_t charging_status = 0U;
#endif
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &charging_status, sizeof(charging_status));
}

static ssize_t
features_read_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    uint32_t features = 0;
#ifdef CONFIG_OMI_BUTTON
    features |= OMI_FEATURE_BUTTON;
#endif
#ifdef CONFIG_OMI_BATTERY
    features |= OMI_FEATURE_BATTERY;
#endif
    features |= OMI_FEATURE_LED_DIMMING;
    features |= OMI_FEATURE_MIC_GAIN;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &features, sizeof(features));
}

static void exchange_func(struct bt_conn *conn, uint8_t att_err, struct bt_gatt_exchange_params *params)
{
    ARG_UNUSED(params);
    if (att_err) {
        LOG_ERR("MTU exchange failed (err %u)", att_err);
    } else {
        current_mtu = bt_gatt_get_mtu(conn);
        LOG_INF("MTU exchange successful. New MTU: %u", current_mtu);
    }
}

//
// --- Battery ---
//
#ifdef CONFIG_OMI_BATTERY
#define BATTERY_REFRESH_INTERVAL_CONNECTED 5000
#define BATTERY_REFRESH_INTERVAL_DISCONNECTED 10000
#define BATTERY_CRITICAL_MV 3300
uint8_t battery_percentage = 0;
static int8_t charging_status_last_notified = -1;
void broadcast_battery_level(struct k_work *work_item);

static int notify_charging_status(struct bt_conn *conn, bool force_notify)
{
    if (conn == NULL) {
        return -ENOTCONN;
    }
    if (!bt_gatt_is_subscribed(conn, &settings_service.attrs[SETTINGS_CHARGING_CCC_ATTR_IDX], BT_GATT_CCC_NOTIFY)) {
        return 0;
    }
    uint8_t charging_status = is_charging ? 1U : 0U;
    if (!force_notify && charging_status_last_notified == (int8_t) charging_status) {
        return 0;
    }
    int err =
        bt_gatt_notify(conn, &settings_service.attrs[SETTINGS_CHARGING_CCC_ATTR_IDX], &charging_status, sizeof(charging_status));
    if (err) {
        return err;
    }
    charging_status_last_notified = (int8_t) charging_status;
    return 0;
}

K_WORK_DELAYABLE_DEFINE(battery_work, broadcast_battery_level);

void broadcast_battery_level(struct k_work *work_item)
{
    ARG_UNUSED(work_item);
    uint16_t battery_millivolt;
    uint32_t next = (is_connected && current_connection != NULL) ? BATTERY_REFRESH_INTERVAL_CONNECTED
                                                                 : BATTERY_REFRESH_INTERVAL_DISCONNECTED;

    if (battery_get_millivolt(&battery_millivolt) == 0 &&
        battery_get_percentage(&battery_percentage, battery_millivolt) == 0) {
        LOG_INF("Battery at %d mV (capacity %d%%)", battery_millivolt, battery_percentage);
        if (is_connected && current_connection != NULL) {
            (void) notify_charging_status(current_connection, false);
            (void) bt_bas_set_battery_level(battery_percentage);
        }
    } else {
        LOG_ERR("Failed to read battery level");
    }
    k_work_reschedule(&battery_work, K_MSEC(next));
}
#else
static int notify_charging_status(struct bt_conn *conn, bool force_notify)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(force_notify);
    return 0;
}
#endif

//
// --- Connection callbacks ---
//
static void _transport_connected(struct bt_conn *conn, uint8_t err)
{
    struct bt_conn_info info = {0};
    if (bt_conn_get_info(conn, &info)) {
        return;
    }

    LOG_INF("Transport connected");
    current_connection = bt_conn_ref(conn);
    current_mtu = bt_gatt_get_mtu(conn);

    update_conn_params(current_connection);
    k_sleep(K_MSEC(300));
    update_phy(current_connection);
    k_sleep(K_MSEC(1000));
    update_data_length(current_connection);
    update_mtu(current_connection);

    is_connected = true;
}

// TX slots reserved for non-audio notifications (battery/status).
#define AUDIO_TX_RESERVED_SLOTS 2
K_SEM_DEFINE(audio_tx_sem,
             CONFIG_BT_CONN_TX_MAX - AUDIO_TX_RESERVED_SLOTS,
             CONFIG_BT_CONN_TX_MAX - AUDIO_TX_RESERVED_SLOTS);

static void _transport_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(reason);
    is_connected = false;
    atomic_set(&conn_lowpower, 0);
    LOG_INF("Transport disconnected");

    if (current_connection != NULL) {
        bt_conn_unref(current_connection);
        current_connection = NULL;
    }
    current_mtu = 0;
#ifdef CONFIG_OMI_BATTERY
    charging_status_last_notified = -1;
#endif
    k_sem_init(&audio_tx_sem,
               CONFIG_BT_CONN_TX_MAX - AUDIO_TX_RESERVED_SLOTS,
               CONFIG_BT_CONN_TX_MAX - AUDIO_TX_RESERVED_SLOTS);

    /* Resume advertising so the device is discoverable again after a peer
     * disconnects (previously it went silent after the first connection). */
    start_advertising();
}

static bool _le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(param);
    return true;
}

static void _le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    LOG_INF("Conn params updated: interval %u units, latency %u, timeout %u", interval, latency, timeout);
    if (interval > 24 && !atomic_get(&conn_lowpower)) {
        update_conn_params(conn);
    }
}

static void _le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
    ARG_UNUSED(conn);
    LOG_INF("PHY updated: TX %u, RX %u", param->tx_phy, param->rx_phy);
}

static void _le_data_length_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
    ARG_UNUSED(conn);
    LOG_INF("Data length updated: TX %u/%u us, RX %u/%u us",
            info->tx_max_len,
            info->tx_max_time,
            info->rx_max_len,
            info->rx_max_time);
}

static struct bt_conn_cb _callback_references = {
    .connected = _transport_connected,
    .disconnected = _transport_disconnected,
    .le_param_req = _le_param_req,
    .le_param_updated = _le_param_updated,
    .le_phy_updated = _le_phy_updated,
    .le_data_len_updated = _le_data_length_updated,
};

//
// --- Update request functions ---
//
#define PHY_UPDATE_RETRY_COUNT 3
#define PHY_UPDATE_RETRY_DELAY_MS 150
#define MTU_UPDATE_RETRY_COUNT 3
#define MTU_UPDATE_RETRY_DELAY_MS 120
#define CONN_PARAM_RETRY_COUNT 3
#define CONN_PARAM_RETRY_DELAY_MS 300

static void update_conn_params(struct bt_conn *conn)
{
    const struct bt_le_conn_param preferred = {.interval_min = 6, .interval_max = 12, .latency = 0, .timeout = 400};
    for (int attempt = 1; attempt <= CONN_PARAM_RETRY_COUNT; attempt++) {
        int err = bt_conn_le_param_update(conn, &preferred);
        if (!err) {
            return;
        }
        if (attempt < CONN_PARAM_RETRY_COUNT) {
            k_sleep(K_MSEC(CONN_PARAM_RETRY_DELAY_MS));
        }
    }
    LOG_WRN("conn param update failed after retries");
}

void transport_conn_set_lowpower(bool low)
{
    atomic_set(&conn_lowpower, low ? 1 : 0);
    struct bt_conn *conn = current_connection;
    if (conn == NULL) {
        return;
    }
    const struct bt_le_conn_param slow = {.interval_min = 32, .interval_max = 40, .latency = 2, .timeout = 400};
    const struct bt_le_conn_param fast = {.interval_min = 6, .interval_max = 12, .latency = 0, .timeout = 400};
    int err = bt_conn_le_param_update(conn, low ? &slow : &fast);
    if (err) {
        LOG_WRN("conn param %s update failed (err %d)", low ? "slow" : "fast", err);
    }
}

static void update_phy(struct bt_conn *conn)
{
    const struct bt_conn_le_phy_param preferred = {
        .options = BT_CONN_LE_PHY_OPT_NONE,
        .pref_rx_phy = BT_GAP_LE_PHY_2M,
        .pref_tx_phy = BT_GAP_LE_PHY_2M,
    };
    for (int attempt = 1; attempt <= PHY_UPDATE_RETRY_COUNT; attempt++) {
        int err = bt_conn_le_phy_update(conn, &preferred);
        if (!err) {
            return;
        }
        if (attempt < PHY_UPDATE_RETRY_COUNT) {
            k_sleep(K_MSEC(PHY_UPDATE_RETRY_DELAY_MS));
        }
    }
    LOG_ERR("PHY update failed after retries");
}

static void update_data_length(struct bt_conn *conn)
{
    struct bt_conn_le_data_len_param param = {
        .tx_max_len = BT_GAP_DATA_LEN_MAX,
        .tx_max_time = BT_GAP_DATA_TIME_MAX,
    };
    int err = bt_conn_le_data_len_update(conn, &param);
    if (err) {
        LOG_ERR("data len update failed (err %d)", err);
    }
}

static void update_mtu(struct bt_conn *conn)
{
    exchange_params.func = exchange_func;
    for (int attempt = 1; attempt <= MTU_UPDATE_RETRY_COUNT; attempt++) {
        int err = bt_gatt_exchange_mtu(conn, &exchange_params);
        if (!err) {
            return;
        }
        if (err == -EALREADY) {
            current_mtu = bt_gatt_get_mtu(conn);
            return;
        }
        if ((err == -EBUSY || err == -EAGAIN) && attempt < MTU_UPDATE_RETRY_COUNT) {
            k_sleep(K_MSEC(MTU_UPDATE_RETRY_DELAY_MS));
            continue;
        }
        LOG_ERR("MTU exchange failed (err %d)", err);
        return;
    }
}

//
// --- Ring buffer + pusher ---
//
#define NET_BUFFER_HEADER_SIZE 3
#define RING_BUFFER_HEADER_SIZE 2
static uint8_t tx_queue[NETWORK_RING_BUF_SIZE * (CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE)];
static uint8_t tx_buffer[CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE];
static uint8_t tx_buffer_2[CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE];
static uint32_t tx_buffer_size = 0;
static struct ring_buf ring_buf;
K_SEM_DEFINE(tx_queue_sem, 0, NETWORK_RING_BUF_SIZE);

static bool write_to_tx_queue(uint8_t *data, size_t size)
{
    if (size > CODEC_OUTPUT_MAX_BYTES) {
        return false;
    }
    tx_buffer_2[0] = size & 0xFF;
    tx_buffer_2[1] = (size >> 8) & 0xFF;
    memcpy(tx_buffer_2 + RING_BUFFER_HEADER_SIZE, data, size);

    int written = ring_buf_put(&ring_buf, tx_buffer_2, (CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE));
    if (written != CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE) {
        return false;
    }
    k_sem_give(&tx_queue_sem);
    return true;
}

static bool read_from_tx_queue(void)
{
    uint32_t package_size = CODEC_OUTPUT_MAX_BYTES + RING_BUFFER_HEADER_SIZE;
    tx_buffer_size = ring_buf_get(&ring_buf, tx_buffer, package_size);
    if (tx_buffer_size != package_size) {
        return false;
    }
    tx_buffer_size = tx_buffer[0] + (tx_buffer[1] << 8);
    return true;
}

static void on_audio_tx_done(struct bt_conn *conn, void *user_data)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(user_data);
    k_sem_give(&audio_tx_sem);
}

K_THREAD_STACK_DEFINE(pusher_stack, 4096);
static struct k_thread pusher_thread;
static uint16_t packet_next_index = 0;

#define MAX_POSSIBLE_MTU 517
static uint8_t pusher_temp_data[MAX_POSSIBLE_MTU];

static bool push_to_gatt(struct bt_conn *conn)
{
    uint8_t *buffer = tx_buffer + RING_BUFFER_HEADER_SIZE;
    uint32_t offset = 0;
    uint8_t index = 0;
    const int max_retries = 3;

    while (offset < tx_buffer_size) {
        uint32_t packet_size = MIN(current_mtu - NET_BUFFER_HEADER_SIZE, tx_buffer_size - offset);

        k_sem_take(&audio_tx_sem, K_FOREVER);

        uint32_t id = packet_next_index++;
        pusher_temp_data[0] = id & 0xFF;
        pusher_temp_data[1] = (id >> 8) & 0xFF;
        pusher_temp_data[2] = index;
        memcpy(pusher_temp_data + NET_BUFFER_HEADER_SIZE, buffer + offset, packet_size);

        offset += packet_size;
        index++;

        int retry_count = 0;
        while (retry_count < max_retries) {
            struct bt_gatt_notify_params params = {
                .attr = &audio_service.attrs[1],
                .data = pusher_temp_data,
                .len = packet_size + NET_BUFFER_HEADER_SIZE,
                .func = on_audio_tx_done,
                .user_data = NULL,
            };
            int err = bt_gatt_notify_cb(conn, &params);
            if (err) {
                k_sleep(K_MSEC(1));
                retry_count++;
                continue;
            }
            break;
        }

        if (retry_count >= max_retries) {
            LOG_ERR("Failed to send packet after %d retries", max_retries);
            k_sem_give(&audio_tx_sem);
            return false;
        }
    }
    return true;
}

void pusher(void)
{
    k_msleep(500);
    while (!atomic_get(&pusher_stop_flag)) {
        k_sem_take(&tx_queue_sem, K_FOREVER);
        if (atomic_get(&pusher_stop_flag)) {
            break;
        }

        while (read_from_tx_queue()) {
            struct bt_conn *conn = current_connection;
            bool is_subscribed = false;
            if (conn) {
                conn = bt_conn_ref(conn);
                if (current_mtu >= MINIMAL_PACKET_SIZE) {
                    is_subscribed = bt_gatt_is_subscribed(conn, &audio_service.attrs[1], BT_GATT_CCC_NOTIFY);
                }
            }

            if (conn && is_subscribed) {
                push_to_gatt(conn);
                bt_conn_unref(conn);
            } else {
                if (conn) {
                    bt_conn_unref(conn);
                }
                /* No subscriber (no storage in this build): drop the frame. */
                k_sleep(K_MSEC(10));
            }
        }
    }
}

int transport_off(void)
{
    atomic_set(&pusher_stop_flag, 1);
    k_sem_give(&tx_queue_sem);
    int ret = k_thread_join(&pusher_thread, K_MSEC(500));
    if (ret != 0) {
        k_thread_abort(&pusher_thread);
    }

    if (current_connection != NULL) {
        bt_conn_disconnect(current_connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(current_connection);
        current_connection = NULL;
    }

    int err = bt_le_adv_stop();
    if (err) {
        LOG_ERR("adv stop failed %d", err);
    }
    err = bt_disable();
    if (err) {
        LOG_ERR("bt_disable failed %d", err);
    }
    is_connected = false;
    current_mtu = 0;
    return 0;
}

int transport_start(void)
{
    bt_conn_cb_register(&_callback_references);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("bt_enable failed (err %d)", err);
        return err;
    }
    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        settings_load();
    }

#ifdef CONFIG_OMI_BUTTON
    button_init();
    register_button_service();
    activate_button_work();
#endif

    bt_gatt_service_register(&audio_service);
    bt_gatt_service_register(&settings_service);
    bt_gatt_service_register(&features_service);
    bt_gatt_service_register(&time_sync_service);

    start_advertising();

#ifdef CONFIG_OMI_BATTERY
    if (battery_charge_start()) {
        LOG_ERR("Battery init failed");
    } else {
        LOG_INF("Battery initialized");
        k_work_schedule(&battery_work, K_MSEC(3000));
    }
#endif

    ring_buf_init(&ring_buf, sizeof(tx_queue), tx_queue);

    k_thread_create(&pusher_thread,
                    pusher_stack,
                    K_THREAD_STACK_SIZEOF(pusher_stack),
                    (k_thread_entry_t) pusher,
                    NULL,
                    NULL,
                    NULL,
                    K_PRIO_PREEMPT(7),
                    0,
                    K_NO_WAIT);
    LOG_INF("Pusher started");
    return 0;
}

struct bt_conn *get_current_connection(void)
{
    return current_connection;
}

int broadcast_audio_packets(uint8_t *buffer, size_t size)
{
    if (!write_to_tx_queue(buffer, size)) {
        return -1;
    }
    return 0;
}
