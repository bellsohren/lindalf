#ifndef SHARED_H
#define SHARED_H

/*
 * wizard-cli – shared application state
 * All threads access this structure via the embedded mutex.
 */

#include <pthread.h>
#include <stdint.h>

#define SAMPLE_RATE     44100
#define APP_NAME        "wizard-cli"
#define APP_VERSION     "1.0.0"

typedef struct {
    volatile int    running;       /* set to 0 to stop all threads    */
    pthread_mutex_t lock;

    /* ── DSP parameters (guarded by lock) ─────────────────────────── */
    float  volume;       /* 0.0 – 2.0,  default 1.0                   */
    float  lfo_rate;     /* Hz   0.1 – 5.0, default 0.5               */
    float  lfo_depth;    /* 0.0 – 1.0,  default 0.0                   */
    float  reverb_mix;   /* 0.0 – 0.90, default 0.0                   */

    /* ── HUD display values (written by audio, read by animation) ─── */
    char   hud_line[128];
} app_state_t;

static inline void app_state_init(app_state_t *s)
{
    s->running    = 1;
    pthread_mutex_init(&s->lock, NULL);
    s->volume     = 1.0f;
    s->lfo_rate   = 0.5f;
    s->lfo_depth  = 0.0f;
    s->reverb_mix = 0.0f;
    s->hud_line[0] = '\0';
}

static inline void app_state_destroy(app_state_t *s)
{
    pthread_mutex_destroy(&s->lock);
}

#endif /* SHARED_H */
