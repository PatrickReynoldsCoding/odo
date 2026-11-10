/*
 * The touch pad as a MESH controller: the screen as an instrument. Columns of notes left to
 * right in a key and scale, each finger its own voice. Slide sideways to glide, up for louder
 * and brighter (the bottom fades to silence), rock a finger for vibrato. The note strip along
 * the bottom shows the notes; drag it sideways to move them (locked, it plays instead).
 *
 * Split, it is two pads side by side, each playing its own sound (part 0 left, 1 right), with
 * the divide dragged along the strip. A pad can hold a chord's notes instead of a scale's: a
 * chord wheel (the circle of fifths, three rings of chord types) or a chord grid (the twelve
 * roots across, a chord type in each row) takes its left part; touch a chord to play its notes,
 * slide to the next without lifting.
 *
 * A port of odo's built-in touch pad (PadScreen.kt, ChordUi.kt, Chords.kt and PadSettings in
 * Perf.kt, before 0.26). Android draws the pad in pixels; its pixel sizes (line widths, orbs,
 * ripples) are given here in dp at a typical phone density, PX dp per pixel.
 */
#include "mesh.h"

#define MAX_FINGERS 15
#define MAX_COLS 49   /* a Seaboard RISE 49's keys */
#define TRAIL 28
#define MAX_RIPPLES 16
#define MAX_GRABS 4

static const float PX = 0.38f;          /* dp per Android pixel, at ~2.6 px per dp */
static const float STRIP = 64;          /* the note strip's height, dp */
static const float DIVIDE_GRAB = 28;    /* a touch on the strip this close (dp) to the divide drags it */

/* ---- settings (the manifest's, in order) ---------------------------------------------------- */
enum {
    S_SIZE, S_OCTAVE, S_KEY, S_SCALE, S_MODE,
    S_SPLIT, S_R_MODE, S_R_SIZE, S_R_OCTAVE, S_R_KEY, S_R_SCALE, S_R_DRUMS,
    S_RING0, S_RING1, S_RING2, S_ALIGN1, S_ALIGN2, S_ROW0,
    S_VERTICAL = S_ROW0 + 7, S_FADE, S_GLIDE, S_VIBRATO, S_VIBSENSE, S_LOCKED, S_DRUMS, S_THEME, S_LINES,
    S_SPLIT_AT, S_ROOT, S_TYPE, S_MOD, S_ROOT2, S_TYPE2, S_MOD2, S_SHIFT, S_SHIFT2,
    S_COUNT
};
static double set_[S_COUNT] = {
    8, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 1, 2, 0, 1, 8, 9, 10, 5, 2,
    0, 2, 0, 2, 1, 0, 0, 0, 1,
    0.5, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const int SCALES[] = {0, 0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x9AD, 0x295, 0x4A9, 0x4E9, 0xFFF};
static const float VIBRATO_DEPTH[] = {0, 0.25f, 0.5f, 1};
static const float VIB_SENSE_GAIN[] = {0.5f, 1, 2, 4};
static const float FADE_HEIGHT[] = {0, 0.1f, 0.25f, 0.5f, 1};

/* Vibrato: semitones per dp of wobble at the Normal depth and Medium sensitivity, before it rounds off. */
static const float VIB_SEMIS_PER_DP = 0.08f, VIBRATO_NORMAL = 0.5f;
static const float VIB_STILL_DP = 0.4f;     /* wobble smaller than this is a still finger's jitter */
static const float VIB_SLIDE_DPS = 40;      /* a finger moving one way this fast is half sliding */
static const float SNAP_HOLD = 0.08f;       /* Snap: columns past the edge before the note changes */

/* The drum kit's notes (drums/kit.dsp, General MIDI order from C), most used first. */
static const int DRUM_ORDER[12] = {36, 38, 42, 46, 39, 37, 44, 40, 41, 43, 45, 47};
static const char* DRUM_NAMES[12] = {"Kick", "Rim", "Snare", "Clap", "Crash", "Tom 1", "Hat", "Tom 2", "Pedal", "Tom 3",
                                     "Open", "Bell"};

static int iset(int k) { return (int)set_[k]; }

/* ---- small maths ------------------------------------------------------------------------------ */
static const float PI = 3.14159265f;
static float floorf_(float x) { return __builtin_floorf(x); }
static float sqrtf_(float x) { return __builtin_sqrtf(x); }
static int roundi(float x) { return (int)floorf_(x + 0.5f); }
static float absf(float x) { return x < 0 ? -x : x; }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int mod12(int v) { return ((v % 12) + 12) % 12; }
static float expf_(float x) {           /* good to ~1e-5 for the small arguments used here */
    if (x < -20) return 0;
    if (x > 20) x = 20;
    float y = x / 32, r = 1 + y * (1 + y * (0.5f + y * (1.0f / 6 + y * (1.0f / 24 + y / 120))));
    for (int k = 0; k < 5; k++) r *= r;
    return r;
}
static float tanhf_(float x) {
    if (x > 9) return 1;
    if (x < -9) return -1;
    float e = expf_(2 * x);
    return (e - 1) / (e + 1);
}
static float sinf_(float x) {           /* to ~1e-6 */
    x -= 2 * PI * floorf_((x + PI) / (2 * PI));
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static float cosf_(float x) { return sinf_(x + PI / 2); }
static float atan2f_(float y, float x) {  /* to ~1e-4 rad: enough to find a segment of a wheel */
    float ax = absf(x), ay = absf(y);
    if (ax < 1e-9f && ay < 1e-9f) return 0;
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = PI / 2 - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}

/* ---- themes (PAD_THEMES) ------------------------------------------------------------------------ */
typedef struct { unsigned top, mid, bottom, line, label, strip, part[2]; int flat; } Theme;
static const Theme THEMES[6] = {
    {0x1A1626FFu, 0x0E1014FFu, 0x0B1418FFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x07080BFFu, {0xFF8A65FFu, 0x4DD0E1FFu}, 0},   /* Night */
    {0x3A1430FFu, 0x2A1022FFu, 0x1A0C18FFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x12070FFFu, {0xFFB74DFFu, 0xF06292FFu}, 0},   /* Sunset */
    {0x0A2E3AFFu, 0x07202BFFu, 0x041318FFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x020B0EFFu, {0x80DEEAFFu, 0x69F0AEFFu}, 0},   /* Ocean */
    {0x1B2A17FFu, 0x131E11FFu, 0x0C140BFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x060B05FFu, {0xD4E157FFu, 0xFFB74DFFu}, 0},   /* Forest */
    {0x000000FFu, 0x000000FFu, 0x000000FFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x1C1C1CFFu, {0xFFEB3BFFu, 0x00E5FFFFu}, 0},   /* Contrast */
    {0xF7F3EAFFu, 0xF1ECE1FFu, 0xE9E3D6FFu, 0x1B1B1BFFu, 0x1B1B1BFFu, 0xDCD4C3FFu, {0xD84315FFu, 0x00838FFFu}, 0},   /* Daylight */
};
static const Theme EINK_THEME = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x000000FFu, 0x000000FFu, 0xFFFFFFFFu,
                                 {0x000000FFu, 0x666666FFu}, 1};
static const unsigned WHITE = 0xFFFFFFFFu, BLACK = 0x000000FFu;

/* ---- chords (Chords.kt) ------------------------------------------------------------------------- */
enum { T_MAJ, T_MIN, T_DIM, T_AUG, T_SUS2, T_SUS4, T_6, T_M6, T_7, T_MAJ7, T_M7, T_MMAJ7, T_DIM7, T_M7B5,
       T_ADD9, T_9, T_MAJ9, T_M9, T_11, T_M11, T_13, T_MAJ13, T_M13, T_7B9, T_7S9, T_7S11, T_7B13, T_7SUS4, T_COUNT };
/* Each type's notes, semitones from the root (-1 ends it). */
static const signed char TYPES[T_COUNT][7] = {
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 3, 6, -1}, {0, 4, 8, -1}, {0, 2, 7, -1}, {0, 5, 7, -1},
    {0, 4, 7, 9, -1}, {0, 3, 7, 9, -1}, {0, 4, 7, 10, -1}, {0, 4, 7, 11, -1}, {0, 3, 7, 10, -1}, {0, 3, 7, 11, -1},
    {0, 3, 6, 9, -1}, {0, 3, 6, 10, -1},
    {0, 4, 7, 14, -1}, {0, 4, 7, 10, 14, -1}, {0, 4, 7, 11, 14, -1}, {0, 3, 7, 10, 14, -1},
    {0, 4, 7, 10, 14, 17, -1}, {0, 3, 7, 10, 14, 17, -1}, {0, 4, 7, 10, 14, 21, -1}, {0, 4, 7, 11, 14, 21, -1},
    {0, 3, 7, 10, 14, 21, -1},
    {0, 4, 7, 10, 13, -1}, {0, 4, 7, 10, 15, -1}, {0, 4, 7, 10, 18, -1}, {0, 4, 7, 10, 20, -1}, {0, 5, 7, 10, -1},
};
/* How a chord's name ends: "" for major, "m", "°", "maj7"... */
static const char* SUFFIX[T_COUNT] = {
    "", "m", "\xC2\xB0", "aug", "sus2", "sus4", "6", "m6", "7", "maj7", "m7", "mMaj7", "dim7", "m7\xE2\x99\xAD" "5",
    "add9", "9", "maj9", "m9", "11", "m11", "13", "maj13", "m13",
    "7\xE2\x99\xAD" "9", "7\xE2\x99\xAF" "9", "7\xE2\x99\xAF" "11", "7\xE2\x99\xAD" "13", "7sus4",
};
static const char* PITCH[12] = {"C", "D\xE2\x99\xAD", "D", "E\xE2\x99\xAD", "E", "F", "G\xE2\x99\xAD", "G",
                                "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
/* The quick changes under the wheel: none, 7, 9, 6, sus4, add9. */
static const char* MODS[6] = {"", "7", "9", "6", "sus4", "add9"};
/* A ring's roots from the outer ring's: same root, relative minor, 7th degree, 4th, 5th, 2nd, 3rd. */
static const int ALIGN[7] = {0, 9, 11, 5, 7, 2, 4};
static const int FIFTHS[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};          /* the wheel, from C clockwise */
static const int GRID_ROOTS[12] = {1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6};      /* the grid, left to right */

static int has(int type, int semis) {
    for (int k = 0; TYPES[type][k] >= 0; k++) if (TYPES[type][k] == semis) return 1;
    return 0;
}

/* The chord in hand with a quick change on it: "7" on a minor chord is m7, on a major one 7... */
static int modified(int type, int mod) {
    if (mod <= 0 || mod > 5) return type;
    int minor = has(type, 3) && !has(type, 4) && !has(type, 6), dim = has(type, 3) && has(type, 6);
    switch (mod) {
    case 1: return dim ? T_M7B5 : minor ? T_M7 : type == T_SUS4 ? T_7SUS4 : T_7;
    case 2: return dim ? T_M7B5 : minor ? T_M9 : T_9;
    case 3: return dim ? T_DIM7 : minor ? T_M6 : T_6;
    case 4: return has(type, 10) ? T_7SUS4 : T_SUS4;
    default: return minor ? T_M9 : T_ADD9;
    }
}

/*
 * A pad's columns for a chord: its notes going up from the root nearest `center`, `count` of
 * them, starting `shift` chord notes higher (or lower). Extensions above the octave fold down
 * into it, so each octave holds the same notes.
 */
static void chord_columns(int root, int type, float center, int count, int shift, int* out) {
    int pcs[7], np = 0;
    for (int k = 0; TYPES[type][k] >= 0; k++) {
        int pc = mod12(TYPES[type][k]), dup = 0;
        for (int q = 0; q < np; q++) if (pcs[q] == pc) dup = 1;
        if (!dup) pcs[np++] = pc;
    }
    for (int i = 1; i < np; i++) for (int j = i; j > 0 && pcs[j - 1] > pcs[j]; j--) { int t = pcs[j]; pcs[j] = pcs[j - 1]; pcs[j - 1] = t; }
    int r = root;
    while ((float)(r + 12) <= center) r += 12;
    while ((float)r > center + 6) r -= 12;
    int all[64], n = 0;
    for (int o = -3; o <= 4; o++) for (int q = 0; q < np; q++) {
        int v = r + 12 * o + pcs[q];
        if (v >= 0 && v <= 127 && n < 64) all[n++] = v;
    }
    int first = 0;
    while (first < n && all[first] < r) first++;
    if (first >= n) first = 0;
    int back = count / 2 - np / 2;
    int s = clampi(first - (back > 0 ? back : 0) + shift, 0, n - count > 0 ? n - count : 0);
    for (int i = 0; i < count; i++) out[i] = s + i < n ? all[s + i] : 127;
}

/* ---- the surface and its pads ----------------------------------------------------------------- */
static float W = 600, H = 300;
static int eink;
static float center[2] = {60, 60};
static double now;
static int dirty = 1;
static int ticked;             /* a frame has passed: the settings sent on loading are in */

/*
 * One pad: its settings now (the right one's "Same as left" filled in), where it shows across the
 * surface (0..1), its chord picker, and its columns. Its notes are laid out from x0 to x1 as on a
 * whole surface (after a picker); split, it shows only what fits on its side, the left pad from
 * its left end, the right one up to its right end.
 */
typedef struct {
    int size, octave, key, scale, mode, drums;   /* mode 0 a scale, 1 the chord wheel, 2 the chord grid */
    int root, type, mod;                         /* the chord in hand */
    float start, end, x0, x1, pick;              /* fractions of W; pick: where its picker ends */
    int columns[MAX_COLS], n;
    int shift, shift_lo, shift_hi;
} Pad;
static Pad pads[2];
static int npads = 1;

static const Theme* theme(void) { return eink ? &EINK_THEME : &THEMES[clampi(iset(S_THEME), 0, 5)]; }
static float playH(void) { return H - STRIP > 1 ? H - STRIP : 1; }
static unsigned part_colour(int k) { return theme()->part[k & 1]; }
static int chords(const Pad* p) { return p->mode != 0 && !p->drums; }
static float pad_w(const Pad* p) { float w = p->x1 - p->x0; return w > 1e-3f ? w : 1e-3f; }

/* How much of the surface (0..1) a pad's chord picker takes: the wheel about 230 dp, the grid
   about 290 dp, never more than 60%; nothing in scale mode or for a drum kit. */
static float picker_frac(const Pad* p) {
    if (!chords(p)) return 0;
    return minf((p->mode == 1 ? 230 : 290) / W, 0.6f);
}

static void settle(int k);
static void layout(void) {
    npads = set_[S_SPLIT] != 0 ? 2 : 1;
    float at = npads == 2 ? clampf((float)set_[S_SPLIT_AT], 0.2f, 0.8f) : 1;
    for (int k = 0; k < npads; k++) {
        Pad* p = &pads[k];
        int r = k == 1;
        p->size = r && iset(S_R_SIZE) > 0 ? iset(S_R_SIZE) + 2 : iset(S_SIZE);
        p->octave = iset(S_OCTAVE) + (r ? iset(S_R_OCTAVE) : 0);
        p->key = r && iset(S_R_KEY) > 0 ? iset(S_R_KEY) - 1 : iset(S_KEY);
        p->scale = r && iset(S_R_SCALE) > 0 ? iset(S_R_SCALE) - 1 : iset(S_SCALE);
        p->mode = clampi(r && iset(S_R_MODE) > 0 ? iset(S_R_MODE) - 1 : iset(S_MODE), 0, 2);
        p->drums = set_[r ? S_R_DRUMS : S_DRUMS] != 0;
        p->root = mod12(iset(r ? S_ROOT2 : S_ROOT));
        p->type = clampi(iset(r ? S_TYPE2 : S_TYPE), 0, T_COUNT - 1);
        p->mod = clampi(iset(r ? S_MOD2 : S_MOD), 0, 5);
        p->shift = iset(r ? S_SHIFT2 : S_SHIFT);
        p->start = k == 0 ? 0 : at;
        p->end = k == 0 ? at : 1;
        float f = picker_frac(p), notesW = 1 - f;
        p->pick = p->start + minf(f, p->end - p->start);
        if (k == 0) { p->x0 = p->pick; p->x1 = p->pick + notesW; }
        else { p->x0 = 1 - notesW; p->x1 = 1; }
        settle(k);
    }
    dirty = 1;
}

/* Pad k's columns from its settings: a drum kit's drums, a chord's notes, or the scale's. */
static void settle(int k) {
    Pad* p = &pads[k];
    if (p->drums) {
        p->n = clampi(p->size, 4, 12);
        for (int i = 0; i < p->n; i++) p->columns[i] = DRUM_ORDER[i];
        p->shift_lo = p->shift_hi = 0;
        return;
    }
    int size = clampi(p->size, 3, MAX_COLS);
    float want = center[k] + 12.0f * (float)p->octave;
    p->n = size;
    if (p->mode != 0) {
        chord_columns(p->root, modified(p->type, p->mod), want, size, p->shift, p->columns);
        p->shift_lo = -24; p->shift_hi = 24;
        return;
    }
    int mask = SCALES[clampi(p->scale, 0, 9)];
    if (mask == 0) mask = 0xFFF;
    int in[128], n = 0;
    for (int q = 0; q < 128; q++) if ((mask >> mod12(q - p->key)) & 1) in[n++] = q;
    int c = 0;
    for (int q = 1; q < n; q++) if (absf((float)in[q] - want) < absf((float)in[c] - want)) c = q;
    int base = c - size / 2;
    int last = n - size > 0 ? n - size : 0;
    p->shift_lo = -base; p->shift_hi = last - base;
    int start = clampi(base + p->shift, 0, last);
    for (int i = 0; i < size; i++) p->columns[i] = start + i < n ? in[start + i] : 127;
}

/* x (0..1 across a pad's notes) to sounding pitch, for a glide amount g (0 snap .. 1 free). */
static float pitch_at(const int* cols, int n, float x, float g) {
    float u = x * (float)n - 0.5f;
    int i = clampi((int)floorf_(u), 0, n - 2);
    float t = u - (float)i;
    float r = g >= 0.995f ? t : clampf((t - (1 - g) / 2) / g, 0, 1);
    return (float)cols[i] + r * (float)(cols[i + 1] - cols[i]);
}
static int column_at(int n, float x) { return clampi(roundi(x * (float)n - 0.5f), 0, n - 1); }
/* Sounding pitch back to x across a pad, for drawing. */
static float x_of(const Pad* p, float pitch) {
    if (p->n < 2) return 0.5f;
    int i = 0;
    while (i < p->n - 2 && pitch > (float)p->columns[i + 1]) i++;
    int span = p->columns[i + 1] - p->columns[i];
    if (span < 1) span = 1;
    return ((float)i + (pitch - (float)p->columns[i]) / (float)span + 0.5f) / (float)p->n;
}

/* ---- fingers ---------------------------------------------------------------------------------- */
typedef struct {
    int used, id, voice, pad, drum;
    int cols[MAX_COLS], n;     /* the columns it went down on: dragging the strip does not bend it */
    float x, y;                /* across its pad's notes 0..1 (may go past their edges); height 0 bottom .. 1 top */
    float base;                /* pitch from where it is */
    int col;                   /* Snap: the column it plays */
    float centre, drift, lastX, vib;
    int q_pitch, q_pr, q_sl, q_vol;   /* what was last sent, quantised as the host does */
    float trail[TRAIL * 2];
    int tn;
} Finger;
static Finger fingers[MAX_FINGERS];
static int next_voice = 1;

/* A finger dragging a pad's note strip, the divide, or a chord picker. */
enum { G_STRIP, G_DIVIDE, G_PICK };
typedef struct { int used, id, kind, pad, shift; float x; } Grab;
static Grab grabs[MAX_GRABS];

typedef struct { float x, y, age; int pad; } Ripple;
static Ripple ripples[MAX_RIPPLES];

static float pitch_of(const Finger* f) { return f->base + f->vib; }

/* Where the finger is to its pitch, by the glide. Snap holds a note until the finger is clearly in the next column. */
static void place(Finger* f) {
    float g = (float)set_[S_GLIDE];
    int n = f->n;
    if (f->drum) { f->base = (float)f->cols[clampi(f->col, 0, n - 1)]; return; }
    if (g <= 0.005f || n < 2) {
        float u = f->x * (float)n - 0.5f;
        if (f->col < 0 || absf(u - (float)f->col) > 0.5f + SNAP_HOLD) f->col = clampi(roundi(u), 0, n - 1);
        f->base = (float)f->cols[f->col];
    } else {
        f->col = -1;
        f->base = pitch_at(f->cols, n, f->x, g);
    }
}

/* Pressure, slide and volume for a finger's height, by the Up/down setting. */
static void expression(const Finger* f, float* p, float* sl, float* vol) {
    float y = f->y, h = FADE_HEIGHT[clampi(iset(S_FADE), 0, 4)];
    float fade = 1;
    if (h > 0) { fade = clampf(y / h, 0, 1); fade *= fade; }
    switch (iset(S_VERTICAL)) {
    case 1: *p = 0.7f; *sl = y; *vol = 1; break;
    case 2: *p = y; *sl = 0.5f; *vol = fade; break;
    default: *p = y; *sl = y; *vol = fade; break;
    }
}

static void send_expression(Finger* f) {
    float p, sl, vol, pitch = pitch_of(f);
    expression(f, &p, &sl, &vol);
    int qp = roundi(pitch * 8192.0f / 48), qr = roundi(p * 127), qs = roundi(sl * 127), qv = roundi(vol * 127);
    if (qv != f->q_vol) { f->q_vol = qv; mesh_note_volume(f->voice, vol, now); }
    if (qp != f->q_pitch || qr != f->q_pr || qs != f->q_sl) {
        f->q_pitch = qp; f->q_pr = qr; f->q_sl = qs;
        mesh_note_change(f->voice, pitch, p, sl, now);
    }
}

/* After pad k's columns change: in a chord mode, fingers held on it move to the same column of the
   new chord, so the sound follows the chord. (In scale mode a finger keeps the notes it went down on.) */
static void follow(int k) {
    Pad* p = &pads[k];
    if (!chords(p)) return;
    for (int q = 0; q < MAX_FINGERS; q++) {
        Finger* f = &fingers[q];
        if (!f->used || f->pad != k) continue;
        f->n = p->n;
        for (int i = 0; i < p->n; i++) f->cols[i] = p->columns[i];
        if (f->col >= f->n) f->col = f->n - 1;
        place(f);
        send_expression(f);
    }
}

static Finger* find(int id) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used && fingers[k].id == id) return &fingers[k];
    return 0;
}
static Grab* find_grab(int id) {
    for (int k = 0; k < MAX_GRABS; k++) if (grabs[k].used && grabs[k].id == id) return &grabs[k];
    return 0;
}
static Grab* new_grab(int id, int kind, int pad, float x) {
    for (int k = 0; k < MAX_GRABS; k++) if (!grabs[k].used) {
        Grab* g = &grabs[k];
        g->used = 1; g->id = id; g->kind = kind; g->pad = pad; g->x = x; g->shift = pads[pad].shift;
        dirty = 1;
        return g;
    }
    return 0;
}

static int pad_at(float x) { return npads == 2 && x >= pads[1].start ? 1 : 0; }

/* A finger down at x (0..1 across the surface), height y, on pad k. */
static void down(int id, int k, float x, float y) {
    Pad* p = &pads[k];
    Finger* f = 0;
    for (int q = 0; q < MAX_FINGERS && !f; q++) if (!fingers[q].used) f = &fingers[q];
    if (!f) return;
    float lx = (x - p->x0) / pad_w(p);
    f->used = 1; f->id = id; f->voice = next_voice++; f->pad = k; f->drum = p->drums;
    f->n = p->n;
    for (int i = 0; i < p->n; i++) f->cols[i] = p->columns[i];
    f->x = lx; f->y = clampf(y, 0, 1);
    f->col = column_at(p->n, lx);
    f->centre = f->lastX = lx; f->drift = 0; f->vib = 0; f->tn = 0;
    place(f);
    float pr, sl, vol, pitch = pitch_of(f);
    expression(f, &pr, &sl, &vol);
    /* The strike follows the height too. */
    mesh_note_on(f->voice, npads == 2 ? k : 0, pitch, pr, sl, pr, now);
    /* The engine clears bend and pressure at the note on, which the host sends just before it:
       send them again, first a hair off so the host does not drop them as unchanged. */
    mesh_note_change(f->voice, pitch + 0.02f, pr < 0.5f ? pr + 0.01f : pr - 0.01f, sl, now);
    mesh_note_change(f->voice, pitch, pr, sl, now);
    mesh_note_volume(f->voice, vol, now);
    f->q_pitch = roundi(pitch * 8192.0f / 48); f->q_pr = roundi(pr * 127); f->q_sl = roundi(sl * 127); f->q_vol = roundi(vol * 127);
    if (!eink) for (int r = 0; r < MAX_RIPPLES; r++) if (ripples[r].age <= 0 || ripples[r].age > 0.7f) {
        ripples[r].x = x; ripples[r].y = f->y; ripples[r].age = 1e-4f; ripples[r].pad = k;
        break;
    }
    dirty = 1;
}

static void lift(Finger* f) {
    mesh_note_off(f->voice, now);
    f->used = 0;
    dirty = 1;
}

/* ---- the chord pickers ------------------------------------------------------------------------ */
static const float R_MID = 0.69f, R_IN = 0.43f, R_HUB = 0.22f;
static const float MOD_H = 30, GRID_LABEL = 46, GRID_HEAD = 22, GRID_GAP = 3;

typedef struct { float x, y, w, h; } Box;
/* Where pad k's picker is (dp). */
static Box picker_box(int k) {
    Box b = {pads[k].start * W, 0, (pads[k].pick - pads[k].start) * W, playH()};
    return b;
}
/* The wheel: its centre and radius, and the row of quick changes under it. */
static void wheel_geom(int k, float* cx, float* cy, float* rad, float* mods_y) {
    Box b = picker_box(k);
    float pad = 6, room = b.h - MOD_H - 3 * pad;
    float d = minf(b.w - 2 * pad, room);
    *rad = d / 2 > 1 ? d / 2 : 1;
    *cx = b.x + b.w / 2;
    *cy = pad + room / 2 + pad / 2;
    *mods_y = b.h - pad - MOD_H;
}
/* The quick change buttons: five, centred under the wheel. */
static Box mod_box(int k, int i) {
    Box b = picker_box(k);
    float cx, cy, rad, my;
    wheel_geom(k, &cx, &cy, &rad, &my);
    float gap = 4, bw = minf(48, (b.w - 12 - 4 * gap) / 5);
    float x0 = b.x + (b.w - 5 * bw - 4 * gap) / 2;
    Box m = {x0 + (float)(i - 1) * (bw + gap), my, bw, MOD_H};
    return m;
}

static int ring_type(int ring) { return clampi(iset(S_RING0 + ring), 0, T_COUNT - 1); }
static int ring_align(int ring) { return ring == 0 ? 0 : ALIGN[clampi(iset(ring == 1 ? S_ALIGN1 : S_ALIGN2), 0, 6)]; }
static int row_type(int row) { return clampi(iset(S_ROW0 + row), 0, T_COUNT - 1); }

/* Play a chord on pad k: from now on its notes are this chord's (fingers held there follow it). */
static void pick(int k, int root, int type) {
    Pad* p = &pads[k];
    if (p->root == root && p->type == type) return;
    p->root = root; p->type = type;
    set_[k ? S_ROOT2 : S_ROOT] = root; set_[k ? S_TYPE2 : S_TYPE] = type;
    mesh_set_setting(k ? S_ROOT2 : S_ROOT, root);
    mesh_set_setting(k ? S_TYPE2 : S_TYPE, type);
    settle(k);
    follow(k);
    dirty = 1;
}

/* The chord under (x, y) on pad k's picker, if any: 1 and its root and type. */
static int chord_at(int k, float x, float y, int* root, int* type) {
    Pad* p = &pads[k];
    if (p->mode == 1) {
        float cx, cy, rad, my;
        wheel_geom(k, &cx, &cy, &rad, &my);
        float dx = x - cx, dy = y - cy, r = sqrtf_(dx * dx + dy * dy) / rad;
        if (r > 1 || r < R_HUB) return 0;
        int ring = r > R_MID ? 0 : r > R_IN ? 1 : 2;
        float a = atan2f_(dx, -dy);                  /* clockwise from the top */
        if (a < 0) a += 2 * PI;
        int i = ((int)floorf_(a / (PI / 6) + 0.5f)) % 12;
        *root = (FIFTHS[i] + ring_align(ring)) % 12;
        *type = ring_type(ring);
        return 1;
    }
    Box b = picker_box(k);
    float lx = x - b.x - 5, ly = y - 5, w = b.w - 10, h = b.h - 10;
    if (lx < GRID_LABEL || ly < GRID_HEAD || w <= GRID_LABEL || h <= GRID_HEAD) return 0;
    int col = clampi((int)((lx - GRID_LABEL) / ((w - GRID_LABEL) / 12)), 0, 11);
    int row = clampi((int)((ly - GRID_HEAD) / ((h - GRID_HEAD) / 7)), 0, 6);
    *root = GRID_ROOTS[col];
    *type = row_type(row);
    return 1;
}

/* A touch on pad k's picker: a quick change (the wheel's), or a chord, then the chords it slides onto. */
static void picker_down(int id, int k, float x, float y) {
    Pad* p = &pads[k];
    if (p->mode == 1) for (int i = 1; i <= 5; i++) {
        Box m = mod_box(k, i);
        if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
            int v = p->mod == i ? 0 : i;
            p->mod = v;
            set_[k ? S_MOD2 : S_MOD] = v;
            mesh_set_setting(k ? S_MOD2 : S_MOD, v);
            settle(k);
            follow(k);
            dirty = 1;
            return;
        }
    }
    int root, type;
    if (chord_at(k, x, y, &root, &type)) pick(k, root, type);
    new_grab(id, G_PICK, k, x);
}

/* ---- the strip and the divide ----------------------------------------------------------------- */

/* A touch on the note strip: near a split's divide it drags the divide; elsewhere it drags that
   pad's notes sideways. Locked (or a drum kit), it plays like the rest of the pad. */
static void strip_down(int id, float fx, float fy) {
    int k = pad_at(fx);
    if (set_[S_LOCKED] != 0) { down(id, k, fx, fy); return; }
    if (npads == 2 && absf(fx - pads[1].start) * W < DIVIDE_GRAB) { new_grab(id, G_DIVIDE, 0, fx); return; }
    if (pads[k].drums) { down(id, k, fx, fy); return; }
    new_grab(id, G_STRIP, k, fx);
}

static void grab_move(Grab* g, float fx, float x, float y) {
    if (g->kind == G_DIVIDE) {
        double at = clampf(fx, 0.2f, 0.8f);
        if (at != set_[S_SPLIT_AT]) { set_[S_SPLIT_AT] = at; mesh_set_setting(S_SPLIT_AT, at); layout(); }
        return;
    }
    if (g->kind == G_PICK) {
        int root, type;
        if (chord_at(g->pad, x, y, &root, &type)) pick(g->pad, root, type);
        return;
    }
    Pad* p = &pads[g->pad];
    int lo = p->shift_lo < g->shift ? p->shift_lo : g->shift, hi = p->shift_hi > g->shift ? p->shift_hi : g->shift;
    int s = clampi(g->shift - roundi((fx - g->x) / pad_w(p) * (float)p->n), lo, hi);
    if (s != p->shift) {
        p->shift = s;
        set_[g->pad ? S_SHIFT2 : S_SHIFT] = s;
        settle(g->pad);
        dirty = 1;
    }
}

static void grab_up(Grab* g) {
    if (g->kind == G_STRIP) {                       /* where the notes were moved to is kept */
        int k = g->pad ? S_SHIFT2 : S_SHIFT;
        mesh_set_setting(k, set_[k]);
    }
    g->used = 0;
    dirty = 1;
}

/*
 * A small side-to-side wobble of a resting finger, made bigger. The wobble is the finger's
 * distance from where it has been over the last ~0.12 s; while the finger slides that distance
 * is a lag, not a wobble, so it is let through less the faster the slide. A tanh keeps a big
 * wobble from going past the setting's depth.
 */
static void vibrato(Finger* f, float dt) {
    float dp_per_x = W * pad_w(&pads[f->pad]);
    float depth = f->drum ? 0 : VIBRATO_DEPTH[clampi(iset(S_VIBRATO), 0, 3)];
    float v = (f->x - f->lastX) * dp_per_x / dt;
    f->lastX = f->x;
    f->drift += (v - f->drift) * (1 - expf_(-dt / 0.15f));
    float d = f->drift / VIB_SLIDE_DPS;
    float not_sliding = 1 / (1 + d * d * d * d);
    float a = 1 - expf_(-dt / 0.12f), b = 1 - not_sliding;
    f->centre += (f->x - f->centre) * (a > b ? a : b);
    if (depth == 0) { f->vib = 0; return; }
    float dev = (f->x - f->centre) * dp_per_x;
    float mag = absf(dev) - VIB_STILL_DP;
    float wobble = mag > 0 ? (dev < 0 ? -mag : mag) : 0;
    float gain = VIB_SEMIS_PER_DP * VIB_SENSE_GAIN[clampi(iset(S_VIBSENSE), 0, 3)];
    f->vib = depth * tanhf_(wobble * not_sliding * gain / VIBRATO_NORMAL);
}

/* Where to draw a finger across its pad's notes: a drum on its column; in a chord mode where the
   finger is (the notes change under it with the chord); otherwise where its pitch is. */
static float finger_x(const Finger* f) {
    const Pad* p = &pads[f->pad];
    if (f->drum) return ((float)f->col + 0.5f) / (float)p->n;
    if (chords(p)) return f->x;
    return x_of(p, pitch_of(f));
}

/* ---- drawing ---------------------------------------------------------------------------------- */
static const char* NAMES[12] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G", "A\xE2\x99\xAD", "A",
                                "B\xE2\x99\xAD", "B"};

static void note_name(const Pad* p, int n, char* out) {
    const char* s = p->drums ? DRUM_NAMES[mod12(n)] : NAMES[mod12(n)];
    int k = 0;
    while (s[k]) { out[k] = s[k]; k++; }
    if (!p->drums) {
        int o = n / 12 - 1;
        if (o < 0) { out[k++] = '-'; o = -o; }
        if (o >= 10) out[k++] = (char)('0' + o / 10);
        out[k++] = (char)('0' + o % 10);
    }
    out[k] = 0;
}

static int is_root(const Pad* p, int n) {
    if (p->drums) return n % 12 == 0;
    return mod12(n - (chords(p) ? p->root : p->key)) == 0;
}

/* About how wide Roboto draws s at size: its characters (not bytes) times an average width. */
static float text_w(const char* s, float size) {
    int n = 0;
    for (int i = 0; s[i]; i++) if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    return (float)n * size * 0.56f;
}

/*
 * How a set of chord names is drawn: as on a lead sheet, the root large and the rest of the name
 * smaller and raised beside it; or, where that will not fit, the rest under the root. One style
 * for a whole ring or row, set by its widest name, so they all match.
 */
typedef struct { int two; float root, suffix; } NameStyle;
static NameStyle fit_names(const char* const* roots, const char* const* suffixes, int n, float box_w, float box_h, float max_root) {
    float rs = minf(max_root, box_h * 0.78f), w1 = 0, w2 = 0;
    int any_suffix = 0;
    for (int i = 0; i < n; i++) {
        w1 = maxf(w1, text_w(roots[i], rs) + text_w(suffixes[i], rs * 0.7f));
        if (suffixes[i][0]) any_suffix = 1;
    }
    NameStyle s = {0, rs, rs * 0.7f};
    if (w1 <= box_w || !any_suffix) {
        if (w1 > box_w) { s.root *= box_w / w1; s.suffix = s.root * 0.7f; }
        return s;
    }
    float one = rs * box_w / w1, r2 = minf(max_root, box_h / 1.62f);
    for (int i = 0; i < n; i++) w2 = maxf(w2, maxf(text_w(roots[i], r2), text_w(suffixes[i], r2 * 0.7f)));
    if (w2 > box_w) r2 *= box_w / w2;
    if (one >= r2 * 0.8f) { s.root = one; s.suffix = one * 0.7f; return s; }
    s.two = 1; s.root = r2; s.suffix = r2 * 0.7f;
    return s;
}
/* One chord name centred on (cx, cy). */
static void draw_name(const char* root, const char* suffix, float cx, float cy, NameStyle s, unsigned c, int bold) {
    int b = bold ? MESH_TEXT_BOLD : 0;
    if (!s.two || !suffix[0]) {
        float rw = text_w(root, s.root), sw = text_w(suffix, s.suffix), x = cx - (rw + sw) / 2, base = cy + s.root * 0.36f;
        mesh_text(x, base, s.root, c, MESH_TEXT_LEFT | b, root);
        if (suffix[0]) mesh_text(x + rw, base - s.root * 0.3f, s.suffix, c, MESH_TEXT_LEFT | b, suffix);
    } else {
        float top = cy - (s.root + s.suffix * 1.05f) / 2;
        mesh_text(cx, top + s.root * 0.8f, s.root, c, MESH_TEXT_CENTER | b, root);
        mesh_text(cx, top + s.root + s.suffix * 0.85f, s.suffix, c, MESH_TEXT_CENTER | b, suffix);
    }
}
/* A chord type on its own, readable: "maj", "m", "dim", "maj7"... */
static const char* type_label(int t) { return t == T_MAJ ? "maj" : t == T_DIM ? "dim" : SUFFIX[t]; }

/* A ring's segment i (of 12), between radii r0 < r1, as convex pieces. */
static void segment(float cx, float cy, float r0, float r1, int i, unsigned fill) {
    enum { PIECES = 4 };
    float a0 = ((float)i - 0.5f) * PI / 6 - PI / 2;
    for (int q = 0; q < PIECES; q++) {
        float a = a0 + (float)q * (PI / 6) / PIECES, b = a + (PI / 6) / PIECES + 0.004f;
        float xy[8] = {cx + r1 * cosf_(a), cy + r1 * sinf_(a), cx + r1 * cosf_(b), cy + r1 * sinf_(b),
                       cx + r0 * cosf_(b), cy + r0 * sinf_(b), cx + r0 * cosf_(a), cy + r0 * sinf_(a)};
        mesh_poly(4, xy, fill, 0, 0);
    }
}
static void arc(float cx, float cy, float r, unsigned c, float w) {
    enum { STEPS = 72 };
    for (int q = 0; q < STEPS; q++) {
        float a = (float)q * 2 * PI / STEPS, b = (float)(q + 1) * 2 * PI / STEPS;
        mesh_line(cx + r * cosf_(a), cy + r * sinf_(a), cx + r * cosf_(b), cy + r * sinf_(b), c, w);
    }
}

static void draw_wheel(int k) {
    const Theme* t = theme();
    const Pad* p = &pads[k];
    unsigned accent = t->flat ? BLACK : part_colour(k), ink = t->flat ? BLACK : mesh_alpha(t->label, 0.85f);
    float cx, cy, rad, my;
    wheel_geom(k, &cx, &cy, &rad, &my);
    static const float R1[3] = {1, 0.69f, 0.43f}, R0[3] = {0.69f, 0.43f, 0.22f}, MAXR[3] = {0.13f, 0.105f, 0.085f};
    static const unsigned BASE[3] = {0x262A35FFu, 0x1D2029FFu, 0x171A21FFu};
    for (int ring = 0; ring < 3; ring++) {
        int type = ring_type(ring), roots[12];
        const char* rn[12];
        const char* sf[12];
        for (int i = 0; i < 12; i++) { roots[i] = (FIFTHS[i] + ring_align(ring)) % 12; rn[i] = PITCH[roots[i]]; sf[i] = SUFFIX[type]; }
        float mid = rad * (R0[ring] + R1[ring]) / 2;
        float side = minf(rad * (R1[ring] - R0[ring]), mid * PI / 6) * 0.84f;
        NameStyle st = fit_names(rn, sf, 12, side, side, rad * MAXR[ring]);
        for (int i = 0; i < 12; i++) {
            int on = p->root == roots[i] && p->type == type;
            segment(cx, cy, rad * R0[ring], rad * R1[ring], i, on ? accent : t->flat ? WHITE : BASE[ring]);
        }
        for (int i = 0; i < 12; i++) {
            int on = p->root == roots[i] && p->type == type;
            float a = (float)i * PI / 6;
            draw_name(rn[i], sf[i], cx + mid * sinf_(a), cy - mid * cosf_(a), st,
                      on ? (t->flat ? WHITE : 0x14161BFFu) : ink, on);
        }
    }
    /* The lines between the segments and around the rings. */
    unsigned lc = t->flat ? BLACK : 0x0B0D11FFu;
    float lw = t->flat ? 2 * PX * 2 : 3 * PX * 2;
    for (int i = 0; i < 12; i++) {
        float a = ((float)i - 0.5f) * PI / 6 - PI / 2;
        mesh_line(cx + rad * R_HUB * cosf_(a), cy + rad * R_HUB * sinf_(a), cx + rad * cosf_(a), cy + rad * sinf_(a), lc, lw);
    }
    arc(cx, cy, rad, lc, lw);
    arc(cx, cy, rad * R_MID, lc, lw);
    arc(cx, cy, rad * R_IN, lc, lw);
    /* The hub: the chord in hand, with its quick change. */
    mesh_circle(cx, cy, rad * R_HUB, t->flat ? WHITE : 0x0F1116FFu, accent, 3 * PX * 2);
    const char* hr[1] = {PITCH[p->root]};
    const char* hs[1] = {SUFFIX[modified(p->type, p->mod)]};
    float hub = rad * R_HUB * 1.45f;
    draw_name(hr[0], hs[0], cx, cy, fit_names(hr, hs, 1, hub, hub, rad * 0.14f), accent, 1);
    /* The quick changes: tap one to put it on every chord you pick, tap again to take it off. */
    for (int i = 1; i <= 5; i++) {
        Box m = mod_box(k, i);
        int on = p->mod == i;
        mesh_rect(m.x, m.y, m.w, m.h, m.h / 2, on ? (t->flat ? BLACK : mesh_alpha(accent, 0.3f)) : 0,
                  on ? accent : mesh_alpha(ink, 0.4f), 1);
        mesh_text(m.x + m.w / 2, m.y + m.h / 2 + 4.3f, 12, on && t->flat ? WHITE : on ? accent : ink, MESH_TEXT_CENTER, MODS[i]);
    }
}

static void draw_grid(int k) {
    const Theme* t = theme();
    const Pad* p = &pads[k];
    unsigned accent = t->flat ? BLACK : part_colour(k), ink = t->flat ? BLACK : t->label;
    Box b = picker_box(k);
    float x0 = b.x + 5, y0 = 5, w = b.w - 10, h = b.h - 10;
    if (w <= GRID_LABEL || h <= GRID_HEAD) return;
    float cw = (w - GRID_LABEL) / 12, rh = (h - GRID_HEAD) / 7;
    /* The chord in hand, in the corner. */
    const char* nr[1] = {PITCH[p->root]};
    const char* ns[1] = {SUFFIX[modified(p->type, p->mod)]};
    draw_name(nr[0], ns[0], x0 + GRID_LABEL / 2, y0 + GRID_HEAD / 2,
              fit_names(nr, ns, 1, GRID_LABEL - GRID_GAP, GRID_HEAD, GRID_HEAD * 0.8f), accent, 1);
    /* The roots along the top, all one size. */
    const char* rr[12];
    const char* empty[12];
    for (int c = 0; c < 12; c++) { rr[c] = PITCH[GRID_ROOTS[c]]; empty[c] = ""; }
    NameStyle rs = fit_names(rr, empty, 12, cw - GRID_GAP, GRID_HEAD, GRID_HEAD * 0.62f);
    for (int c = 0; c < 12; c++)
        draw_name(rr[c], "", x0 + GRID_LABEL + cw * ((float)c + 0.5f), y0 + GRID_HEAD / 2, rs, mesh_alpha(ink, t->flat ? 1 : 0.75f), 0);
    /* The rows' types, all one size, set by the longest. */
    const char* tl[7];
    for (int r = 0; r < 7; r++) tl[r] = type_label(row_type(r));
    NameStyle ts = fit_names(tl, empty, 7, GRID_LABEL - 2 * GRID_GAP, rh, minf(rh * 0.42f, GRID_HEAD * 0.7f));
    for (int r = 0; r < 7; r++) {
        float y = y0 + GRID_HEAD + rh * (float)r;
        int type = row_type(r);
        draw_name(tl[r], "", x0 + GRID_LABEL / 2, y + rh / 2, ts, mesh_alpha(ink, t->flat ? 1 : 0.85f), 1);
        for (int c = 0; c < 12; c++) {
            int on = p->root == GRID_ROOTS[c] && p->type == type;
            float x = x0 + GRID_LABEL + cw * (float)c;
            mesh_rect(x + GRID_GAP / 2, y + GRID_GAP / 2, cw - GRID_GAP, rh - GRID_GAP, GRID_GAP * 1.5f,
                      on ? accent : t->flat ? WHITE : 0x1C1F27FFu, t->flat ? BLACK : 0x333846FFu, 1.5f * PX * 2);
        }
    }
}

static void orb(float x, float y, float pressure, unsigned c) {
    const Theme* t = theme();
    float r = (60 + 70 * pressure) * PX;
    if (t->flat) {
        mesh_circle(x, y, r, WHITE, c, 6 * PX);
        mesh_circle(x, y, r * 0.3f, c, 0, 0);
        return;
    }
    mesh_glow(x, y, r * 1.8f, mesh_alpha(c, 0.55f), mesh_alpha(c, 0));
    mesh_circle(x, y, r, 0, mesh_alpha(c, 0.8f), 3 * PX);
    mesh_circle(x, y, r * 0.62f, mesh_alpha(t->bottom, 0.55f), 0, 0);
    mesh_circle(x, y, r * 0.24f, mesh_alpha(WHITE, 0.9f), 0, 0);
    mesh_circle(x, y, r * 0.17f, c, 0, 0);
}

/* A rectangle cut to [lo, hi] across. */
static void rect_in(float x, float y, float w, float h, float lo, float hi, unsigned fill) {
    float a = maxf(x, lo), b = minf(x + w, hi);
    if (b > a) mesh_rect(a, y, b - a, h, 0, fill, 0, 0);
}

/* Pad k: its columns (the ones that show beside its picker), then its strip. */
static void draw_pad(int k, float la, float ra, float lw, float rw, float ba) {
    const Theme* t = theme();
    const Pad* p = &pads[k];
    float ph = playH(), left = p->x0 * W, cw = pad_w(p) * W / (float)p->n;
    float lo = maxf(p->start, p->pick) * W, hi = p->end * W;
    unsigned c = part_colour(k);
    int held[MAX_COLS] = {0};
    for (int q = 0; q < MAX_FINGERS; q++) if (fingers[q].used && fingers[q].pad == k) {
        const Finger* f = &fingers[q];
        int i = f->drum ? f->col : column_at(p->n, x_of(p, pitch_of(f)));
        if (i >= 0 && i < p->n) held[i] = 1;
    }
    for (int i = 0; i < p->n; i++) {
        float x = left + ((float)i + 0.5f) * cw, x0 = left + (float)i * cw;
        /* Every other column shaded, so it is easy to see which one a finger is in. */
        if (i % 2 == 1 && ba > 0) rect_in(x0, 0, cw, ph, lo, hi, mesh_alpha(t->line, ba));
        if (held[i]) {
            if (t->flat) rect_in(x0, 0, cw, ph, lo, hi, 0xAAAAAAFFu);
            else {   /* a horizontal gradient, clear at the edges and 0.2 in the middle: nested bands */
                enum { BANDS = 8 };
                for (int s = 1; s <= BANDS; s++) {
                    float bw = cw * (float)s / BANDS;
                    rect_in(x - bw / 2, 0, bw, ph, lo, hi, mesh_alpha(c, 0.0275f));   /* 1 - 0.8^(1/8) */
                }
            }
        }
        if (x < lo || x > hi) continue;
        int root = is_root(p, p->columns[i]);
        unsigned lc = t->flat ? BLACK : mesh_alpha(root ? c : t->line, root ? ra : la);
        float width = t->flat ? (root ? rw + 2 : lw) : (root ? rw : lw);
        mesh_line(x, 0, x, ph, lc, width * PX);
    }
    /* The note strip: the notes' names; unlocked, a handle to drag them by. */
    float top = H - STRIP, sl = maxf(left, p->start * W), sr = minf(p->x1 * W, hi);
    int dragging = 0;
    for (int g = 0; g < MAX_GRABS; g++) if (grabs[g].used && grabs[g].kind == G_STRIP && grabs[g].pad == k) dragging = 1;
    if (sr > sl) {
        mesh_rect(sl, top, sr - sl, STRIP, 0, dragging && !t->flat ? mesh_alpha(c, 0.25f) : t->strip, 0, 0);
        mesh_line(sl, top, sr, top, t->flat ? t->line : mesh_alpha(t->line, 0.5f), (t->flat ? 3 : 2) * PX);
    }
    float ts = STRIP * 0.36f < cw * 0.42f ? STRIP * 0.36f : cw * 0.42f;
    for (int i = 0; i < p->n; i++) {
        float x = left + ((float)i + 0.5f) * cw, x0 = left + (float)i * cw;
        if (x < sl || x > sr) continue;
        int root = is_root(p, p->columns[i]);
        if (held[i] && t->flat) rect_in(x0, top, cw, STRIP, sl, sr, BLACK);
        if (i > 0 && x0 > sl) mesh_line(x0, top + STRIP * 0.2f, x0, H - STRIP * 0.2f, mesh_alpha(t->label, t->flat ? 0.6f : 0.18f), 2 * PX);
        unsigned tc = held[i] && t->flat ? WHITE : t->flat ? BLACK : root ? c : mesh_alpha(t->label, 0.85f);
        char name[16];
        note_name(p, p->columns[i], name);
        mesh_text(x, top + STRIP * 0.62f, ts, tc, MESH_TEXT_CENTER | (root ? MESH_TEXT_BOLD : 0), name);
    }
    /* Grip marks at the outer ends while the strip can be dragged (the divide has its own handle;
       a kit's strip does not move). */
    if (set_[S_LOCKED] == 0 && !p->drums) {
        unsigned gc = mesh_alpha(t->label, t->flat ? 1 : 0.45f);
        for (int q = 0; q < 3; q++) {
            float y = top + STRIP * (0.38f + 0.12f * (float)q);
            if (k == 0) mesh_line(sl + 8 * PX, y, sl + 26 * PX, y, gc, 2 * PX);
            if (k == npads - 1) mesh_line(sr - 26 * PX, y, sr - 8 * PX, y, gc, 2 * PX);
        }
    }
    /* Its chord picker, over its left part. */
    if (p->pick > p->start + 0.001f) {
        Box b = picker_box(k);
        mesh_rect(b.x, b.y, b.w, b.h, 0, t->flat ? WHITE : mesh_alpha(BLACK, 0.42f), t->flat ? BLACK : 0, t->flat ? 2 : 0);
        if (p->mode == 1) draw_wheel(k); else draw_grid(k);
    }
}

/* A split's divide: a line down the surface and, unlocked, a handle on the strip to drag it by. */
static void draw_divide(void) {
    const Theme* t = theme();
    float x = pads[1].start * W, top = H - STRIP;
    unsigned ink = t->flat ? BLACK : t->label;
    mesh_line(x, 0, x, H, t->flat ? BLACK : mesh_alpha(ink, 0.6f), (t->flat ? 4 : 3) * PX);
    if (set_[S_LOCKED] != 0) return;
    int dragging = 0;
    for (int g = 0; g < MAX_GRABS; g++) if (grabs[g].used && grabs[g].kind == G_DIVIDE) dragging = 1;
    float hw = 14, hh = STRIP * 0.7f, y0 = top + (STRIP - hh) / 2;
    mesh_rect(x - hw, y0, 2 * hw, hh, hw, t->flat ? WHITE : dragging ? t->part[0] : t->strip,
              t->flat ? BLACK : mesh_alpha(ink, 0.8f), (t->flat ? 3 : 2) * PX);
    for (int q = -1; q <= 1; q++)
        mesh_line(x + (float)q * 4, y0 + hh * 0.3f, x + (float)q * 4, y0 + hh * 0.7f, t->flat ? BLACK : mesh_alpha(ink, 0.7f), 2 * PX);
}

static int nodraw;              /* no one sees it (MESH_NODRAW): draw nothing */
static void draw(void) {
    if (nodraw) { dirty = 0; return; }
    const Theme* t = theme();
    float ph = playH();
    mesh_begin();
    if (t->flat) mesh_clear(t->top);
    else {
        mesh_vgradient(0, 0, W, H / 2, t->top, t->mid);
        mesh_vgradient(0, H / 2, W, H - H / 2, t->mid, t->bottom);
    }
    /* Soft, Clear, Bold: line alpha, root line alpha, line width, root width, band alpha */
    float la = 0.45f, ra = 0.85f, lw = 3, rw = 5, ba = 0.05f;
    if (iset(S_LINES) == 0) { la = 0.22f; ra = 0.5f; lw = 2; rw = 3; ba = 0; }
    else if (iset(S_LINES) == 2) { la = 0.75f; ra = 1; lw = 4; rw = 7; ba = 0.10f; }
    for (int k = 0; k < npads; k++) draw_pad(k, la, ra, lw, rw, ba);
    if (npads == 2) draw_divide();
    /* Ripples where fingers went down. */
    if (!t->flat) for (int r = 0; r < MAX_RIPPLES; r++) if (ripples[r].age > 0 && ripples[r].age <= 0.7f) {
        float k = ripples[r].age / 0.7f;
        mesh_circle(ripples[r].x * W, (1 - ripples[r].y) * ph, (40 + 220 * k) * PX, 0,
                    mesh_alpha(part_colour(ripples[r].pad), 0.5f * (1 - k)), (4 * (1 - k) + 1) * PX);
    }
    /* Each finger: its trail, then its orb, as big as it is loud. */
    for (int q = 0; q < MAX_FINGERS; q++) if (fingers[q].used) {
        Finger* f = &fingers[q];
        const Pad* p = &pads[f->pad];
        unsigned c = part_colour(f->pad);
        for (int i = 0; i < f->tn; i++) {
            float a = (float)(i + 1) / (float)f->tn;
            mesh_circle(f->trail[2 * i] * W, (1 - f->trail[2 * i + 1]) * ph, (8 + 26 * a) * PX, mesh_alpha(c, 0.22f * a), 0, 0);
        }
        float pr, sl, vol;
        expression(f, &pr, &sl, &vol);
        orb((p->x0 + finger_x(f) * pad_w(p)) * W, (1 - f->y) * ph, pr, c);
    }
    mesh_end();
    dirty = 0;
}

/* ---- the interface ------------------------------------------------------------------------------- */
MESH_EXPORT(init) void init(float width, float height, int flags, double t) {
    W = width > 1 ? width : 1; H = height > 1 ? height : 1; eink = flags & MESH_EINK; nodraw = flags & MESH_NODRAW; now = t;
    layout();
    draw();
}

MESH_EXPORT(instrument) void instrument(int part, float center_note) {
    if (part < 0 || part > 1 || center_note == center[part]) return;
    center[part] = center_note;
    layout();
}

MESH_EXPORT(setting) void setting(int index, double value) {
    if (index < 0 || index >= S_COUNT || set_[index] == value) return;
    set_[index] = value;
    /* Another way of laying out the notes (a scale, the wheel, the grid): they start from the
       middle again, as Android's pad did. Not while loading, when the saved shift follows. */
    if (ticked && (index == S_MODE || index == S_R_MODE)) {
        int both = index == S_MODE;
        for (int k = both ? 0 : 1; k <= 1; k++) {
            int sh = k ? S_SHIFT2 : S_SHIFT;
            if (set_[sh] != 0) { set_[sh] = 0; mesh_set_setting(sh, 0); }
        }
    }
    if (index == S_SPLIT && value == 0) {        /* one pad again: the right one's fingers let go */
        for (int q = 0; q < MAX_FINGERS; q++) if (fingers[q].used && fingers[q].pad == 1) lift(&fingers[q]);
        for (int g = 0; g < MAX_GRABS; g++) if (grabs[g].used && grabs[g].pad == 1) grabs[g].used = 0;
    }
    /* Everything but the touch and the look can move the notes: lay the pads out again (each keeps
       its strip's shift). A held finger in a chord mode follows a new chord. */
    layout();
    for (int k = 0; k < npads; k++) follow(k);
}

MESH_EXPORT(touch) void touch(int id, int phase, float x, float y, float size, float force, double t) {
    (void)size; (void)force;
    now = t;
    float fx = x / W, fy = 1 - y / playH();
    if (phase == MESH_DOWN) {
        Finger* old = find(id);
        if (old) lift(old);
        Grab* og = find_grab(id);
        if (og) og->used = 0;
        int k = pad_at(fx);
        if (y > H - STRIP) strip_down(id, fx, fy);
        else if (fx < pads[k].pick) picker_down(id, k, x, y);
        else down(id, k, fx, fy);
    } else if (phase == MESH_MOVE) {
        Grab* g = find_grab(id);
        if (g) { grab_move(g, fx, x, y); return; }
        Finger* f = find(id);
        if (!f) return;
        f->x = (fx - pads[f->pad].x0) / pad_w(&pads[f->pad]); f->y = clampf(fy, 0, 1);
        place(f);
        send_expression(f);
        dirty = 1;
    } else {
        Grab* g = find_grab(id);
        if (g) { grab_up(g); return; }
        Finger* f = find(id);
        if (f) lift(f);
    }
}

MESH_EXPORT(tick) void tick(double t) {
    ticked = 1;
    float dt = (float)((t - now) / 1000.0);
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    now = t;
    int moving = 0;
    for (int q = 0; q < MAX_FINGERS; q++) if (fingers[q].used) {
        Finger* f = &fingers[q];
        if (dt > 0) vibrato(f, dt);
        place(f);
        send_expression(f);
        if (!eink) {
            if (f->tn == TRAIL) {
                for (int i = 0; i < (TRAIL - 1) * 2; i++) f->trail[i] = f->trail[i + 2];
                f->tn--;
            }
            const Pad* p = &pads[f->pad];
            f->trail[2 * f->tn] = p->x0 + finger_x(f) * pad_w(p); f->trail[2 * f->tn + 1] = f->y;
            f->tn++;
            moving = 1;
        }
    }
    for (int r = 0; r < MAX_RIPPLES; r++) if (ripples[r].age > 0) {
        ripples[r].age += dt;
        if (ripples[r].age > 0.7f) ripples[r].age = 0;
        moving = 1;
    }
    if (dirty || moving) draw();
}

MESH_EXPORT(stop) void stop(void) {
    for (int q = 0; q < MAX_FINGERS; q++) if (fingers[q].used) lift(&fingers[q]);
    for (int g = 0; g < MAX_GRABS; g++) grabs[g].used = 0;
    dirty = 1;
}
