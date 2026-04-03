# lindalf

```
        ✦ The Wizard's Reverie ✦

*
            -/   \-
          -/      \-
        | /  /\ .\ .|
        | /  \/ .\..|
       / |    |  o.|o\
      ====================
      ====================
     \ \   -   -ooooo  / /
     \ \    (o)  oO@   / /
     \ \   . ==( o@    / /
     \  \             /  /
      \  \           /  /
     ______\  /______


```

A terminal art program featuring an animated ASCII wizard relaxing with
his pipe.  While he smokes, an epic folk fantasy music track can play in
the background — fully processed in real-time with LFO modulation and
reverb.

---

## Features

| Feature | Details |
|---|---|
| ASCII animation | Multi-frame wizard at 24 FPS, ANSI 256-colour |
| Smoke system | Particle-based smoke rising from the pipe |
| Fade-in | Soft curtain-raise at startup |
| Audio backend | ALSA, 16-bit PCM stereo, 44.1 kHz |
| Real-time DSP | Volume · LFO modulation · Feedback-delay reverb |
| Live controls | Keyboard adjustable without pausing |
| Threaded | Animation / Audio / Input run in separate pthreads |

---

## Requirements

### Runtime
- Linux (Ubuntu 20.04+ / Debian Bullseye+ recommended)
- Terminal with ANSI 256-colour support (xterm, gnome-terminal, kitty, …)
- ALSA (`libasound2`)

### Build
- GCC ≥ 9 or Clang ≥ 10
- `libasound2-dev`
- `pkg-config`
- GNU Make

---

## Installation

### 1 – Install build dependencies (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install build-essential pkg-config libasound2-dev
```

### 2 – Clone and build

```bash
git clone https://github.com/bellsohren/lindalf.git
cd lindalf
make
```

### 3 – Run

```bash
# Without music
./lindalf

# With a WAV file
./lindalf --music /path/to/your/music.wav
```

### 4 – System-wide install (optional)

```bash
sudo make install          # installs to /usr/local/bin/lindalf
sudo make uninstall        # removes it
```

---

## Controls

| Key | Action |
|-----|--------|
| `q` | Quit |
| `+` / `-` | Volume up / down (step 0.05, range 0.0 – 2.0) |
| `L` / `l` | LFO rate up / down (step 0.1 Hz, range 0.1 – 5.0 Hz) |
| `D` / `d` | LFO depth up / down (step 0.05, range 0.0 – 1.0) |
| `R` / `r` | Reverb mix up / down (step 0.05, range 0.0 – 0.9) |

The current parameter values are shown in the HUD at the bottom of the screen.

---

## Audio requirements

- Format: **16-bit signed PCM WAV** (`.wav`)
- Channels: mono or stereo (automatic upmix for mono)
- Sample rate: any (ALSA soft-resampler handles conversion)
- The file loops continuously

**No copyrighted music is bundled.**  You can generate a royalty-free
folk/fantasy track with:

- [LMMS](https://lmms.io/) – free DAW
- [MuseScore](https://musescore.org/) → File → Export → WAV
- [Suno](https://suno.com/) (check their licence for generated audio)
- [freemusicarchive.org](https://freemusicarchive.org/) (filter by CC0)
- `sox` to synthesise a simple test tone:

```bash
# Generate a 60-second 432 Hz sine wave test tone
sox -n -r 44100 -c 2 -b 16 test.wav synth 60 sine 432
./lindalf --music test.wav
```

---

## DSP details

### LFO (Low-Frequency Oscillator)
A sine-wave oscillator modulates the playback amplitude:

```
gain = volume × (1 + depth × sin(2π × rate × t))
```

- Rate 0.1 – 5.0 Hz controls tremolo speed
- Depth 0.0 – 1.0 controls intensity (0 = off)

### Reverb
A single-tap feedback delay line:

```
out  = in  + mix × delayed
store = out × feedback (0.45)
delay = mix × 0.5 s
```

Higher `mix` increases both delay time and wet signal.


---

## Project structure

```
lindalf/
├── main.c          Entry point, thread orchestration
├── shared.h        Shared app_state_t (volume, LFO, reverb, running flag)
├── animation.c/h   ANSI renderer, wizard frames, particle system
├── audio.c/h       WAV parser + ALSA playback
├── dsp.c/h         Volume · LFO · reverb pipeline
├── input.c/h       Raw-mode keyboard handler
├── Makefile
├── README.md
└── debian/         Packaging metadata
```

---

## Licence

MIT – see `LICENCE` file.

The project contains no copyrighted audio.  Any WAV file you provide
remains under its own licence.
