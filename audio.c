/*
 * wizard-cli – audio.c
 * WAV parser, ALSA output, real-time DSP pipeline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include "audio.h"
#include "dsp.h"

#define PERIOD_FRAMES  1024    /* ALSA period size in sample frames */

/* ── Byte-order-safe WAV helpers ──────────────────────────────────── */
static int rd_u16le(FILE *f, uint16_t *v)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return -1;
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return 0;
}
static int rd_u32le(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *v = (uint32_t)(b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24));
    return 0;
}

/* ── WAV chunk parser ─────────────────────────────────────────────── */
static int parse_wav(audio_state_t *a, const char *path)
{
    a->fp = fopen(path, "rb");
    if (!a->fp) {
        perror(path);
        return -1;
    }

    /* RIFF/WAVE header */
    char tag[5];
    tag[4] = '\0';
    uint32_t riff_size;
    if (fread(tag, 1, 4, a->fp) != 4 || memcmp(tag, "RIFF", 4) != 0) {
        fprintf(stderr, "audio: not a RIFF file: %s\n", path);
        fclose(a->fp);
        return -1;
    }
    if (rd_u32le(a->fp, &riff_size) < 0) goto bad;
    if (fread(tag, 1, 4, a->fp) != 4 || memcmp(tag, "WAVE", 4) != 0) {
        fprintf(stderr, "audio: not a WAVE file: %s\n", path);
        fclose(a->fp);
        return -1;
    }

    /* Walk chunks */
    int got_fmt = 0, got_data = 0;
    while (!got_data) {
        if (fread(tag, 1, 4, a->fp) != 4) break;
        tag[4] = '\0';
        uint32_t csz;
        if (rd_u32le(a->fp, &csz) < 0) break;

        if (memcmp(tag, "fmt ", 4) == 0) {
            if (csz < 16) { fprintf(stderr, "audio: bad fmt chunk\n"); goto bad; }
            uint16_t audio_fmt;
            if (rd_u16le(a->fp, &audio_fmt) < 0) goto bad;
            if (audio_fmt != 1) {
                fprintf(stderr, "audio: only PCM (format=1) WAV supported\n");
                goto bad;
            }
            if (rd_u16le(a->fp, &a->file_channels)    < 0) goto bad;
            if (rd_u32le(a->fp, &a->file_sample_rate) < 0) goto bad;
            uint32_t byte_rate; if (rd_u32le(a->fp, &byte_rate) < 0) goto bad;
            uint16_t blk_align; if (rd_u16le(a->fp, &blk_align) < 0) goto bad;
            if (rd_u16le(a->fp, &a->file_bits) < 0) goto bad;
            /* skip any extra fmt bytes */
            if (csz > 16) fseek(a->fp, (long)(csz - 16), SEEK_CUR);
            got_fmt = 1;

        } else if (memcmp(tag, "data", 4) == 0) {
            a->data_size   = csz;
            a->data_offset = ftell(a->fp);
            got_data = 1;

        } else {
            /* unknown chunk – skip */
            fseek(a->fp, (long)csz, SEEK_CUR);
        }
    }

    if (!got_fmt || !got_data) {
        fprintf(stderr, "audio: missing fmt or data chunk in %s\n", path);
        goto bad;
    }
    if (a->file_bits != 16) {
        fprintf(stderr, "audio: only 16-bit PCM WAV supported (file has %u-bit)\n",
                a->file_bits);
        goto bad;
    }
    if (a->file_channels < 1 || a->file_channels > 2) {
        fprintf(stderr, "audio: only mono/stereo WAV supported\n");
        goto bad;
    }
    return 0;

bad:
    fclose(a->fp);
    a->fp = NULL;
    return -1;
}

/* ── audio_init ───────────────────────────────────────────────────── */
int audio_init(audio_state_t *a, const char *path, app_state_t *state)
{
    memset(a, 0, sizeof(*a));
    a->state  = state;
    a->pcm_ok = 0;

    if (parse_wav(a, path) != 0)
        return -1;

    /* Open ALSA playback device */
    int err;
    if ((err = snd_pcm_open(&a->pcm, "default",
                             SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "audio: ALSA open: %s\n", snd_strerror(err));
        fclose(a->fp); a->fp = NULL;
        return -1;
    }

    /*
     * Always output: stereo, 44100 Hz, signed 16-bit LE.
     * ALSA's soft-resample handles sample-rate conversion if the WAV
     * differs from 44100 Hz.
     */
    if ((err = snd_pcm_set_params(a->pcm,
                                   SND_PCM_FORMAT_S16_LE,
                                   SND_PCM_ACCESS_RW_INTERLEAVED,
                                   2,           /* channels out */
                                   SAMPLE_RATE,
                                   1,           /* allow soft resample */
                                   80000)) < 0) { /* 80 ms target latency */
        fprintf(stderr, "audio: ALSA set_params: %s\n", snd_strerror(err));
        snd_pcm_close(a->pcm); a->pcm = NULL;
        fclose(a->fp);         a->fp  = NULL;
        return -1;
    }

    dsp_reverb_init(&a->reverb);
    dsp_lfo_init(&a->lfo);
    a->pcm_ok = 1;
    return 0;
}

/* ── audio_thread ─────────────────────────────────────────────────── */
void *audio_thread(void *arg)
{
    audio_state_t *a = (audio_state_t *)arg;
    if (!a->pcm_ok) return NULL;

    int     file_frame_bytes = (int)(a->file_channels * 2); /* bytes/frame */
    int16_t *file_buf  = malloc((size_t)PERIOD_FRAMES * file_frame_bytes);
    int16_t *play_buf  = malloc((size_t)PERIOD_FRAMES * 2 * sizeof(int16_t)); /* stereo */

    if (!file_buf || !play_buf) {
        fprintf(stderr, "audio: out of memory\n");
        free(file_buf); free(play_buf);
        return NULL;
    }

    fseek(a->fp, a->data_offset, SEEK_SET);

    while (a->state->running) {
        /* Read a chunk from the WAV file */
        size_t want     = (size_t)(PERIOD_FRAMES * file_frame_bytes);
        size_t got_bytes = fread(file_buf, 1, want, a->fp);

        if (got_bytes == 0) {
            /* End of file – loop back to beginning */
            fseek(a->fp, a->data_offset, SEEK_SET);
            continue;
        }

        int frames = (int)(got_bytes / (size_t)file_frame_bytes);

        /* ── Upmix mono → stereo ─────────────────────────────────── */
        if (a->file_channels == 1) {
            /* process backwards so we can do it in-place if needed */
            for (int i = frames - 1; i >= 0; i--) {
                play_buf[i * 2]     = file_buf[i];
                play_buf[i * 2 + 1] = file_buf[i];
            }
        } else {
            memcpy(play_buf, file_buf, (size_t)frames * 4);
        }

        /* ── Apply DSP effects ───────────────────────────────────── */
        dsp_process(play_buf, frames, a->state, &a->reverb, &a->lfo);

        /* ── Update HUD string (for animation overlay) ───────────── */
        {
            float vol, lfr, lfd, rvb;
            pthread_mutex_lock(&a->state->lock);
            vol = a->state->volume;
            lfr = a->state->lfo_rate;
            lfd = a->state->lfo_depth;
            rvb = a->state->reverb_mix;
            pthread_mutex_unlock(&a->state->lock);

            pthread_mutex_lock(&a->state->lock);
            snprintf(a->state->hud_line, sizeof(a->state->hud_line),
                     " Vol:%.2f  LFO:%.2fHz/%.2f  Reverb:%.2f ",
                     (double)vol, (double)lfr, (double)lfd, (double)rvb);
            pthread_mutex_unlock(&a->state->lock);
        }

        /* ── Write to ALSA ───────────────────────────────────────── */
        snd_pcm_sframes_t written = snd_pcm_writei(a->pcm, play_buf, frames);
        if (written < 0) {
            written = snd_pcm_recover(a->pcm, (int)written, 0);
            if (written < 0) {
                fprintf(stderr, "audio: ALSA write error: %s\n",
                        snd_strerror((int)written));
                break;
            }
        }
    }

    snd_pcm_drain(a->pcm);
    free(file_buf);
    free(play_buf);
    return NULL;
}

/* ── audio_cleanup ───────────────────────────────────────────────── */
void audio_cleanup(audio_state_t *a)
{
    if (a->pcm) { snd_pcm_close(a->pcm); a->pcm = NULL; }
    if (a->fp)  { fclose(a->fp);         a->fp  = NULL; }
}
