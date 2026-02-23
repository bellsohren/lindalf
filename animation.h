#ifndef ANIMATION_H
#define ANIMATION_H

/*
 * wizard-cli – animation module
 *
 * Renders a coloured ASCII wizard with:
 *   • Layered ANSI colours (hat, eyes, beard, robe, pipe)
 *   • Frame-based blinking / subtle motion (4 frames @ ~24 FPS)
 *   • Smoke particle system rising from the pipe
 *   • HUD overlay showing current DSP parameters
 *   • Pure ANSI escape-code rendering (no ncurses) – single large write()
 *     per frame to minimise flicker.
 *
 * The cursor is hidden during playback and restored on exit.
 */

#include "shared.h"

typedef struct {
    app_state_t *state;
} anim_state_t;

/* Init / cleanup (call from main thread) */
void anim_init(anim_state_t *a);
void anim_cleanup(anim_state_t *a);

/* Thread entry-point */
void *anim_thread(void *arg);   /* arg: anim_state_t* */

#endif /* ANIMATION_H */
