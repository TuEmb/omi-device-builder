/*
 * Analog (ADC-sampled) microphone backend — SKELETON.
 *
 * For boards whose mic is an analog MEMS part (e.g. the XIAO MG24 Sense's
 * MSM381ACT001, DATA=PC9 / PWR=PC8) rather than a digital PDM mic. A real
 * implementation samples an ADC channel at MIC_SAMPLE_RATE and feeds fixed
 * MIC_FRAME_SAMPLES blocks to the registered callback (same contract as
 * mic_pdm.c). Selected via CONFIG_OMI_MIC_BACKEND_ADC.
 *
 * TODO(contributor): implement ADC-timer sampling + DC removal + gain.
 */

#include "mic.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config.h"

LOG_MODULE_REGISTER(mic, CONFIG_LOG_DEFAULT_LEVEL);

static mix_handler _callback = NULL;
static uint8_t _gain = 4;

void set_mic_callback(mix_handler callback)
{
    _callback = callback;
}

void mic_set_gain(uint8_t gain_level)
{
    _gain = gain_level;
}

bool mic_is_running(void)
{
    return false;
}

int mic_start(void)
{
    ARG_UNUSED(_callback);
    ARG_UNUSED(_gain);
    LOG_ERR("Analog ADC mic backend not implemented yet");
    return -ENOTSUP;
}
