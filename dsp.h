#ifndef DSP_H
#define DSP_H

/*
 * wizard-cli – digital signal processing module
 *
 * Provides:
 *   • Volume scaling
 *   • LFO amplitude modulation
 *   • Simple feedback-delay reverb
 *
 * All processing is done in-place on interleaved stereo int16_t buffers.
 */

#include <stdint.h>
#include "shared.h"

/* ── Reverb delay buffer: 2 s × 2 channels ──────────────────────── */
#define REVERB_BUF_FRAMES  (SAMPLE_RATE * 2)

typedef struct {
    float  buf[REVERB_BUF_FRAMES * 2];   /* interleaved L/R float    */
    int    write_pos;                     /* current write head (frames) */
} reverb_t;

typedef struct {
    double phase;   /* current LFO phase in radians */
} lfo_t;

/* ── Init ─────────────────────────────────────────────────────────── */
void dsp_reverb_init(reverb_t *r);
void dsp_lfo_init(lfo_t *l);

/* ── Main processing entry-point ──────────────────────────────────── */
/*
 * Process `frames` interleaved stereo int16 samples in-place.
 * Reads volume/lfo_rate/lfo_depth/reverb_mix from `state` (thread-safe).
 */
void dsp_process(int16_t *samples, int frames, app_state_t *state,
                 reverb_t *rev, lfo_t *lfo);

#endif /* DSP_H */
