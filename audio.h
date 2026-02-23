#ifndef AUDIO_H
#define AUDIO_H

/*
 * wizard-cli – audio module
 *
 * WAV file parsing + ALSA playback with real-time DSP.
 * Runs in its own pthread.
 */

#include <stdio.h>
#include <stdint.h>
#ifndef NO_ALSA
#  include <alsa/asoundlib.h>
#endif
#include "shared.h"
#include "dsp.h"

typedef struct {
    app_state_t *state;

    /* ALSA handle */
#ifndef NO_ALSA
    snd_pcm_t  *pcm;
#else
    void       *pcm;
#endif
    int         pcm_ok;

    /* WAV file */
    FILE       *fp;
    long        data_offset;   /* byte offset of PCM data in file */
    uint32_t    data_size;     /* bytes of PCM data                */

    /* WAV format */
    uint32_t    file_sample_rate;
    uint16_t    file_channels;
    uint16_t    file_bits;

    /* DSP state (owned by this thread, no mutex needed) */
    reverb_t    reverb;
    lfo_t       lfo;
} audio_state_t;

/*
 * audio_init – open WAV, configure ALSA.
 * Returns 0 on success, -1 on error (error already printed to stderr).
 */
int  audio_init(audio_state_t *a, const char *path, app_state_t *state);

/* Thread entry-point (pthread_create compatible) */
void *audio_thread(void *arg);

/* Release all ALSA and file resources */
void audio_cleanup(audio_state_t *a);

#endif /* AUDIO_H */
