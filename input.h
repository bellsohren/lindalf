#ifndef INPUT_H
#define INPUT_H

/*
 * wizard-cli – input module
 *
 * Reads keystrokes in raw (non-blocking) terminal mode
 * and updates app_state_t accordingly.
 *
 * Key bindings:
 *   q       Quit
 *   + / =   Volume up
 *   -       Volume down
 *   l / L   LFO rate up / down
 *   d / D   LFO depth up / down
 *   r / R   Reverb mix up / down
 */

#include "shared.h"

typedef struct {
    app_state_t *state;
} input_state_t;

/* Save current terminal attributes and enter raw mode */
void input_init(void);

/* Restore original terminal attributes (call before exit) */
void input_cleanup(void);

/* Thread entry-point */
void *input_thread(void *arg);   /* arg: input_state_t* */

#endif /* INPUT_H */
