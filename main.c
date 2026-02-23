/*
 * wizard-cli – main.c
 *
 * Entry point.  Parses arguments, sets up shared state, starts threads:
 *
 *   • animation_thread  – ANSI rendering @ 24 FPS
 *   • audio_thread      – ALSA WAV playback + real-time DSP
 *   • input_thread      – raw-mode keyboard → shared state
 *
 * Usage:
 *   wizard-cli [--music <file.wav>]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "shared.h"
#include "animation.h"
#include "audio.h"
#include "input.h"

/* ── Signal handling ──────────────────────────────────────────────── */
static app_state_t *g_state_ptr = NULL;

static void sig_handler(int sig)
{
    (void)sig;
    if (g_state_ptr) g_state_ptr->running = 0;
}

/* ── Usage ────────────────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [--music <file.wav>] [--help]\n"
        "\n"
        "Options:\n"
        "  --music <file.wav>   WAV file to play during the animation\n"
        "                       (16-bit PCM, mono or stereo)\n"
        "  --help               Show this help\n"
        "\n"
        "Controls (during playback):\n"
        "  q          Quit\n"
        "  + / -      Volume up / down\n"
        "  L / l      LFO rate up / down\n"
        "  D / d      LFO depth up / down\n"
        "  R / r      Reverb mix up / down\n",
        prog);
}

/* ── main ─────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *music_path = NULL;
    int         have_audio = 0;

    /* ── Parse arguments ─────────────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--music") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --music requires a filename argument\n");
                return EXIT_FAILURE;
            }
            music_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h")     == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* ── Shared state ────────────────────────────────────────────── */
    app_state_t state;
    app_state_init(&state);
    g_state_ptr = &state;

    /* ── Signal handlers ─────────────────────────────────────────── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    /* ── Input init (terminal raw mode) ─────────────────────────── */
    input_init();

    /* ── Animation init (hides cursor, clears screen) ────────────── */
    anim_state_t  anim  = { .state = &state };
    anim_init(&anim);

    /* ── Audio init (optional) ───────────────────────────────────── */
    audio_state_t audio;
    memset(&audio, 0, sizeof(audio));
    if (music_path) {
        if (audio_init(&audio, music_path, &state) == 0) {
            have_audio = 1;
        } else {
            fprintf(stderr, "Warning: audio disabled – animation continues\n");
        }
    }

    /* ── Launch threads ──────────────────────────────────────────── */
    pthread_t thr_anim, thr_audio, thr_input;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if (pthread_create(&thr_anim, &attr, anim_thread, &anim) != 0) {
        perror("pthread_create animation");
        goto cleanup;
    }

    if (have_audio) {
        if (pthread_create(&thr_audio, &attr, audio_thread, &audio) != 0) {
            perror("pthread_create audio");
            have_audio = 0;
        }
    }

    input_state_t inp = { .state = &state };
    if (pthread_create(&thr_input, &attr, input_thread, &inp) != 0) {
        perror("pthread_create input");
        state.running = 0;
    }

    pthread_attr_destroy(&attr);

    /* ── Wait for all threads ────────────────────────────────────── */
    if (state.running)
        pthread_join(thr_input, NULL);   /* blocks until 'q' or signal */

    /* Signal remaining threads to stop */
    state.running = 0;

    pthread_join(thr_anim, NULL);

    if (have_audio) {
        pthread_join(thr_audio, NULL);
    }

cleanup:
    /* ── Cleanup ─────────────────────────────────────────────────── */
    anim_cleanup(&anim);          /* restore cursor     */
    input_cleanup();              /* restore terminal   */

    if (have_audio)
        audio_cleanup(&audio);

    app_state_destroy(&state);

    printf("\nThanks for watching the wizard.\n");
    return EXIT_SUCCESS;
}
