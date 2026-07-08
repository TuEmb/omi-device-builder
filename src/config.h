#ifndef CONFIG_H
#define CONFIG_H

// Audio / ring-buffer sizing (kept identical to the omi firmware so the codec
// and the BLE audio packet framing stay wire-compatible with the Omi app).
#define AUDIO_BUFFER_SAMPLES 16000 // 1s @ 16 kHz
#define NETWORK_RING_BUF_SIZE 32   // number of frames * CODEC_OUTPUT_MAX_BYTES
#define MINIMAL_PACKET_SIZE 100    // below this it's not worth sending anything

// Microphone
#define MIC_SAMPLE_RATE 16000
#define MIC_CHANNELS 1
// Samples per captured PCM frame handed to the mic callback (100 ms @ 16 kHz).
// The mic callback signature carries no length, so mic.c and the consumer
// (main.c) must agree on this constant.
#define MIC_FRAME_SAMPLES (MIC_SAMPLE_RATE / 10)

// Codec
#ifdef CONFIG_OMI_CODEC_OPUS
#define CODEC_OPUS 1
#else
#error "Enable CONFIG_OMI_CODEC_OPUS in the project .conf file"
#endif

#if CODEC_OPUS
#define CODEC_PACKAGE_SAMPLES (160 * 2)
#define CODEC_OUTPUT_MAX_BYTES (CODEC_PACKAGE_SAMPLES / 2)
#define CODEC_OPUS_APPLICATION OPUS_APPLICATION_RESTRICTED_LOWDELAY
#define CODEC_OPUS_BITRATE 32000
#define CODEC_OPUS_VBR 1
#define CODEC_OPUS_COMPLEXITY 3
#endif
#define CONFIG_OPUS_MODE CONFIG_OPUS_MODE_CELT

// Codec ID advertised on the audio-format characteristic (21 = Opus).
#ifdef CODEC_OPUS
#define CODEC_ID 21
#endif

#endif // CONFIG_H
