/*
 * wizard-cli – animation.c
 *
 * Full-screen ANSI animation of a wizard smoking a pipe.
 * Rendering strategy:
 *   1. Erase the animation rectangle with spaces (no global cls → no flash)
 *   2. Draw wizard frames with embedded ANSI colour codes
 *   3. Draw smoke particles
 *   4. Draw key-binding HUD at the bottom
 *   5. Emit entire render buffer in one write() call (~30 KB max)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "animation.h"

/* ── ANSI helpers ─────────────────────────────────────────────────── */
#define CSI        "\033["
#define CUP(r,c)   CSI #r ";" #c "H"   /* cursor position (literals only) */
#define HIDE_CUR   CSI "?25l"
#define SHOW_CUR   CSI "?25h"
#define CLR_SCR    CSI "2J" CSI "H"

/* Colours */
#define AX  "\033[0m"          /* reset                */
#define AH  "\033[0;35m"       /* hat – dark magenta   */
#define AHB "\033[1;35m"       /* hat – bright magenta */
#define AS1 "\033[1;33m"       /* hat star 1 – gold    */
#define AS2 "\033[0;33m"       /* hat star 2 – amber   */
#define AF  "\033[38;5;223m"   /* face / skin          */
#define AE0 "\033[1;36m"       /* eyes open – cyan     */
#define AE1 "\033[0;36m"       /* eyes blink           */
#define ABD "\033[1;37m"       /* beard – bright white */
#define AR  "\033[0;34m"       /* robe – blue          */
#define ARB "\033[1;34m"       /* robe bright          */
#define APP "\033[0;33m"       /* pipe – amber         */
#define ASM "\033[0;37m"       /* smoke – grey         */
#define ASM2 "\033[1;37m"      /* smoke – bright white */
#define AGL "\033[1;31m"       /* pipe glow – red/orange */
#define AGLD "\033[0;31m"      /* pipe glow dim        */
#define AGR "\033[32m"         /* green accent         */

/* ── Render buffer ────────────────────────────────────────────────── */
#define RBUF_SIZE  (1 << 17)   /* 128 KB – plenty for one frame */

/* ── Particle system ──────────────────────────────────────────────── */
#define MAX_PARTICLES  55
#define FPS            24
#define FRAME_NS       (1000000000L / FPS)

typedef struct {
    float  x, y;        /* floating-point screen position */
    float  vx, vy;      /* velocity in cells/frame        */
    int    age;
    int    max_age;
    int    bright;      /* 1 = bright smoke               */
} particle_t;

static particle_t g_particles[MAX_PARTICLES];
static int        g_num_particles = 0;
static unsigned   g_prng_state    = 12345;

/* Simple xorshift PRNG (no stdlib rand to avoid srand issues) */
static unsigned prng_next(void)
{
    g_prng_state ^= g_prng_state << 13;
    g_prng_state ^= g_prng_state >> 17;
    g_prng_state ^= g_prng_state << 5;
    return g_prng_state;
}

static float prng_float(void)   /* [0, 1) */
{
    return (float)(prng_next() & 0xFFFF) / 65536.0f;
}

static float prng_range(float lo, float hi)
{
    return lo + prng_float() * (hi - lo);
}

/* ── Particle management ──────────────────────────────────────────── */
static void particle_spawn(float px, float py)
{
    if (g_num_particles >= MAX_PARTICLES) return;
    particle_t *p = &g_particles[g_num_particles++];
    p->x       = px + prng_range(-0.3f, 0.3f);
    p->y       = py;
    p->vx      = prng_range(-0.18f, 0.18f);
    p->vy      = prng_range(-0.30f, -0.12f);
    p->age     = 0;
    p->max_age = (int)prng_range(18.0f, 40.0f);
    p->bright  = (prng_next() & 3) == 0;
}

static void particles_update(void)
{
    int i = 0;
    while (i < g_num_particles) {
        particle_t *p = &g_particles[i];
        p->x  += p->vx;
        p->y  += p->vy;
        p->vx += prng_range(-0.04f, 0.04f);  /* slight turbulence */
        p->age++;
        if (p->age >= p->max_age) {
            /* remove by swapping with last */
            g_particles[i] = g_particles[--g_num_particles];
        } else {
            i++;
        }
    }
}

/* ── Render buffer helpers ────────────────────────────────────────── */
static char  g_rbuf[RBUF_SIZE];
static int   g_rpos;

static void rb_reset(void)     { g_rpos = 0; }
static void rb_cat(const char *s)
{
    size_t len = strlen(s);
    if (g_rpos + (int)len + 1 < RBUF_SIZE) {
        memcpy(g_rbuf + g_rpos, s, len);
        g_rpos += (int)len;
    }
}
static void rb_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_rbuf + g_rpos, (size_t)(RBUF_SIZE - g_rpos), fmt, ap);
    va_end(ap);
    if (n > 0 && g_rpos + n < RBUF_SIZE) g_rpos += n;
}
/* Move cursor to 1-based (row, col) and emit str */
static void rb_at(int row, int col, const char *str)
{
    rb_printf("\033[%d;%dH%s", row, col, str);
}
static void rb_at_f(int row, int col, const char *fmt, ...)
{
    rb_printf("\033[%d;%dH", row, col);
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_rbuf + g_rpos, (size_t)(RBUF_SIZE - g_rpos), fmt, ap);
    va_end(ap);
    if (n > 0 && g_rpos + n < RBUF_SIZE) g_rpos += n;
}

/* ── Erase animation rectangle ────────────────────────────────────── */
static void rb_erase_rect(int row0, int col0, int rows, int cols)
{
    /* build a blank line of `cols` spaces */
    char blank[256];
    int n = cols < 255 ? cols : 255;
    memset(blank, ' ', (size_t)n);
    blank[n] = '\0';

    for (int r = 0; r < rows; r++)
        rb_at(row0 + r, col0, blank);
}

/* ── Wizard art ───────────────────────────────────────────────────── */
/*
 * The wizard occupies 20 display rows × 24 display columns.
 * Pipe smoke exit point:  +8 rows, +22 cols from wizard top-left.
 *
 * Frames cycle 0-3:
 *   Frame 0, 2 : eyes open  (o)
 *   Frame 1    : eyes wider (0)
 *   Frame 3    : blink      (-)
 *   Even frames: pipe glow bright
 *   Odd frames : pipe glow dim
 */
#define WIZ_ROWS  20
#define WIZ_COLS  26    /* display columns (no ANSI codes counted) */
#define PIPE_DROW  8    /* row delta from wizard top to pipe exit  */
#define PIPE_DCOL 22    /* col delta from wizard left to pipe exit */

static void draw_wizard(int wr, int wc, int frame)
{
    /* ── Hat ─────────────────────────────────────────────────────── */
    /*    Stars above the hat tip rotate between frames */
    const char *sc0 = (frame & 1) ? AS2"*"AX : AS1"*"AX;
    const char *sc1 = (frame & 1) ? AS1"*"AX : AS2"*"AX;
    const char *sc2 = (frame & 2) ? AS1"+"AX : AS2"·"AX;

    /* Starfield above hat */
    rb_at_f(wr + 0, wc + 4, AS2"%s"AX"  "AS1"%s"AX"  "AS2"%s"AX, sc1, sc2, sc0);
    rb_at_f(wr + 1, wc + 1, AS1"%s"AX"      "AS2"%s"AX, sc0, sc1);

    /* Hat body */
    rb_at_f(wr + 2, wc + 5, AH"    |"AX);
    rb_at_f(wr + 3, wc + 4, AH"   /|\\  "AX);
    rb_at_f(wr + 4, wc + 3, AH"  / | \\ "AX);
    rb_at_f(wr + 5, wc + 2, AH" /  |  \\"AX);

    /* Stars on hat — runtime pointer via %s format arg */
    const char *hs = (frame < 2) ? AS1 "*" AX : AS2 "+" AX;
    rb_at_f(wr + 6, wc + 1,
            AH "/" AX AS2 "." AX AH " %s | %s " AX AS2 "." AX AH "\\" AX,
            hs, hs);

    /* Hat brim */
    rb_at_f(wr + 7, wc + 0, AHB "/___________\\" AX);

    /* ── Face ─────────────────────────────────────────────────────── */
    /* Eye appearance varies per frame — colour and glyph as runtime args */
    const char *ec;      /* eye colour ANSI sequence */
    char        el, er;  /* left / right eye glyph   */
    switch (frame) {
        case 1:  el = '0'; er = '0'; ec = AE0; break;
        case 3:  el = '-'; er = '-'; ec = AE1; break;
        default: el = 'o'; er = 'o'; ec = AE0; break;
    }
    /* %s inserts the runtime colour string, %c inserts the eye glyph */
    rb_at_f(wr + 8, wc + 0,
            AF "|" AX "  %s%c" AX "    %s%c" AX "  " AF "|" AX,
            ec, el, ec, er);

    /* Nose + beard */
    rb_at_f(wr + 9, wc + 0, AF "|" AX ABD "  ~~~~~~~  " AX AF "|" AX);

    /* Mouth + pipe — ember glow varies with frame, inserted via %s */
    const char *pg = (frame & 1)
                     ? AGLD "\xE2\x97\x8F" AX   /* dim glow ● */
                     : AGL  "\xE2\x97\x8F" AX;  /* bright glow ● */
    rb_at_f(wr + 10, wc + 0,
            AF "|" AX ABD " (------ )" AX AF "|" AX APP "====" AX "%s",
            pg);

    /* Thick beard */
    rb_at_f(wr + 11, wc + 0, AF "|" AX ABD " ( WWWWW ) " AX AF "|" AX);
    rb_at_f(wr + 12, wc + 0, AF "|" AX ABD "  WWWWWWW  " AX AF "|" AX);
    rb_at_f(wr + 13, wc + 0, AF "|___________|" AX);

    /* ── Robe ─────────────────────────────────────────────────────── */
    rb_at_f(wr + 14, wc - 1, AR " /|" AX "           " AR "|\\" AX);

    /* Flowing robe — wave pattern inserted via %s */
    const char *wave = (frame & 1)
                       ? ARB "~\xC2\xB7~\xC2\xB7~\xC2\xB7~\xC2\xB7~" AX  /* bright ~·~ */
                       : AR  "\xC2\xB7~\xC2\xB7~\xC2\xB7~\xC2\xB7~\xC2\xB7" AX; /* dim ·~· */
    rb_at_f(wr + 15, wc - 2, AR "/  |" AX " %s " AR "|  \\" AX, wave);
    rb_at_f(wr + 16, wc - 1, AR " \\  |" AX "           " AR "|  /" AX);

    /* Lower body */
    rb_at_f(wr +17, wc + 0, AR"    |"AX AF"           "AX AR"|"AX);
    rb_at_f(wr +18, wc + 0, AR"    |___________|"AX);

    /* Feet */
    rb_at_f(wr +19, wc + 1, AR"   _|"AX "         " AR"|_"AX);
    rb_at_f(wr +20, wc + 0, AR"  (_)"AX "         " AR"(_)"AX);
}

/* ── Particle rendering ───────────────────────────────────────────── */
static const char *smoke_chars[] = { "o", "O", "*", ".", "·", " " };
#define SMOKE_CHARS_N  5

static void draw_particles(void)
{
    for (int i = 0; i < g_num_particles; i++) {
        particle_t *p = &g_particles[i];
        int row = (int)(p->y + 0.5f);
        int col = (int)(p->x + 0.5f);
        if (row < 1 || col < 1) continue;

        /* age-based char selection */
        int ci = (p->age * SMOKE_CHARS_N) / p->max_age;
        if (ci >= SMOKE_CHARS_N) ci = SMOKE_CHARS_N - 1;
        const char *ch = smoke_chars[ci];

        /* age-based colour */
        const char *clr = (p->age < p->max_age / 3) ? ASM2 : ASM;

        rb_printf("\033[%d;%dH%s%s"AX, row, col, clr, ch);
    }
}

/* ── HUD ──────────────────────────────────────────────────────────── */
static const char *HUD_KEYS =
    "\033[0;32m[+/-]\033[0m Vol  "
    "\033[0;32m[L/l]\033[0m LFO-Rate  "
    "\033[0;32m[D/d]\033[0m LFO-Depth  "
    "\033[0;32m[R/r]\033[0m Reverb  "
    "\033[0;32m[q]\033[0m Quit";

static void draw_hud(int term_rows, int term_cols __attribute__((unused)), app_state_t *state)
{
    char params[256] = { '\0' };
    pthread_mutex_lock(&state->lock);
    if (state->hud_line[0]) {
        snprintf(params, sizeof(params), "\033[1;37m%.200s\033[0m", state->hud_line);
    } else {
        snprintf(params, sizeof(params),
                 "\033[1;37m Vol:%.2f  LFO:%.2fHz/%.2f  Reverb:%.2f \033[0m",
                 (double)state->volume,
                 (double)state->lfo_rate,
                 (double)state->lfo_depth,
                 (double)state->reverb_mix);
    }
    pthread_mutex_unlock(&state->lock);

    rb_printf("\033[%d;1H%s", term_rows - 2, params);
    rb_printf("\033[%d;1H%s", term_rows - 1, HUD_KEYS);
}

/* ── Title banner ─────────────────────────────────────────────────── */
static void draw_title(int wc)
{
    rb_at_f(1, wc, AS1"✦ "AX AHB"The Wizard's Reverie"AX AS1" ✦"AX);
}

/* ── anim_init / anim_cleanup ─────────────────────────────────────── */
void anim_init(anim_state_t *a)
{
    (void)a;
    /* hide cursor, clear screen once */
    const char *seq = HIDE_CUR CLR_SCR;
    ssize_t r = write(STDOUT_FILENO, seq, strlen(seq));
    (void)r;
}

void anim_cleanup(anim_state_t *a)
{
    (void)a;
    /* show cursor, move to bottom */
    const char *seq = SHOW_CUR "\033[999;1H\n";
    ssize_t r = write(STDOUT_FILENO, seq, strlen(seq));
    (void)r;
}

/* ── anim_thread ──────────────────────────────────────────────────── */
void *anim_thread(void *arg)
{
    anim_state_t *a = (anim_state_t *)arg;

    struct timespec ts_frame;
    ts_frame.tv_sec  = 0;
    ts_frame.tv_nsec = FRAME_NS;

    int    frame       = 0;
    int    tick        = 0;       /* global tick counter */
    int    spawn_every = FPS / 3; /* spawn new particle 8×/sec */

    /* Fade-in counter */
    int fade_ticks = FPS * 2;    /* 2 s fade-in */

    while (a->state->running) {

        /* ── Get terminal size ───────────────────────────────────── */
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) < 0) {
            ws.ws_row = 24; ws.ws_col = 80;
        }
        int term_rows = ws.ws_row;
        int term_cols = ws.ws_col;

        /* ── Compute wizard top-left so it is centered ─────────── */
        int wiz_r = (term_rows - WIZ_ROWS) / 2;
        int wiz_c = (term_cols - WIZ_COLS) / 2;
        if (wiz_r < 3)  wiz_r = 3;
        if (wiz_c < 4)  wiz_c = 4;

        /* ── Pipe smoke origin (absolute terminal coords) ─────── */
        float smoke_origin_r = (float)(wiz_r + PIPE_DROW);
        float smoke_origin_c = (float)(wiz_c + PIPE_DCOL);

        /* ── Erase animation area ─────────────────────────────── */
        int erase_r = wiz_r - 4;   /* headroom above hat */
        if (erase_r < 1) erase_r = 1;
        int erase_rows = term_rows - erase_r - 3;  /* leave HUD rows */
        int erase_cols = term_cols - wiz_c + 10;
        if (erase_cols > term_cols) erase_cols = term_cols;

        rb_reset();
        rb_erase_rect(erase_r, 1, erase_rows, erase_cols);

        /* ── Title ────────────────────────────────────────────── */
        draw_title(wiz_c);

        /* ── Update & spawn particles ─────────────────────────── */
        particles_update();
        if ((tick % spawn_every) == 0) {
            /* Spawn 1-2 particles per spawn tick */
            particle_spawn(smoke_origin_c, smoke_origin_r);
            if ((tick % (spawn_every * 2)) == 0)
                particle_spawn(smoke_origin_c, smoke_origin_r);
        }

        /* ── Draw particles (behind wizard) ───────────────────── */
        draw_particles();

        /* ── Draw wizard ──────────────────────────────────────── */
        draw_wizard(wiz_r, wiz_c, frame);

        /* ── Fade-in overlay (blanks that thin out) ──────────── */
        if (fade_ticks > 0) {
            /* For each cell in wizard area, 50% probability of blank
             * that decreases linearly to 0 over fade_ticks.          */
            fade_ticks--;
            /* Simple approach: print a translucent grey wash that fades */
            float alpha = (float)fade_ticks / (float)(FPS * 2);
            int wash_cols = (int)(alpha * (float)WIZ_COLS);
            if (wash_cols > 1) {
                char wash[WIZ_COLS + 2];
                memset(wash, ' ', (size_t)wash_cols);
                wash[wash_cols] = '\0';
                for (int r = 0; r < WIZ_ROWS + 2; r++) {
                    int mask_col = wiz_c + WIZ_COLS - wash_cols;
                    if (mask_col > 0)
                        rb_at_f(wiz_r + r, mask_col,
                                "\033[40m%s\033[0m", wash);
                }
            }
        }

        /* ── HUD ──────────────────────────────────────────────── */
        draw_hud(term_rows, term_cols, a->state);

        /* ── Reset colour, hide cursor again ─────────────────── */
        rb_cat(AX HIDE_CUR);

        /* ── Flush entire frame in one write ─────────────────── */
        ssize_t written = 0;
        while (written < g_rpos) {
            ssize_t n = write(STDOUT_FILENO,
                              g_rbuf + written,
                              (size_t)(g_rpos - written));
            if (n <= 0) break;
            written += n;
        }

        /* ── Advance frame ────────────────────────────────────── */
        tick++;
        frame = (frame + 1) & 3;   /* 0 → 1 → 2 → 3 → 0 … */

        nanosleep(&ts_frame, NULL);
    }

    return NULL;
}
