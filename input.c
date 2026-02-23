/*
 * wizard-cli – input.c
 * Non-blocking keyboard input using raw termios mode.
 */

#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include "input.h"

static struct termios g_saved_tio;
static int            g_tio_saved = 0;

/* ── input_init ───────────────────────────────────────────────────── */
void input_init(void)
{
    if (tcgetattr(STDIN_FILENO, &g_saved_tio) == 0) {
        g_tio_saved = 1;
        struct termios raw = g_saved_tio;
        raw.c_lflag    &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 1;    /* block until 1 char available */
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
}

/* ── input_cleanup ────────────────────────────────────────────────── */
void input_cleanup(void)
{
    if (g_tio_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tio);
}

/* ── Clamp helper ─────────────────────────────────────────────────── */
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── input_thread ─────────────────────────────────────────────────── */
void *input_thread(void *arg)
{
    input_state_t *inp   = (input_state_t *)arg;
    app_state_t   *state = inp->state;

    unsigned char c;
    while (state->running) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        pthread_mutex_lock(&state->lock);

        switch (c) {

        /* ── Quit ────────────────────────────────────────────────── */
        case 'q': case 'Q':
            state->running = 0;
            break;

        /* ── Volume ──────────────────────────────────────────────── */
        case '+': case '=':
            state->volume = clampf(state->volume + 0.05f, 0.0f, 2.0f);
            break;
        case '-':
            state->volume = clampf(state->volume - 0.05f, 0.0f, 2.0f);
            break;

        /* ── LFO rate (upper = increase, lower = decrease) ──────── */
        case 'L':
            state->lfo_rate = clampf(state->lfo_rate + 0.1f, 0.1f, 5.0f);
            break;
        case 'l':
            state->lfo_rate = clampf(state->lfo_rate - 0.1f, 0.1f, 5.0f);
            break;

        /* ── LFO depth ───────────────────────────────────────────── */
        case 'D':
            state->lfo_depth = clampf(state->lfo_depth + 0.05f, 0.0f, 1.0f);
            break;
        case 'd':
            state->lfo_depth = clampf(state->lfo_depth - 0.05f, 0.0f, 1.0f);
            break;

        /* ── Reverb mix ──────────────────────────────────────────── */
        case 'R':
            state->reverb_mix = clampf(state->reverb_mix + 0.05f, 0.0f, 0.90f);
            break;
        case 'r':
            state->reverb_mix = clampf(state->reverb_mix - 0.05f, 0.0f, 0.90f);
            break;

        default:
            break;
        }

        pthread_mutex_unlock(&state->lock);
    }

    return NULL;
}
