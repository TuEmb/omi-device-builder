#ifndef MIC_H
#define MIC_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*mix_handler)(int16_t *);

/**
 * @brief Start PDM microphone capture.
 *
 * Uses the Zephyr DMIC (nRF PDM) driver via the `dmic_dev` devicetree node.
 * Captured 16 kHz / 16-bit mono PCM is delivered to the callback registered
 * with set_mic_callback() (or forwarded to the codec if none is set).
 *
 * @return 0 on success, negative errno on error.
 */
int mic_start(void);

void set_mic_callback(mix_handler callback);

/** Software gain (0..8), applied to the PCM samples before encoding. */
void mic_set_gain(uint8_t gain_level);

bool mic_is_running(void);

#endif // MIC_H
