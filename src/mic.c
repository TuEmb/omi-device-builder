#include "mic.h"

#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config.h"

LOG_MODULE_REGISTER(mic, CONFIG_LOG_DEFAULT_LEVEL);

/* Standard Zephyr DMIC (nRF PDM) device. Each board overlay must provide a
 * `dmic-dev` alias pointing at its PDM peripheral (with the mic pins wired via
 * pinctrl and status = "okay"). */
static const struct device *const dmic_dev = DEVICE_DT_GET(DT_ALIAS(dmic_dev));

/* 100 ms blocks @ 16 kHz mono, 16-bit = 1600 samples = 3200 bytes. */
#define MIC_BLOCK_SAMPLES MIC_FRAME_SAMPLES
#define MIC_BLOCK_BYTES (MIC_BLOCK_SAMPLES * sizeof(int16_t))
#define MIC_BLOCK_COUNT 8

K_MEM_SLAB_DEFINE_STATIC(mic_mem_slab, MIC_BLOCK_BYTES, MIC_BLOCK_COUNT, 4);

static mix_handler _callback = NULL;
static volatile bool _running = false;
static uint8_t _gain = 4; /* neutral (see mic_set_gain) */

K_THREAD_STACK_DEFINE(mic_stack, 2048);
static struct k_thread mic_thread;

void set_mic_callback(mix_handler callback)
{
    _callback = callback;
}

void mic_set_gain(uint8_t gain_level)
{
    if (gain_level > 8) {
        gain_level = 8;
    }
    _gain = gain_level;
    LOG_INF("Mic gain level set to %u", gain_level);
}

bool mic_is_running(void)
{
    return _running;
}

/* Apply software gain around the neutral level 4 (=1x). Each step is ~ +/-25%.
 * Kept simple and saturating; hardware PDM has no analog gain we can retune. */
static void apply_gain(int16_t *samples, size_t count)
{
    if (_gain == 4) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        int32_t v = (int32_t) samples[i] * (int32_t) _gain / 4;
        if (v > INT16_MAX) {
            v = INT16_MAX;
        } else if (v < INT16_MIN) {
            v = INT16_MIN;
        }
        samples[i] = (int16_t) v;
    }
}

static void mic_entry(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (_running) {
        void *buffer;
        uint32_t size;
        int ret = dmic_read(dmic_dev, 0, &buffer, &size, 1000);
        if (ret < 0) {
            LOG_ERR("dmic_read failed: %d", ret);
            continue;
        }

        int16_t *pcm = (int16_t *) buffer;
        size_t count = size / sizeof(int16_t);
        apply_gain(pcm, count);

        if (_callback) {
            /* omi's mix_handler passes a pointer; the codec path consumes a
             * fixed MIC_BLOCK; keep parity with the original contract. */
            _callback(pcm);
        }

        k_mem_slab_free(&mic_mem_slab, buffer);
    }
}

int mic_start(void)
{
    if (!device_is_ready(dmic_dev)) {
        LOG_ERR("DMIC device not ready");
        return -ENODEV;
    }

    struct pcm_stream_cfg stream = {
        .pcm_width = 16,
        .mem_slab = &mic_mem_slab,
    };

    struct dmic_cfg cfg = {
        .io =
            {
                /* Typical PDM mic timing; adjust per mic datasheet if needed. */
                .min_pdm_clk_freq = 1000000,
                .max_pdm_clk_freq = 3500000,
                .min_pdm_clk_dc = 40,
                .max_pdm_clk_dc = 60,
            },
        .streams = &stream,
        .channel =
            {
                .req_num_streams = 1,
                .req_num_chan = MIC_CHANNELS,
            },
    };

    cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
    stream.pcm_rate = MIC_SAMPLE_RATE;
    stream.block_size = MIC_BLOCK_BYTES;

    int ret = dmic_configure(dmic_dev, &cfg);
    if (ret < 0) {
        LOG_ERR("dmic_configure failed: %d", ret);
        return ret;
    }

    ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
    if (ret < 0) {
        LOG_ERR("dmic_trigger START failed: %d", ret);
        return ret;
    }

    _running = true;
    k_thread_create(&mic_thread,
                    mic_stack,
                    K_THREAD_STACK_SIZEOF(mic_stack),
                    mic_entry,
                    NULL,
                    NULL,
                    NULL,
                    K_PRIO_PREEMPT(5),
                    0,
                    K_NO_WAIT);
    k_thread_name_set(&mic_thread, "mic");

    LOG_INF("PDM mic started (%d Hz, %d ch)", MIC_SAMPLE_RATE, MIC_CHANNELS);
    return 0;
}
