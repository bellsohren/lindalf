/*
 * wizard-cli – dsp.c
 * Real-time audio effects: volume, LFO modulation, feedback-delay reverb.
 */

#include <math.h>
#include <string.h>
#include "dsp.h"

/* ─────────────────────────────────────────────────────────────────── */
void dsp_reverb_init(reverb_t *r)
{
    memset(r->buf, 0, sizeof(r->buf));
    r->write_pos = 0;
}

void dsp_lfo_init(lfo_t *l)
{
    l->phase = 0.0;
}

/* ─────────────────────────────────────────────────────────────────── */
void dsp_process(int16_t *samples, int frames, app_state_t *state,
                 reverb_t *rev, lfo_t *lfo)
{
    /* snapshot parameters under lock to avoid per-sample locking */
    pthread_mutex_lock(&state->lock);
    float volume     = state->volume;
    float lfo_rate   = state->lfo_rate;
    float lfo_depth  = state->lfo_depth;
    float reverb_mix = state->reverb_mix;
    pthread_mutex_unlock(&state->lock);

    /* LFO phase increment per sample */
    double lfo_inc = 2.0 * M_PI * (double)lfo_rate / (double)SAMPLE_RATE;

    /* Reverb delay: map reverb_mix 0..1 to 0..0.5 s */
    int delay_frames = (int)(reverb_mix * (float)SAMPLE_RATE * 0.5f);
    if (delay_frames < 1)                delay_frames = 1;
    if (delay_frames >= REVERB_BUF_FRAMES) delay_frames = REVERB_BUF_FRAMES - 1;
    const float feedback = 0.45f;   /* feedback factor – kept below 0.5 for stability */

    for (int i = 0; i < frames; i++) {

        /* ── LFO ─────────────────────────────────────────────────── */
        float lfo_val = 1.0f + lfo_depth * sinf((float)lfo->phase);
        lfo->phase += lfo_inc;
        if (lfo->phase >= 2.0 * M_PI)
            lfo->phase -= 2.0 * M_PI;

        float gain = volume * lfo_val;

        /* ── Convert to float ────────────────────────────────────── */
        float l_in = (float)samples[i * 2]     / 32767.0f;
        float r_in = (float)samples[i * 2 + 1] / 32767.0f;

        /* ── Reverb read ─────────────────────────────────────────── */
        int read_pos = rev->write_pos - delay_frames;
        if (read_pos < 0)
            read_pos += REVERB_BUF_FRAMES;

        float del_l = rev->buf[read_pos * 2];
        float del_r = rev->buf[read_pos * 2 + 1];

        /* ── Mix: dry + reverb tail ──────────────────────────────── */
        float out_l = l_in * gain + del_l * reverb_mix;
        float out_r = r_in * gain + del_r * reverb_mix;

        /* ── Reverb write (feedback of output into delay line) ───── */
        rev->buf[rev->write_pos * 2]     = out_l * feedback;
        rev->buf[rev->write_pos * 2 + 1] = out_r * feedback;
        rev->write_pos = (rev->write_pos + 1) % REVERB_BUF_FRAMES;

        /* ── Hard clip & convert back to int16 ───────────────────── */
        if (out_l >  1.0f) out_l =  1.0f;
        if (out_l < -1.0f) out_l = -1.0f;
        if (out_r >  1.0f) out_r =  1.0f;
        if (out_r < -1.0f) out_r = -1.0f;

        samples[i * 2]     = (int16_t)(out_l * 32767.0f);
        samples[i * 2 + 1] = (int16_t)(out_r * 32767.0f);
    }
}
