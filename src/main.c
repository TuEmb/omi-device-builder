#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config.h"
#include "led.h"
#include "settings.h"
#include "transport.h"
#ifdef CONFIG_OMI_MIC
#include "codec.h"
#include "mic.h"
#endif
#ifdef CONFIG_OMI_BATTERY
#include "battery.h"
#endif
#ifdef CONFIG_OMI_STORAGE
#include "storage.h"
#endif

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Shared state referenced by transport.c. */
bool is_connected = false;
bool is_charging = false;

#ifdef CONFIG_OMI_BATTERY
extern uint8_t battery_percentage;
#endif

#ifdef CONFIG_OMI_MIC
/* Opus-encoded frame ready -> queue for BLE notification. */
static void codec_handler(uint8_t *data, size_t len)
{
    (void) broadcast_audio_packets(data, len);
}

/* PCM frame from the mic -> feed the codec. Frame length is the agreed
 * MIC_FRAME_SAMPLES (the mix_handler carries no length). */
static void mic_handler(int16_t *buffer)
{
    int err = codec_receive_pcm(buffer, MIC_FRAME_SAMPLES);
    if (err) {
        LOG_ERR("codec_receive_pcm failed: %d", err);
    }
}
#endif

#ifdef CONFIG_OMI_LED
/* Simple status LED: solid blue when connected, blinking red while advertising. */
static void led_status_loop(void)
{
    bool blink = false;
    while (1) {
        if (is_connected) {
            set_led_red(false);
            set_led_blue(true);
        } else {
            set_led_blue(false);
            set_led_red(blink);
            blink = !blink;
        }
        k_msleep(500);
    }
}
K_THREAD_DEFINE(led_tid, 1024, led_status_loop, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, 0);
#endif

int main(void)
{
    LOG_INF("omi-device-builder starting (%s)", CONFIG_BT_DEVICE_NAME);

    app_settings_init();

#ifdef CONFIG_OMI_STORAGE
    if (storage_init()) {
        LOG_ERR("storage_init failed");
    }
#endif

#ifdef CONFIG_OMI_LED
    led_start();
#endif

#ifdef CONFIG_OMI_MIC
    /* Audio pipeline: mic -> codec -> transport. */
    set_codec_callback(codec_handler);
    if (codec_start()) {
        LOG_ERR("codec_start failed");
    }

    set_mic_callback(mic_handler);
    mic_set_gain(app_settings_get_mic_gain());
    if (mic_start()) {
        LOG_ERR("mic_start failed");
    }
#else
    LOG_WRN("Mic disabled for this board (no Zephyr driver yet) - BLE only, no audio");
#endif

    if (transport_start()) {
        LOG_ERR("transport_start failed");
    }

    LOG_INF("omi-device-builder ready");
    return 0;
}
