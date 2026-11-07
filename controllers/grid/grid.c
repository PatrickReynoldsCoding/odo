/*
 * The Grid as a MESH controller: square keys, held upright, the notes climbing row by row from
 * the bottom right (1.1, 1.2, 1.3, then 2.1...; from the left, flipped across), so three fingers
 * sit on three neighbouring notes. Each square a note of a scale, or every semitone with the
 * notes outside the scale darkened. Hold a square and move it: across bends toward the note you
 * drag to, up and down changes the tone (each can be set to loudness, tone, bend or volume); a
 * quick wiggle is vibrato. A square is lit while its note sounds, and fades after.
 *
 * Square k counts along the rows from the bottom, endlessly: the rows run on past the top and
 * bottom of the screen, and the scroll bar (a thin strip the height of the screen, with each
 * root's octave marked) slides them up and down under the finger, as the touch pad's note strip
 * does. At full size the squares fill the width, edge to edge. Either axis can be flipped.
 *
 * Or, Up each column: the columns are each a scale of their own, climbing up the rows, and each
 * scrolls on its own. A lock (the Lock button, or the rail) keeps them still while you play;
 * unlocked, a drag on a column slides it and plays nothing.
 * Controls can sit in strips either side: Latch, Hold, Bend, Tone. The colours can mark notes
 * by their place in the key (the 4th always blue, say).
 *
 * Slide between notes: a finger plays whichever square it is on. Move onto another and its note
 * starts (the last one stops), without lifting: no strike zones, nothing across or up and down,
 * no vibrato; every note as loud as the Dynamics fader says.
 *
 * A port of odo's built-in Grid (GridUi.kt, KeyGrid.kt), one section (no split).
 */
#include "mesh.h"

#define MAX_FINGERS 10
#define MAX_VOICES 15          /* the MIDI channels MPE has for notes */
#define MAX_RING 32
#define MAX_SIDE 10

/* ---- settings (the manifest's, in order) ---------------------------------------------------- */
enum {
    S_COLS, S_LAYOUT, S_LOCK, S_SPLIT, S_SPLIT_SIZE, S_TOP_WAY, S_TOP_NOTES, S_TOP_SCALE, S_TOP_KEY, S_TOP_OCTAVE, S_SIZE, S_SPACING, S_FLIP_X, S_FLIP_Y, S_CHROMATIC, S_SCALE, S_KEY, S_OCTAVE,
    S_ACROSS, S_UPDOWN, S_BEND_RANGE, S_BEND_SNAP, S_ZONE_EDIT, S_ZONE_BLEND, S_ZONE_RANGE, S_VIBRATO, S_LABELS, S_THEME, S_COLOURS, S_ECHO,
    S_LATCH, S_LATCHMODE, S_BAR, S_SIDE0, S_TURN = S_SIDE0 + 6,
    /* hidden: kept by the MESH, set from the zone editor and the Dynamics fader */
    S_ZONE_MAP, S_ZONE_P1, S_DYN = S_ZONE_P1 + 4,
    /* the axes' shaping, set in the touch panel: amount and sensitivity in %, flip */
    S_AX_AMOUNT, S_AX_SENS, S_AX_FLIP, S_UD_AMOUNT, S_UD_SENS, S_UD_FLIP,
    /* Slide between notes: a finger plays the square under it, whichever, until it lifts */
    S_SLIDE, S_COUNT
};
/* Strike zones: a square as 3 x 3 zones (rows top to bottom as on screen), each a starting level
   0..5 (pp p mp mf f ff), kept as one number: zone c is digit c in base 6. */
#define ZONES_EVEN 6046617.0     /* every zone mf */
static double set_[S_COUNT] = {3, 0, 1, 0, 50, 0, 0, 0, 0, 0, 100, 9, 0, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 1, 24, 2, 0, 0, 0, 1, 0, 0, 2, 1, 0, 0, 0, 1, 2, 0,
                               ZONES_EVEN, ZONES_EVEN, ZONES_EVEN, ZONES_EVEN, ZONES_EVEN, 1,
                               100, 100, 0, 100, 100, 0, 0};
/* Bend range: 0 toward the next note; else semitones at a whole square's slide (as the gyro's). */
static const float BEND_RANGES[7] = {0, 1, 2, 3, 5, 7, 12};
enum { TURN_AUTO, TURN_UPRIGHT, TURN_SIDEWAYS };

enum { T_NONE, T_LOUDNESS, T_TONE, T_BEND, T_VOLUME };
enum { LATCH_CHORD, LATCH_TAP };
enum { C_LATCH, C_HOLD, C_BEND, C_TONE, C_DYN, C_LOCK, C_COUNT };   /* the side controls */
enum { COL_PLAIN, COL_DEGREES, COL_CHORD, COL_PILLARS, COL_NAMES };
/* Same notes elsewhere: how the other squares of a held note's pitch, in any octave, are marked. */
enum { ECHO_OFF, ECHO_RING, ECHO_GLOW };
enum { SIDE_OFF, SIDE_LEFT, SIDE_RIGHT };

/* SCALES[1..] of Perf.kt: bit d set when the degree d semitones above the key is in the scale. */
static const int MASKS[9] = {0xAB5, 0x5AD, 0x6AD, 0x6B5, 0x9AD, 0x295, 0x4A9, 0x4E9, 0xFFF};
static const float VIBRATO_DEPTH[4] = {0, 0.25f, 0.5f, 1};

/* ---- small maths ---------------------------------------------------------------------------- */
static float floorf_(float x) { return __builtin_floorf(x); }
static int roundi(float x) { return (int)floorf_(x + 0.5f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float absf(float v) { return v < 0 ? -v : v; }
static int floordiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static int floormod(int a, int b) { return a - b * floordiv(a, b); }
static float expf_(float x) {
    if (x < -80) return 0;
    if (x > 80) x = 80;
    float n = floorf_(x * 1.44269504f + 0.5f);
    float r = x - n * 0.69314718f;
    float p = 1 + r * (1 + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120)))));
    union { float f; unsigned u; } s;
    s.u = (unsigned)((int)n + 127) << 23;
    return p * s.f;
}

/* ---- the looks: GRID_THEMES ------------------------------------------------------------------- */
typedef struct { unsigned background, square, light, hot, on_light; int bright; } Theme;
static const Theme THEMES[6] = {
    {0x000000FFu, 0x0B1C18FFu, 0x23FFD0FFu, 0xDCFFF8FFu, 0x002A22FFu, 0},   /* Neon */
    {0x0D0705FFu, 0x27170FFFu, 0xFF8A4CFFu, 0xFFE2CCFFu, 0x3A1200FFu, 0},   /* Ember */
    {0x04070DFFu, 0x111A2AFFu, 0x7FC4FFFFu, 0xE6F3FFFFu, 0x02203AFFu, 0},   /* Ice */
    {0x08040FFFu, 0x1C1030FFu, 0xC58BFFFFu, 0xF3E6FFFFu, 0x240A40FFu, 0},   /* Violet */
    {0x0E0610FFu, 0x2A1226FFu, 0xFF5FA2FFu, 0xFFE0EEFFu, 0x3A0020FFu, 0},   /* Sunset */
    {0xEDE9E1FFu, 0xFFFFFFFFu, 0x0E7C66FFu, 0x6FE3C6FFu, 0xFFFFFFFFu, 1},   /* Paper */
};
static const unsigned WHITE = 0xFFFFFFFFu, BLACK = 0x000000FFu;

/* Colours for the notes of the key, by their interval above it: 1 red, 2 orange, 3 yellow,
   4 blue, 5 green, 6 indigo, 7 pink; the flattened 5th a slate between 4 and 5. */
static const unsigned DEGREE[12] = {
    0xFF5C5CFFu, 0xFF9F43FFu, 0xFF9F43FFu, 0xFFD43BFFu, 0xFFD43BFFu, 0x4DABF7FFu,
    0x8CA0B8FFu, 0x51CF66FFu, 0x9775FAFFu, 0x9775FAFFu, 0xF06595FFu, 0xF06595FFu};
/* By note name, whatever the key: round the circle of fifths, C red. */
static const unsigned NAMED[12] = {
    0xFF5C5CFFu, 0x9775FAFFu, 0xFFA94DFFu, 0xF06595FFu, 0xFFE066FFu, 0xE599F7FFu,
    0x69DB7CFFu, 0xFF8787FFu, 0x38D9A9FFu, 0xFFC078FFu, 0x4DABF7FFu, 0xC0EB75FFu};
static const Theme* theme(void) { int k = (int)set_[S_THEME]; return &THEMES[k < 0 || k > 5 ? 0 : k]; }

/* ---- the surface ---------------------------------------------------------------------------- */
static void turn_to(float sw, float sh);
/* The Grid is laid out upright, W x H; turned sideways (for a tablet held wide) everything it
   draws and every touch is turned a quarter: the bottom of the upright grid (its low notes) on
   the left, its right edge at the bottom. SW x SH is the surface itself. */
static float W = 400, H = 700, SW = 400, SH = 700;
static int turned;
static int eink;
static float center[2] = {60, 60}; /* the middle of each part's instrument, as a MIDI note */
static int have_top;               /* the host has said where the top part's instrument is */
static double now, last_tick = -1;
static int dirty = 1;

static const float STRIP = 64, BAR = 30, SIDE_TOP = 150;

/* Played state the side controls change (the rail's settings set them too). */
static int latch_on;
static int holding;
static float side_bend, side_tone;

/* Where things are: worked out again whenever something changes. */
static int cols = 3;
static float cell = 50, gap = 4, left = 0;               /* cell: a row's pitch */
/* Split: a second grid across the top, its own part (sound). Each half is its own grid: where it
   is (top..bot, upright), which way its rows climb, its notes and its scroll. Unsplit, only the
   bottom one, the whole height. Everything below works on the current half, hv. */
typedef struct {
    float top, bot, shown;         /* shown: rows in it */
    float pos, colpos[8];          /* the scroll: rows slid by (fractions too); up each column, each column's */
    int flip, key, chrom, mask, ndeg, degs[12], kshift;
} Half;
static Half halves[2] = {{0, 700, 7, 0, {0}, 0, 0, 0, 0xAB5, 7, {0}, 0}, {0, 0, 0, 0, {0}, 0, 0, 0, 0xAB5, 7, {0}, 0}};
static Half* hv = &halves[0];
static int nhalves = 1;
static int half_index(void) { return (int)(hv - halves); }
static float bar_x = -1;                                   /* the scroll bar's left edge, or -1 */
typedef struct { int control; float x, y, w, h; } SideRect;
static SideRect sides[MAX_SIDE];
static int nsides;

static int chromatic(void) { return hv->chrom; }
static int key(void) { return hv->key; }
/* Unflipped, the notes climb right to left along each row (for the right hand) and rows climb up. */
static int mirrored(void) { return set_[S_FLIP_X] == 0; }
static int flipped_y(void) { return hv->flip; }
static int split_on(void) { return set_[S_SPLIT] != 0; }
/* Up each column: every column runs up the scale on its own, and scrolls on its own (while unlocked). */
static int columns(void) { return set_[S_LAYOUT] != 0; }
static int locked(void) { return set_[S_LOCK] != 0; }
/* The Lock button only means something with the columns. */
static int side_of(int c) { return c == C_LOCK && !columns() ? SIDE_OFF : (int)set_[S_SIDE0 + c]; }
static int is_button(int c) { return c <= C_HOLD || c == C_LOCK; }

/* The strip of one side, from the bottom up as the thumb reaches: buttons their own height,
   sliders sharing the rest, none taller than 300. */
static void strip(int where, float x) {
    int list[C_COUNT], n = 0, nb = 0, ns = 0;
    for (int c = 0; c < C_COUNT; c++) if (side_of(c) == where) { list[n++] = c; if (is_button(c)) nb++; else ns++; }
    if (!n) return;
    float top = SIDE_TOP, bot = H - 10;
    float spare = (bot - top) - 52.0f * (float)nb - 10.0f * (float)(n - 1);
    float sh = ns ? clampf(spare / (float)ns, 40, 300) : 0;
    float total = 52.0f * (float)nb + sh * (float)ns + 10.0f * (float)(n - 1);
    float y = bot - total;
    for (int k = 0; k < n && nsides < MAX_SIDE; k++) {
        float h = is_button(list[k]) ? 52 : sh;
        SideRect r = {list[k], x + 6, y, STRIP - 12, h};
        sides[nsides++] = r;
        y += h + 10;
    }
}

static int index_of(int note);
static void keep(int index, double v);

static void layout(void) {
    cols = (int)set_[S_COLS];
    if (cols < 2) cols = 2;
    if (cols > 8) cols = 8;
    /* From the outside in, on each side: the scroll bar, then the strip of controls. */
    /* Up each column there is no scroll bar: each column is dragged on its own instead. */
    int nl = 0, nr = 0, bar = columns() ? SIDE_OFF : (int)set_[S_BAR];
    for (int c = 0; c < C_COUNT; c++) { if (side_of(c) == SIDE_LEFT) nl = 1; if (side_of(c) == SIDE_RIGHT) nr = 1; }
    float lw = (bar == SIDE_LEFT ? BAR : 0) + (nl ? STRIP : 0), rw = (bar == SIDE_RIGHT ? BAR : 0) + (nr ? STRIP : 0);
    bar_x = bar == SIDE_LEFT ? 0 : bar == SIDE_RIGHT ? W - BAR : -1;
    nsides = 0;
    strip(SIDE_LEFT, bar == SIDE_LEFT ? BAR : 0);
    strip(SIDE_RIGHT, W - rw);
    float gx = lw, gw = W - lw - rw;
    if (gw < 40) gw = 40;
    /* Full size: the squares reach both edges, with the spacing for a margin: a row's pitch p,
       its gap g*p, cols*p + g*p across. */
    float g = clampf((float)set_[S_SPACING] / 100.0f, 0.02f, 0.3f);
    float size = clampf((float)set_[S_SIZE] / 100.0f, 0.5f, 1);
    cell = size * gw / ((float)cols + g);
    gap = cell * g;
    left = gx + (gw - (cell * (float)cols + gap)) / 2 + gap;
    /* Split: the line across at the top's share of the height, a little gap either side of it. */
    nhalves = split_on() ? 2 : 1;
    float line = H * clampf((float)set_[S_SPLIT_SIZE] / 100.0f, 0.2f, 0.8f), half_gap = gap > 6 ? gap : 6;
    for (int h = 0; h < nhalves; h++) {
        Half* a = &halves[h];
        int bottom = h == 0;
        /* The top's notes are the bottom's unless it says otherwise; its rows mirror the bottom's
           (they climb toward the line, as the bottom's do) or run the same way up. */
        int sc = bottom || set_[S_TOP_SCALE] == 0 ? (int)set_[S_SCALE] : (int)set_[S_TOP_SCALE] - 1;
        a->key = bottom || set_[S_TOP_KEY] == 0 ? (int)set_[S_KEY] : (int)set_[S_TOP_KEY] - 1;
        a->chrom = bottom || set_[S_TOP_NOTES] == 0 ? set_[S_CHROMATIC] != 0 : set_[S_TOP_NOTES] == 2;
        a->flip = bottom ? set_[S_FLIP_Y] != 0 : set_[S_TOP_WAY] == 0 ? set_[S_FLIP_Y] == 0 : set_[S_FLIP_Y] != 0;
        a->top = bottom ? (nhalves > 1 ? line + half_gap / 2 : 0) : 0;
        a->bot = bottom ? H : line - half_gap / 2;
        a->shown = (a->bot - a->top) / cell;
        a->mask = MASKS[sc < 0 || sc > 8 ? 0 : sc];
        a->ndeg = 0;
        for (int d = 0; d < 12; d++) if ((a->mask >> d) & 1) a->degs[a->ndeg++] = d;
        hv = a;
        /* At no scroll, the middle of the half holds the note nearest its instrument's middle. */
        float c = bottom || !have_top ? center[0] : center[1];
        int oct = (int)set_[S_OCTAVE] + (bottom ? 0 : (int)set_[S_TOP_OCTAVE]);
        int target = roundi(c + 12.0f * (float)oct);
        int mid = columns() ? (int)floorf_(a->shown / 2) : (int)floorf_(a->shown / 2) * cols + cols / 2;
        a->kshift = index_of(target) - mid;
    }
    hv = &halves[0];
    dirty = 1;
}

/* The half at upright height y, or -1 on the line between. */
static int half_at(float y) {
    if (nhalves < 2) return 0;
    return y >= halves[0].top ? 0 : y <= halves[1].bot ? 1 : -1;
}

static int in_scale(int note) { return (hv->mask >> floormod(note - key(), 12)) & 1; }

/* The notes in order: index k is the k-th note of the scale (or semitone) above the key's C-1. */
static int note_at(int k) {
    return chromatic() ? key() + k : key() + 12 * floordiv(k, hv->ndeg) + hv->degs[floormod(k, hv->ndeg)];
}
/* The index of a note, or of the nearest note of the scale at or below it. */
static int index_of(int note) {
    int rel = note - key();
    if (chromatic()) return rel;
    int oct = floordiv(rel, 12), d = floormod(rel, 12), i = 0;
    for (int j = 0; j < hv->ndeg; j++) if (hv->degs[j] <= d) i = j;
    return oct * hv->ndeg + i;
}
/* The note of square k (k counts along the rows from row 0, which is at the bottom unscrolled).
   Up each column, the note is the row's alone: every column holds the same notes, scrolled its own way. */
static int note_of(int k) {
    int note = note_at((columns() ? floordiv(k, cols) : k) + hv->kshift);
    return note < 0 ? 0 : note > 127 ? 127 : note;
}

/* As far as the scroll goes: the lowest and highest notes just on screen. */
static void clamp_one(float* p, int per) {
    float lo = (float)floordiv(index_of(0) - hv->kshift, per) + 1;
    float hi = (float)floordiv(index_of(127) - hv->kshift, per) - hv->shown + 1;
    if (hi < lo) hi = lo;
    *p = clampf(*p, lo, hi);
}
static void clamp_pos(void) {
    Half* was = hv;
    for (int h = 0; h < nhalves; h++) {
        hv = &halves[h];
        if (!columns()) clamp_one(&hv->pos, cols);
        else for (int c = 0; c < cols; c++) clamp_one(&hv->colpos[c], 1);
    }
    hv = was;
}
/* The scroll of column c (as counted, not as on screen). */
static float scroll_of(int c) { return columns() ? hv->colpos[c] : hv->pos; }

static int column(int c) { return mirrored() ? cols - 1 - c : c; }

/* Row r's top edge on screen (it may be off it), scrolled by p. */
static float row_top(int r, float p) {
    return flipped_y() ? hv->top + ((float)r - p) * cell : hv->bot - ((float)r - p + 1) * cell;
}
/* The column (as counted) under x, or -1. */
static int column_at(float x) {
    int c = (int)floorf_((x - left + gap / 2) / cell);
    return c < 0 || c >= cols ? -1 : column(c);
}

static int square_at(float x, float y) {
    int c = column_at(x);
    if (c < 0) return -100000;
    float p = scroll_of(c);
    float f = flipped_y() ? (y - hv->top) / cell + p : (hv->bot - y) / cell + p;
    int r = (int)floorf_(f);
    return r * cols + c;
}
static float square_x(int k) { return left + (float)column(floormod(k, cols)) * cell; }
static float square_y(int k) { return row_top(floordiv(k, cols), scroll_of(floormod(k, cols))) + gap / 2; }

/* The square beside k on screen, to the right (dir 1) or left (-1). Along the rows it is the next
   note or the one before (it carries on into the next row); up each column it is the square at the
   same height in the next column, and past the edge the next note up or down its own column. */
static int beside(int k, int dir) {
    int along = mirrored() ? -dir : dir;
    if (!columns()) return k + along;
    int sc = column(floormod(k, cols)) + dir;     /* column() turns counted into on-screen and back */
    if (sc < 0 || sc >= cols) return k + along * cols;
    return square_at(left + (float)sc * cell + (cell - gap) / 2, square_y(k) + (cell - gap) / 2);
}

/* ---- fingers and their notes ------------------------------------------------------------------ */
typedef struct {
    int used, id, voice, n, note, half;   /* half: which grid it plays (the top is part 1) */
    float x0, y0, x, y, sx, sy;   /* where it came down, where it is, and its slow position */
    float strike;                 /* how loud it started: its strike zone's level */
} Finger;
static Finger fingers[MAX_FINGERS];
/* Notes left sounding after their fingers lifted (latch, or hold), oldest first. */
static Finger latched[MAX_VOICES];
static int nlatched;
/* Fingers that came down to let a latched square go: they play nothing until they lift. */
static int spent[MAX_FINGERS], nspent;
/* A square's light after its note was let go, fading. */
typedef struct { int n, half; float glow; } Ring;
static Ring ring[MAX_RING];
static int next_voice = 1;

typedef struct { int id, control, col, half; float from, top, h; float start, y; } SideTouch;
enum { C_COL = 98, C_BAR = 99 };     /* a finger scrolling a column (unlocked), or on the scroll bar */
static SideTouch side_touches[MAX_FINGERS];
static int touched[C_COUNT];

static Finger* find(int id) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used && fingers[k].id == id) return &fingers[k];
    return 0;
}
static int nfingers(void) { int c = 0; for (int k = 0; k < MAX_FINGERS; k++) c += fingers[k].used; return c; }

/* ---- strike zones ---------------------------------------------------------------------------- */
static const float LEVEL[6] = {0.25f, 0.4f, 0.55f, 0.7f, 0.85f, 1.0f};
static const char* LEVEL_NAME[6] = {"pp", "p", "mp", "mf", "f", "ff"};

/* The note's own volume for a strike level: ff full, pp the strike range below, the levels
   between evenly spaced in dB (so mp sounds half way, not just counts half way). */
static float strike_gain(float level) {
    float range = clampf((float)set_[S_ZONE_RANGE], 0, 60);     /* dB: how much quieter pp is than ff */
    float t = clampf((level - LEVEL[0]) / (LEVEL[5] - LEVEL[0]), 0, 1);
    return expf_(-range * (1 - t) * 0.1151293f);     /* 10^(dB/20): ln 10 / 20 */
}

static void zones_of(double v, int* z) {
    long long n = (long long)(v + 0.5);
    for (int c = 0; c < 9; c++) { z[c] = (int)(n % 6); n /= 6; }
}
static double zones_value(const int* z) {
    double v = 0, m = 1;
    for (int c = 0; c < 9; c++) { v += (double)(z[c] < 0 ? 0 : z[c] > 5 ? 5 : z[c]) * m; m *= 6; }
    return v;
}

/* The level where a square is struck: (u, v) across and down it, as it looks on screen. Smooth:
   blended between the zones' middles; else the zone it is in. */
static float strike_level(float u, float v) {
    int z[9];
    zones_of(set_[S_ZONE_MAP], z);
    u = clampf(u, 0, 1); v = clampf(v, 0, 1);
    if (set_[S_ZONE_BLEND] == 0) {
        int c = (int)(u * 3), r = (int)(v * 3);
        if (c > 2) c = 2;
        if (r > 2) r = 2;
        return LEVEL[z[r * 3 + c]];
    }
    float gx = clampf(u * 3 - 0.5f, 0, 2), gy = clampf(v * 3 - 0.5f, 0, 2);
    int c0 = (int)gx, r0 = (int)gy;
    if (c0 > 1) c0 = 1;
    if (r0 > 1) r0 = 1;
    float fx = gx - (float)c0, fy = gy - (float)r0;
    float a = LEVEL[z[r0 * 3 + c0]], b = LEVEL[z[r0 * 3 + c0 + 1]], c = LEVEL[z[(r0 + 1) * 3 + c0]], d = LEVEL[z[(r0 + 1) * 3 + c0 + 1]];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

typedef struct { float pitch, pressure, slide, volume; } Expr;
#define SLIDE_STRIKE 0.85f      /* how hard a slid-onto note is played: f */

/* The axes, from how far the finger has moved (slowly) from where it came down; vibrato from the
   rest. Loudness, tone and volume reach their full change half a square away (the squares are big:
   a whole one was too far to hear much); bend aims at the neighbouring note, a whole square away. */
#define REACH 0.5f
static void apply(Expr* e, int target, float d, int across, const Finger* f, float* semis) {
    /* The axis's own shaping: flipped, more or less sensitive (how short a slide is the full
       change), and how much of the change it makes. */
    int a = across ? S_AX_AMOUNT : S_UD_AMOUNT;
    float amount = clampf((float)set_[a] / 100.0f, 0, 1), sens = clampf((float)set_[a + 1] / 100.0f, 0.1f, 8);
    if (set_[a + 2] != 0) d = -d;
    d *= sens;
    float v = clampf(d, -1, 1) * amount, w = clampf(d / REACH, -1, 1) * amount;
    switch (target) {
    case T_LOUDNESS: {
        /* From where it was struck: up to full, down to nearly nothing. The volume follows it too, so
           it works on struck and plucked instruments, which hardly listen to pressure once the note
           has started. */
        float base = f->strike;
        float level = clampf(base + (w >= 0 ? 1 - base : base - 0.02f) * w, 0.02f, 1);
        e->pressure = level;
        e->volume = clampf(0.4f + level, 0, 1);
        break;
    }
    case T_TONE: e->slide = clampf(0.5f + 0.5f * w, 0, 1); break;
    case T_BEND: {
        float amount = clampf((absf(v) - 0.08f) / 0.92f, 0, 1);
        int r = (int)set_[S_BEND_RANGE];
        float range = BEND_RANGES[r < 0 || r > 6 ? 0 : r];
        if (range > 0) { *semis += (v >= 0 ? range : -range) * amount; break; }
        /* Toward the note you drag to: across, the next square or the one before (unflipped the
           next is to the left); up and down, the row above or below. */
        int dir = v >= 0 ? 1 : -1;
        int to = across ? note_of(beside(f->n, dir)) : note_of(f->n + dir * (flipped_y() ? -cols : cols));
        *semis += (float)(to - f->note) * amount;
        break;
    }
    case T_VOLUME: e->volume = clampf(0.7f + (w >= 0 ? 0.3f : 0.7f) * w, 0, 1); break;   /* both ways, from 0.7 */
    default: break;
    }
}

static Expr expression_in(const Finger* f);
/* A finger's notes are its own half's. */
static Expr expression(const Finger* f) {
    Half* was = hv;
    hv = &halves[f->half < nhalves ? f->half : 0];
    Expr e = expression_in(f);
    hv = was;
    return e;
}

static int sliding(void) { return set_[S_SLIDE] != 0; }

static Expr expression_in(const Finger* f) {
    if (sliding()) {
        /* Sliding: every note the same, firm and full; only the side controls change it. */
        float dyn = clampf((float)set_[S_DYN], 0, 1);
        Expr s = {(float)f->note + side_bend, SLIDE_STRIKE * (0.4f + 0.6f * dyn), clampf(0.5f + side_tone, 0, 1), dyn};
        return s;
    }
    /* Where nothing moves them: a firm press, the tone in the middle, full volume (or Volume's 0.7). */
    Expr e = {0, f->strike, 0.5f, 1};
    if ((int)set_[S_ACROSS] == T_VOLUME || (int)set_[S_UPDOWN] == T_VOLUME) e.volume = 0.7f;
    /* The strike's level also sets the note's own volume (ff full, pp the strike range below), so
       strike zones work on every instrument, even one that ignores how hard it is played (an organ). */
    e.volume *= strike_gain(f->strike);
    float semis = 0;
    apply(&e, (int)set_[S_ACROSS], (f->sx - f->x0) / cell, 1, f, &semis);
    apply(&e, (int)set_[S_UPDOWN], (f->y0 - f->sy) / cell, 0, f, &semis);
    semis += side_bend;
    /* Snap the bend onto semitones, as the gyro's does: the middle of each semitone holds the note
       and the move to the next is squeezed toward the edges (all of it, at full snap). Not the
       vibrato, added after. */
    float g = 1 - clampf((float)set_[S_BEND_SNAP] / 100.0f, 0, 1);
    if (g < 0.995f && semis != 0) {
        float base = floorf_(semis), t = semis - base;
        semis = base + (g < 0.005f ? (t < 0.5f ? 0 : 1) : clampf((t - (1 - g) / 2) / g, 0, 1));
    }
    /* The Dynamics fader: an expression pedal over every note. */
    float dyn = clampf((float)set_[S_DYN], 0, 1);
    e.volume *= dyn;
    e.pressure *= 0.4f + 0.6f * dyn;
    e.slide = clampf(e.slide + side_tone, 0, 1);
    int vi = (int)set_[S_VIBRATO];
    float depth = VIBRATO_DEPTH[vi < 0 || vi > 3 ? 0 : vi];
    if (depth > 0) semis += clampf((f->x - f->sx) * 0.08f * depth / 0.5f, -depth, depth);
    e.pitch = (float)f->note + semis;
    return e;
}

/* The MESH sends only what changed, so every finger can be sent every frame. */
static void send(const Finger* f) {
    Expr e = expression(f);
    mesh_note_volume(f->voice, e.volume, now);
    mesh_note_change(f->voice, e.pitch, e.pressure, e.slide, now);
}

static void glow_after(int half, int n) {
    for (int k = 0; k < MAX_RING; k++)
        if (ring[k].glow <= 0 || (ring[k].n == n && ring[k].half == half)) { ring[k].n = n; ring[k].half = half; ring[k].glow = eink ? 0 : 1; return; }
}

/* Note-off, and the square's light fades after it. */
static void let_go(const Finger* f) {
    mesh_note_off(f->voice, now);
    glow_after(f->half, f->n);
    dirty = 1;
}

static void unlatch(int k) {
    let_go(&latched[k]);
    for (int j = k; j < nlatched - 1; j++) latched[j] = latched[j + 1];
    nlatched--;
}

static void release_latched(void) {
    while (nlatched > 0) unlatch(0);
}

static int latching(void) { return latch_on || holding; }

static int find_latched(int half, int n) {
    for (int k = 0; k < nlatched; k++) if (latched[k].n == n && latched[k].half == half) return k;
    return -1;
}

static void down_in(int id, float x, float y, int h);
/* A finger on a square: it plays its half's note, on its half's part. */
static void down(int id, float x, float y) {
    int h = half_at(y);
    if (h < 0) return;
    hv = &halves[h];
    down_in(id, x, y, h);
    hv = &halves[0];
}

static void down_in(int id, float x, float y, int h) {
    int n = square_at(x, y);
    if (n == -100000) return;
    if (latch_on && set_[S_LATCHMODE] == LATCH_TAP) {
        /* Tap a held square to let it go. */
        int k = find_latched(h, n);
        if (k >= 0) { unlatch(k); if (nspent < MAX_FINGERS) spent[nspent++] = id; return; }
    } else if (latch_on && nfingers() == 0) release_latched();    /* a new hand: the last chord goes */
    /* The same note held over (hold, or a chord being added to) is played again, not doubled. */
    int k = find_latched(h, n);
    if (k >= 0) unlatch(k);
    /* Every voice busy: the oldest latched note makes way. */
    if (nfingers() + nlatched >= MAX_VOICES) { if (nlatched) unlatch(0); else return; }
    Finger* f = 0;
    for (int j = 0; j < MAX_FINGERS; j++) if (!fingers[j].used) { f = &fingers[j]; break; }
    if (!f) return;
    f->used = 1; f->id = id; f->voice = next_voice++; f->n = n; f->note = note_of(n); f->half = h;
    f->x0 = f->x = f->sx = x; f->y0 = f->y = f->sy = y;
    /* Where in its square it was struck, as the square looks on screen (turned or not). */
    float side = cell - gap, u = (x - square_x(n)) / side, w = (y - square_y(n)) / side;
    f->strike = sliding() ? SLIDE_STRIKE : turned ? strike_level(1 - w, u) : strike_level(u, w);
    for (int r = 0; r < MAX_RING; r++) if (ring[r].n == n && ring[r].half == h) ring[r].glow = 0;
    Expr e = expression(f);
    if (e.volume < 1) mesh_note_volume(f->voice, e.volume, now);   /* before: the note starts at it */
    mesh_note_on(f->voice, h, e.pitch, e.pressure, e.slide, f->strike, now);
    dirty = 1;
}

/* Sliding: the finger has moved onto another square (well inside it, so a finger resting on the
   line between two does not flicker between them). Its note stops and the new square's starts,
   on a voice of its own; a held (latched) note there is played again, not doubled. */
static void slide_to(Finger* f, float x, float y) {
    int h = half_at(y);
    if (h < 0) h = f->half;
    Half* was = hv;
    hv = &halves[h];
    int n = square_at(x, y);
    if (n == -100000 || (h == f->half && n == f->n)) { hv = was; return; }
    if (h == f->half) {
        /* Past the old square's edge by a little more than the gap. */
        float m = gap / 2 + cell * 0.08f, sx = square_x(f->n), sy = square_y(f->n), side = cell - gap;
        if (x > sx - m && x < sx + side + m && y > sy - m && y < sy + side + m) { hv = was; return; }
    }
    let_go(f);
    int k = find_latched(h, n);
    if (k >= 0) unlatch(k);
    f->voice = next_voice++; f->n = n; f->note = note_of(n); f->half = h;
    f->x0 = f->sx = x; f->y0 = f->sy = y;
    f->strike = SLIDE_STRIKE;
    for (int r = 0; r < MAX_RING; r++) if (ring[r].n == n && ring[r].half == h) ring[r].glow = 0;
    hv = was;
    Expr e = expression(f);
    if (e.volume < 1) mesh_note_volume(f->voice, e.volume, now);
    mesh_note_on(f->voice, h, e.pitch, e.pressure, e.slide, f->strike, now);
    dirty = 1;
}

static void up(int id) {
    for (int k = 0; k < nspent; k++) if (spent[k] == id) { spent[k] = spent[--nspent]; return; }
    Finger* f = find(id);
    if (!f) return;
    f->used = 0;
    if (latching() && nlatched < MAX_VOICES) {
        /* It stays as it is now (no vibrato left in it), until the latch or hold lets it go. */
        f->sx = f->x; f->sy = f->y;
        send(f);
        latched[nlatched++] = *f;
    } else let_go(f);
    dirty = 1;
}

static void set_latch(int on) {
    latch_on = on;
    if (!on && !holding) release_latched();
    dirty = 1;
}

/* The Hold button: while pressed, lifted fingers leave their notes; let go and they go (unless latched). */
static void hold(int on) {
    holding = on;
    if (!on && !latch_on) release_latched();
    dirty = 1;
}

static void resend_all(void) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used) send(&fingers[k]);
    for (int k = 0; k < nlatched; k++) send(&latched[k]);
}


/* ---- drawing, turned or not ----------------------------------------------------------------------- */
/* Upright (x, y) on the surface: turned, x along the upright grid goes down the surface and y,
   from the bottom, goes left to right. */
static float tx(float x, float y) { return turned ? H - y : x; }
static float ty(float x, float y) { return turned ? x : y; }
static void g_clear(unsigned c) { mesh_clear(c); }
static void g_rect(float x, float y, float w, float h, float r, unsigned fill, unsigned stroke, float sw) {
    if (turned) mesh_rect(H - (y + h), x, h, w, r, fill, stroke, sw);
    else mesh_rect(x, y, w, h, r, fill, stroke, sw);
}
static void g_circle(float x, float y, float r, unsigned fill, unsigned stroke, float sw) { mesh_circle(tx(x, y), ty(x, y), r, fill, stroke, sw); }
static void g_line(float x1, float y1, float x2, float y2, unsigned c, float w) { mesh_line(tx(x1, y1), ty(x1, y1), tx(x2, y2), ty(x2, y2), c, w); }
static void g_glow(float x, float y, float r, unsigned a, unsigned b) { mesh_glow(tx(x, y), ty(x, y), r, a, b); }
/* Text centred on an upright point, then moved by (dx, dy) on the surface itself: lines of a
   button stay one above the other, turned or not. */
static void g_text_at(float x, float y, float dx, float dy, float size, unsigned c, int flags, const char* str) {
    mesh_text(tx(x, y) + dx, ty(x, y) + dy + size * 0.35f, size, c, flags, str);
}
/* Text stays upright: its middle is moved, and it is drawn level there. */
static void g_text(float x, float y, float size, unsigned c, int flags, const char* str) {
    float my = y - size * 0.35f;
    mesh_text(tx(x, my), ty(x, my) + size * 0.35f, size, c, flags, str);
}

/* ---- the side controls ---------------------------------------------------------------------- */
static float frac(SideTouch* s, float y) { return clampf(1 - (y - s->top) / s->h, 0, 1); }

static void side_drag(SideTouch* s, float at) {
    if (s->control == C_BEND) {
        side_bend = clampf((at - s->from) * 4, -2, 2);
        resend_all();
    } else if (s->control == C_TONE) {
        side_tone = clampf(at - 0.5f, -0.5f, 0.5f);
        resend_all();
    } else if (s->control == C_DYN) {
        set_[S_DYN] = at;          /* stays where it is left, as a pedal does */
        resend_all();
    }
    dirty = 1;
}

/* The scroll bar: the rows follow the finger, as a list does. */
/* Split, each half's part of the bar scrolls that half. */
static void bar_drag(SideTouch* s, float y) {
    float rows = (y - s->y) / cell;
    hv = &halves[s->half];
    hv->pos = s->start + (flipped_y() ? -rows : rows);
    hv = &halves[0];
    clamp_pos();
    dirty = 1;
}

/* Up each column, unlocked: the column follows the finger, and settles on a whole row when let go
   (so the rows line up across). */
static void col_drag(SideTouch* s, float y) {
    float rows = (y - s->y) / cell;
    hv = &halves[s->half];
    hv->colpos[s->col] = s->start + (flipped_y() ? -rows : rows);
    hv = &halves[0];
    clamp_pos();
    dirty = 1;
}

static void col_down(int id, float x, float y) {
    int c = column_at(x), h = half_at(y);
    if (c < 0 || h < 0) return;
    for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == 0) {
        SideTouch* s = &side_touches[j];
        s->id = id; s->control = C_COL; s->col = c; s->half = h; s->start = halves[h].colpos[c]; s->y = y;
        break;
    }
    dirty = 1;
}

static int side_down(int id, float x, float y) {
    if (bar_x >= 0 && x >= bar_x - 4 && x <= bar_x + BAR + 4) {
        for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == 0) {
            SideTouch* s = &side_touches[j];
            int h = half_at(y);
            s->half = h < 0 ? 0 : h;
            s->id = id; s->control = C_BAR; s->start = halves[s->half].pos; s->y = y;
            break;
        }
        return 1;
    }
    for (int k = 0; k < nsides; k++) {
        SideRect* r = &sides[k];
        if (x < r->x - 5 || x > r->x + r->w + 5 || y < r->y || y > r->y + r->h) continue;
        for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == 0) {
            SideTouch* s = &side_touches[j];
            s->id = id; s->control = r->control; s->top = r->y; s->h = r->h;
            s->from = frac(s, y);
            touched[r->control]++;
            if (r->control == C_HOLD) hold(1);
            side_drag(s, s->from);
            break;
        }
        return 1;
    }
    return 0;
}

static SideTouch* side_find(int id) {
    for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == id) return &side_touches[j];
    return 0;
}

static void side_up(SideTouch* s) {
    if (s->control == C_BAR) { s->id = 0; dirty = 1; return; }
    if (s->control == C_COL) {
        halves[s->half].colpos[s->col] = (float)roundi(halves[s->half].colpos[s->col]);
        clamp_pos();
        s->id = 0; dirty = 1; return;
    }
    touched[s->control]--;
    if (s->control == C_HOLD) hold(0);
    else if (s->control == C_LATCH) {
        set_latch(!latch_on);
        set_[S_LATCH] = latch_on;
        mesh_set_setting(S_LATCH, latch_on);   /* the rail shows it */
    }
    else if (s->control == C_BEND) { side_bend = 0; resend_all(); }
    else if (s->control == C_DYN) mesh_set_setting(S_DYN, set_[S_DYN]);   /* remembered */
    else if (s->control == C_LOCK) keep(S_LOCK, locked() ? 0 : 1);         /* the rail shows it */
    s->id = 0;
    dirty = 1;
}

/* ---- drawing ---------------------------------------------------------------------------------- */
static const char* NAMES[12] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G", "A\xE2\x99\xAD", "A",
                                "B\xE2\x99\xAD", "B"};

static char* put(char* p, const char* s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char* put_int(char* p, int v, int plus) {
    if (v < 0) { *p++ = '-'; v = -v; } else if (plus) *p++ = '+';
    char d[12]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = d[--n];
    *p = 0;
    return p;
}

static void draw_button(SideRect* r, const char* label, const char* value, int on, const Theme* th, unsigned ink) {
    unsigned fill = on ? (eink ? BLACK : mesh_alpha(ink, 0.85f)) : eink ? WHITE : th->square;
    unsigned edge = on ? (eink ? BLACK : th->hot) : mesh_alpha(ink, 0.5f);
    g_rect(r->x, r->y, r->w, r->h, 10, fill, edge, 1.5f);
    unsigned text = on ? (eink ? WHITE : th->on_light) : ink;
    float cx = r->x + r->w / 2, cy = r->y + r->h / 2;
    g_text_at(cx, cy, 0, -7, 11, text, MESH_TEXT_CENTER | MESH_TEXT_BOLD, label);
    g_text_at(cx, cy, 0, 8, 9, mesh_alpha(text, 0.75f), MESH_TEXT_CENTER, value);
}

static void draw_slider(SideRect* r, const char* label, const char* value, float at, int ticks, const Theme* th, unsigned ink) {
    g_rect(r->x, r->y, r->w, r->h, 10, eink ? WHITE : th->square, mesh_alpha(ink, touched[r->control] ? 0.9f : 0.5f), 1.5f);
    /* Room for the name: across the slider's end when turned (the track starts after it); upright,
       a name wider than the slider is cut to its first letters. */
    int n = 0;
    while (label[n]) n++;
    float lw = (float)n * 11;
    char cut[8];
    if (!turned && lw > r->w - 4) {
        int keep = (int)((r->w - 4) / 11);
        if (keep < 3) keep = 3;
        if (keep > 6) keep = 6;
        for (int i = 0; i < keep && i < n; i++) cut[i] = label[i];
        cut[keep < n ? keep : n] = 0;
        label = cut;
    }
    float top = r->y + (turned && lw + 16 > 46 ? lw + 16 : 46), bot = r->y + r->h - 12;
    for (int i = 0; i < ticks; i++) {
        float y = bot - (bot - top) * (float)i / (float)(ticks - 1);
        g_line(r->x + r->w * 0.3f, y, r->x + r->w * 0.7f, y, mesh_alpha(ink, 0.3f), 1);
    }
    float y = bot - (bot - top) * clampf(at, 0, 1), mid = bot - (bot - top) * 0.5f, x = r->x + r->w / 2;
    g_line(x, mid, x, y, mesh_alpha(ink, 0.6f), 4);
    g_rect(r->x + r->w * 0.18f, y - 5, r->w * 0.64f, 10, 5, eink ? BLACK : th->hot, 0, 0);
    /* The name and value at the slider's top end (upright), its right end (turned). */
    if (turned) {
        float at_ = r->y + (lw / 2 + 6 > 22 ? lw / 2 + 6 : 22);
        g_text_at(x, at_, 0, -7, 11, ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, label);
        g_text_at(x, at_, 0, 7, 9, mesh_alpha(ink, 0.75f), MESH_TEXT_CENTER, value);
    } else {
        g_text(x, r->y + 15, 11, ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, label);
        g_text(x, r->y + 28, 10, mesh_alpha(ink, 0.75f), MESH_TEXT_CENTER, value);
    }
}

static void draw_sides(const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    char v[24];
    for (int k = 0; k < nsides; k++) {
        SideRect* r = &sides[k];
        switch (r->control) {
        case C_LATCH:
            if (!latch_on) put(v, "off");
            else if (nlatched > 0) put(put_int(v, nlatched, 0), " held");
            else put(v, "on");
            draw_button(r, "Latch", v, latch_on, th, ink);
            break;
        case C_HOLD: draw_button(r, "Hold", holding ? "holding" : "press", holding, th, ink); break;
        case C_LOCK: draw_button(r, "Lock", locked() ? "on" : "off", !locked(), th, ink); break;
        case C_BEND: {
            int tenths = roundi(side_bend * 10);
            if (tenths == 0) put(v, "0");
            else {
                char* p = v;
                if (tenths < 0) { *p++ = '-'; tenths = -tenths; } else *p++ = '+';
                p = put_int(p, tenths / 10, 0);
                *p++ = '.';
                put_int(p, tenths % 10, 0);
            }
            draw_slider(r, "Bend", v, 0.5f + side_bend / 4, 5, th, ink);
            break;
        }
        case C_TONE:
            put_int(v, roundi(side_tone * 200), 1);
            draw_slider(r, "Tone", v, 0.5f + side_tone, 3, th, ink);
            break;
        case C_DYN:
            put(put_int(v, roundi((float)set_[S_DYN] * 100), 0), "%");
            draw_slider(r, "Dynamics", v, (float)set_[S_DYN], 5, th, ink);
            break;
        }
    }
}


/* ---- the touch panel: strike zones and the two axes, over the grid ---------------------------- */
/* Opened by the rail's "Edit touch & strike zones"; drawn and touched in surface coordinates
   (never turned). Strike zones: pick a level, then tap or drag over the zones to paint it; or load
   a preset; Save, then a slot, keeps the zones in one of your own four. Across and Up/down: what
   each moves, how much, how sensitive, and which way. Bend: its range and snap. Two columns on a
   wide surface; one, scrolled by dragging its background, on a narrow one. */
enum { K_DONE, K_CELL, K_LEVEL, K_SMOOTH, K_PRESET, K_SLOT, K_SAVE, K_TARGET, K_FLIP, K_SLIDER };
typedef struct { float x, y, w, h; int kind, arg; } Hit;
#define MAX_HITS 64
static Hit hits[MAX_HITS];
static int nhits, brush = 5, saving, ed_finger = -1, ed_slider = -1, ed_dragging;
static float ed_scroll, ed_from, ed_scroll0, ed_room, ed_content;

static const char* PRESET_NAME[7] = {"Even", "Top loud", "Bottom loud", "Centre loud", "Edges loud", "Left soft", "Right soft"};
static const signed char PRESETS[7][9] = {
    {3, 3, 3, 3, 3, 3, 3, 3, 3}, {5, 5, 5, 3, 3, 3, 1, 1, 1}, {1, 1, 1, 3, 3, 3, 5, 5, 5},
    {1, 2, 1, 2, 5, 2, 1, 2, 1}, {5, 4, 5, 4, 1, 4, 5, 4, 5}, {1, 3, 5, 1, 3, 5, 1, 3, 5}, {5, 3, 1, 5, 3, 1, 5, 3, 1}};
static const char* TARGET_NAME[5] = {"None", "Loudness", "Tone", "Bend", "Volume"};
static const char* BEND_NAME[7] = {"Next note", "1 semitone", "2 semitones", "3 semitones", "5 semitones", "7 semitones", "12 semitones"};

/* The sliders: the setting each moves, its range and step, and how its value reads. */
typedef struct { int index; float min, max, step; const char* label; int kind; } Slider;   /* kind 0 number, 1 %, 2 dB, 3 bend range */
enum { SL_RANGE, SL_AX_AMOUNT, SL_AX_SENS, SL_UD_AMOUNT, SL_UD_SENS, SL_BEND_RANGE, SL_BEND_SNAP, SL_COUNT };
static const Slider SLIDERS[SL_COUNT] = {
    {S_ZONE_RANGE, 0, 60, 1, "Softest below loudest", 2},
    {S_AX_AMOUNT, 0, 100, 5, "Amount", 1}, {S_AX_SENS, 25, 400, 5, "Sensitivity", 1},
    {S_UD_AMOUNT, 0, 100, 5, "Amount", 1}, {S_UD_SENS, 25, 400, 5, "Sensitivity", 1},
    {S_BEND_RANGE, 0, 6, 1, "Range", 3}, {S_BEND_SNAP, 0, 100, 5, "Snap to semitones", 1},
};

static int editing(void) { return set_[S_ZONE_EDIT] != 0; }

static void hit(float x, float y, float w, float h, int kind, int arg) {
    if (nhits < MAX_HITS) { Hit t = {x, y, w, h, kind, arg}; hits[nhits++] = t; }
}

static void keep(int index, double v) { set_[index] = v; mesh_set_setting(index, v); dirty = 1; }

static void chip(float x, float y, float w, float h, const char* label, int on, const Theme* th, int kind, int arg) {
    unsigned ink = eink ? BLACK : th->light;
    mesh_rect(x, y, w, h, 8, on ? (eink ? BLACK : mesh_alpha(ink, 0.85f)) : (eink ? WHITE : th->square), mesh_alpha(ink, on ? 1 : 0.45f), 1.5f);
    /* As big as fits: at most 11, smaller for a long name in a narrow chip (text runs about a size per letter). */
    int n = 0;
    while (label[n]) n++;
    float size = n ? clampf((w - 8) / (float)n, 7, 11) : 11;
    mesh_text(x + w / 2, y + h / 2 + size * 0.35f, size, on ? (eink ? WHITE : th->on_light) : ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, label);
    hit(x, y, w, h, kind, arg);
}

static void slider(float x, float y, float w, int id, const Theme* th) {
    const Slider* sl = &SLIDERS[id];
    unsigned ink = eink ? BLACK : th->light;
    float v = (float)set_[sl->index], t = clampf((v - sl->min) / (sl->max - sl->min), 0, 1);
    char val[24];
    if (sl->kind == 3) put(val, BEND_NAME[(int)v < 0 || (int)v > 6 ? 0 : (int)v]);
    else { char* p = put_int(val, roundi(v), 0); put(p, sl->kind == 1 ? "%" : sl->kind == 2 ? " dB" : ""); }
    mesh_text(x, y + 13, 10, mesh_alpha(ink, 0.8f), 0, sl->label);
    mesh_text(x + w, y + 13, 10, ink, MESH_TEXT_RIGHT | MESH_TEXT_BOLD, val);
    float ty = y + 28;
    mesh_rect(x, ty - 3, w, 6, 3, mesh_alpha(ink, 0.25f), 0, 0);
    mesh_rect(x, ty - 3, w * t, 6, 3, eink ? BLACK : mesh_alpha(ink, 0.8f), 0, 0);
    mesh_circle(x + w * t, ty, 9, eink ? BLACK : th->hot, eink ? WHITE : th->background, 2);
    hit(x - 6, y, w + 12, 40, K_SLIDER, id);
}

static void slider_to(int id, float x) {
    for (int k = 0; k < nhits; k++) if (hits[k].kind == K_SLIDER && hits[k].arg == id) {
        const Slider* sl = &SLIDERS[id];
        float t = clampf((x - (hits[k].x + 6)) / (hits[k].w - 12), 0, 1);
        float v = sl->min + t * (sl->max - sl->min);
        v = sl->min + (float)roundi((v - sl->min) / sl->step) * sl->step;
        if ((double)v != set_[sl->index]) keep(sl->index, v);
        return;
    }
}

static float section(float x, float y, const char* title, const Theme* th) {
    mesh_text(x, y + 16, 13, eink ? BLACK : th->light, MESH_TEXT_BOLD, title);
    return y + 26;
}

/* The strike zones: the square, the levels, smooth, range, presets, your four. */
static float zones_part(float x, float y, float cw, float maxE, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    y = section(x, y, "Strike zones", th);
    float E = cw < maxE ? cw : maxE;
    if (E < 90) E = 90;
    float ex = x + (cw - E) / 2, zs = E / 3;
    int z[9];
    zones_of(set_[S_ZONE_MAP], z);
    for (int c = 0; c < 9; c++) {
        float zx = ex + (float)(c % 3) * zs, zy = y + (float)(c / 3) * zs;
        float a = LEVEL[z[c]];
        mesh_rect(zx + 2, zy + 2, zs - 4, zs - 4, 8, eink ? mesh_alpha(BLACK, a * 0.6f) : mesh_alpha(th->light, 0.12f + 0.75f * a), mesh_alpha(ink, 0.5f), 1);
        mesh_text(zx + zs / 2, zy + zs / 2 + 7, zs > 80 ? 20 : 13, a > 0.6f && !eink ? th->on_light : ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, LEVEL_NAME[z[c]]);
        hit(zx, zy, zs, zs, K_CELL, c);
    }
    mesh_rect(ex, y, E, E, 10, 0, ink, 2);
    y += E + 12;
    float gap = 6, lw = (cw - 5 * gap) / 6;
    for (int l = 0; l < 6; l++) chip(x + (float)l * (lw + gap), y, lw, 32, LEVEL_NAME[l], l == brush, th, K_LEVEL, l);
    y += 40;
    chip(x, y, cw, 30, set_[S_ZONE_BLEND] != 0 ? "Smooth between zones: on" : "Smooth between zones: off", set_[S_ZONE_BLEND] != 0, th, K_SMOOTH, 0);
    y += 38;
    slider(x, y, cw, SL_RANGE, th);
    y += 44;
    float pw4 = (cw - 3 * gap) / 4;
    for (int k = 0; k < 7; k++) chip(x + (float)(k % 4) * (pw4 + gap), y + (float)(k / 4) * 36, pw4, 30, PRESET_NAME[k], 0, th, K_PRESET, k);
    y += 76;
    mesh_text(x, y + 20, 11, ink, MESH_TEXT_BOLD, saving ? "Save to:" : "Yours:");
    float sw = (cw - 64 - 4 * gap) / 5;
    for (int k = 0; k < 4; k++) {
        char name[4] = {(char)('1' + k), 0};
        chip(x + 64 + (float)k * (sw + gap), y, sw, 30, name, 0, th, K_SLOT, k);
    }
    chip(x + 64 + 4 * (sw + gap), y, sw, 30, "Save", saving, th, K_SAVE, 0);
    return y + 40;
}

/* One axis: what it moves, its amount and sensitivity, and its direction. */
static float axis_part(float x, float y, float cw, int across, const Theme* th) {
    y = section(x, y, across ? "Across" : "Up/down", th);
    int target = (int)set_[across ? S_ACROSS : S_UPDOWN];
    float gap = 6, tw = (cw - 4 * gap) / 5;
    for (int t = 0; t < 5; t++) chip(x + (float)t * (tw + gap), y, tw, 32, TARGET_NAME[t], t == target, th, K_TARGET, across * 8 + t);
    y += 40;
    slider(x, y, cw, across ? SL_AX_AMOUNT : SL_UD_AMOUNT, th);
    y += 44;
    slider(x, y, cw, across ? SL_AX_SENS : SL_UD_SENS, th);
    y += 44;
    int flipped = set_[across ? S_AX_FLIP : S_UD_FLIP] != 0;
    chip(x, y, cw, 30, flipped ? (across ? "Direction: flipped (left is more)" : "Direction: flipped (down is more)")
                               : (across ? "Direction: right is more" : "Direction: up is more"), flipped, th, K_FLIP, across);
    return y + 40;
}

static float bend_part(float x, float y, float cw, const Theme* th) {
    y = section(x, y, "Bend", th);
    slider(x, y, cw, SL_BEND_RANGE, th);
    y += 44;
    slider(x, y, cw, SL_BEND_SNAP, th);
    return y + 48;
}

static void ed_draw(const Theme* th) {
    nhits = 0;
    unsigned ink = eink ? BLACK : th->light, panel = eink ? WHITE : th->background;
    int two = SW >= 820 && SH >= 520;
    float pw = SW - 24 < (two ? 980 : 520) ? SW - 24 : (two ? 980 : 520), ph = SH - 24, px = (SW - pw) / 2, py = 12;
    float top = py + 62, bottom = py + ph - 8;
    ed_room = bottom - top;
    mesh_rect(0, 0, SW, SH, 0, eink ? 0xFFFFFFCCu : 0x000000B0u, 0, 0);
    mesh_rect(px, py, pw, ph, 16, panel, mesh_alpha(ink, 0.6f), 1.5f);
    /* The content, scrolled; then the header and the foot drawn over what scrolled under them. */
    float in = 16, y0 = top - ed_scroll, end;
    if (two) {
        float cw = (pw - 3 * in) / 2;
        float y1 = zones_part(px + in, y0, cw, ed_room - 290, th);
        float y2 = axis_part(px + 2 * in + cw, y0, cw, 1, th);
        y2 = axis_part(px + 2 * in + cw, y2 + 6, cw, 0, th);
        y2 = bend_part(px + 2 * in + cw, y2 + 6, cw, th);
        end = y1 > y2 ? y1 : y2;
    } else {
        float cw = pw - 2 * in;
        float y1 = zones_part(px + in, y0, cw, cw, th);
        y1 = axis_part(px + in, y1 + 6, cw, 1, th);
        y1 = axis_part(px + in, y1 + 6, cw, 0, th);
        end = bend_part(px + in, y1 + 6, cw, th);
    }
    ed_content = end - y0;
    mesh_rect(px + 1, py + 1, pw - 2, top - py - 1, 15, panel, 0, 0);
    mesh_rect(px + 1, bottom, pw - 2, py + ph - bottom - 1, 15, panel, 0, 0);
    mesh_rect(px, 0, pw, py, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py + ph, pw, SH - py - ph, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py, pw, ph, 16, 0, mesh_alpha(ink, 0.6f), 1.5f);
    mesh_text(px + 18, py + 30, 16, ink, MESH_TEXT_BOLD, "Touch");
    if (ed_content > ed_room) mesh_text(px + 18, py + 50, 10, mesh_alpha(ink, 0.7f), 0, "Drag to scroll");
    /* Hits under the header or foot can't be touched; Done always can. */
    int k = 0;
    for (int j = 0; j < nhits; j++) if (hits[j].y + hits[j].h > top && hits[j].y < bottom) hits[k++] = hits[j];
    nhits = k;
    chip(px + pw - 94, py + 10, 80, 32, "Done", 1, th, K_DONE, 0);
}

/* A touch on the panel: tap the buttons; drag a slider; tap or drag over the zones to paint them;
   drag anywhere else to scroll. */
static void ed_touch(int id, int phase, float x, float y) {
    if (phase != MESH_DOWN && id != ed_finger) return;
    if (phase == MESH_UP || phase == MESH_CANCEL) { ed_finger = -1; ed_slider = -1; ed_dragging = 0; return; }
    if (phase == MESH_MOVE) {
        if (ed_slider >= 0) { slider_to(ed_slider, x); return; }
        if (ed_dragging) {
            float most = ed_content - ed_room;
            ed_scroll = clampf(ed_scroll0 + (ed_from - y), 0, most > 0 ? most : 0);
            dirty = 1;
            return;
        }
    }
    if (phase == MESH_DOWN) { ed_finger = id; ed_slider = -1; ed_dragging = 0; }
    for (int k = 0; k < nhits; k++) {
        Hit* h = &hits[k];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        if (phase == MESH_MOVE && h->kind != K_CELL) return;
        int z[9];
        zones_of(set_[S_ZONE_MAP], z);
        switch (h->kind) {
        case K_DONE: saving = 0; keep(S_ZONE_EDIT, 0); break;
        case K_CELL: if (z[h->arg] != brush) { z[h->arg] = brush; keep(S_ZONE_MAP, zones_value(z)); } break;
        case K_LEVEL: brush = h->arg; break;
        case K_SMOOTH: keep(S_ZONE_BLEND, set_[S_ZONE_BLEND] != 0 ? 0 : 1); break;
        case K_PRESET: {
            int pz[9];
            for (int c = 0; c < 9; c++) pz[c] = PRESETS[h->arg][c];
            keep(S_ZONE_MAP, zones_value(pz));
            break;
        }
        case K_SLOT:
            if (saving) { keep(S_ZONE_P1 + h->arg, set_[S_ZONE_MAP]); saving = 0; }
            else keep(S_ZONE_MAP, set_[S_ZONE_P1 + h->arg]);
            break;
        case K_SAVE: saving = !saving; break;
        case K_TARGET: keep(h->arg >= 8 ? S_ACROSS : S_UPDOWN, h->arg % 8); break;
        case K_FLIP: keep(h->arg ? S_AX_FLIP : S_UD_FLIP, set_[h->arg ? S_AX_FLIP : S_UD_FLIP] != 0 ? 0 : 1); break;
        case K_SLIDER: ed_slider = h->arg; slider_to(h->arg, x); break;
        }
        dirty = 1;
        return;
    }
    if (phase == MESH_DOWN) { ed_dragging = 1; ed_from = y; ed_scroll0 = ed_scroll; }
}

/* The colour a note is marked with, or 0 to use the theme's. */
static unsigned note_colour(int note) {
    int scheme = (int)set_[S_COLOURS], iv = floormod(note - key(), 12);
    if (chromatic() && !in_scale(note)) return 0;
    switch (scheme) {
    case COL_DEGREES: return DEGREE[iv];
    case COL_CHORD: return iv == 0 || iv == 3 || iv == 4 || iv == 7 ? DEGREE[iv] : 0;
    case COL_PILLARS: return iv == 0 || iv == 5 || iv == 7 ? DEGREE[iv] : 0;
    case COL_NAMES: return NAMED[floormod(note, 12)];
    default: return 0;
    }
}

/* Square n of the current half: held, latched, its fading light. */
static int is_held(int n) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used && fingers[k].n == n && fingers[k].half == half_index()) return 1;
    return 0;
}
static int is_kept(int n) { for (int k = 0; k < nlatched; k++) if (latched[k].n == n && latched[k].half == half_index()) return 1; return 0; }
static float glow_of(int n) {
    float g = 0;
    for (int k = 0; k < MAX_RING; k++) if (ring[k].glow > g && ring[k].n == n && ring[k].half == half_index()) g = ring[k].glow;
    return g;
}

/* A rectangle cut to the current half, so a square never reaches across the split line. */
static void c_rect(float x, float y, float w, float h, float r, unsigned fill, unsigned stroke, float sw) {
    if (nhalves > 1) {
        float t = hv->top, b = hv->bot;
        if (y < t) { h -= t - y; y = t; }
        if (y + h > b) h = b - y;
        if (h <= 0.5f) return;
        if (r > h / 2) r = h / 2;
    }
    g_rect(x, y, w, h, r, fill, stroke, sw);
}
static int in_half(float y) { return y >= hv->top && y <= hv->bot; }

static void note_name(char* out, int note, int octave) {
    char* p = put(out, NAMES[note % 12]);
    if (octave) put_int(p, note / 12 - 1, 0);
}

/* The scroll bar: the height of the screen, a marker and its octave at each row that holds the key's root. */
static void draw_bar(const Theme* th) {
    if (bar_x < 0) return;
    int flat = eink;
    unsigned ink = flat ? BLACK : th->light;
    for (int h = 0; h < nhalves; h++) {
    hv = &halves[h];
    g_rect(bar_x + 3, hv->top + 4, BAR - 6, hv->bot - hv->top - 8, 8, flat ? WHITE : mesh_alpha(th->square, 0.9f), mesh_alpha(ink, 0.35f), 1);
    int r0 = (int)floorf_(hv->pos) - 1, r1 = (int)floorf_(hv->pos + hv->shown) + 1;
    for (int r = r0; r <= r1; r++) {
        int root = -1;
        for (int c = 0; c < cols; c++) { int note = note_of(r * cols + c); if (floormod(note - key(), 12) == 0) { root = note; break; } }
        if (root < 0) continue;
        float y = row_top(r, hv->pos) + cell / 2;
        if (y < hv->top + 10 || y > hv->bot - 10) continue;
        unsigned c = note_colour(root);
        unsigned mark = c && !flat ? c : ink;
        g_line(bar_x + 5, y, bar_x + BAR - 5, y, mesh_alpha(mark, 0.8f), 2);
        char name[8];
        note_name(name, root, 1);
        /* The octave beside its mark: above it upright, to its right turned. */
        if (turned) g_text_at(bar_x + BAR / 2, y, 6, 0, 10, mark, MESH_TEXT_LEFT | MESH_TEXT_BOLD, name);
        else g_text_at(bar_x + BAR / 2, y, 0, -9, 10, mark, MESH_TEXT_CENTER | MESH_TEXT_BOLD, name);
    }
    }
    hv = &halves[0];
}

/* Lit outlines on squares a shade apart from the background; a sounding square full of light. */
static void draw(void) {
    layout();
    clamp_pos();
    const Theme* th = theme();
    int flat = eink;
    mesh_begin();
    g_clear(flat ? WHITE : th->background);
    float radius = cell * 0.08f, side = cell - gap;
    unsigned light = flat ? BLACK : th->light;
    /* The pitches sounding now (held or latched), for marking the same notes elsewhere. */
    int sounding = 0, echo = (int)set_[S_ECHO];
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used) sounding |= 1 << floormod(fingers[k].note, 12);
    for (int k = 0; k < nlatched; k++) sounding |= 1 << floormod(latched[k].note, 12);
    /* Up each column, unlocked: each column outlined, as something to drag. */
    for (int h = 0; h < nhalves; h++) {
    hv = &halves[h];
    if (columns() && !locked())
        for (int c = 0; c < cols; c++)
            g_rect(left + (float)c * cell - gap / 2 + 1, hv->top + 2, cell - 2, hv->bot - hv->top - 4, radius + gap / 2,
                   flat ? 0 : mesh_alpha(th->hot, 0.06f), mesh_alpha(flat ? BLACK : th->hot, 0.55f), 2);
    for (int c = 0; c < cols; c++) {
      float p = scroll_of(c);
      int r0 = (int)floorf_(p) - 1, r1 = (int)floorf_(p + hv->shown) + 1;
      for (int r = r0; r <= r1; r++) {
        int n = r * cols + c;
        float ox = square_x(n), oy = square_y(n);
        if (oy > hv->bot || oy + side < hv->top) continue;
        int note = note_of(n);
        int root = floormod(note - key(), 12) == 0;
        int off = chromatic() && !in_scale(note);
        int on = is_held(n), kept = is_kept(n);
        float glow = on || kept ? 1 : glow_of(n);
        unsigned colour = flat ? 0 : note_colour(note);
        unsigned lit = colour ? colour : light;
        /* The square: a shade apart from the background, tinted with its note's colour; a note
           outside the scale sinks nearly into the background. */
        if (!flat) {
            c_rect(ox, oy, side, side, radius, off ? mesh_alpha(th->square, 0.3f) : th->square, 0, 0);
            if (colour && glow < 1) c_rect(ox, oy, side, side, radius, mesh_alpha(colour, th->bright ? 0.35f : 0.22f), 0, 0);
        }
        /* The light: an outer bloom, then the fill. */
        if (glow > 0) {
            if (!flat && !th->bright) c_rect(ox - 6, oy - 6, side + 12, side + 12, radius + 6, mesh_alpha(lit, 0.28f * glow), 0, 0);
            c_rect(ox, oy, side, side, radius, mesh_alpha(lit, (flat ? 1 : 0.92f) * glow), 0, 0);
        }
        /* The outline: the root's fully lit, the scale's soft, a note outside the scale faint. */
        unsigned edge;
        if (on || glow > 0.6f) edge = flat ? BLACK : colour ? WHITE : th->hot;
        else if (off) edge = mesh_alpha(light, flat ? 0.25f : 0.10f);
        else if (colour) edge = mesh_alpha(colour, root ? 1 : 0.8f);
        else edge = root ? light : mesh_alpha(light, flat ? 0.8f : 0.38f);
        if (root && !flat && !on && !th->bright)
            c_rect(ox - 3, oy - 3, side + 6, side + 6, radius + 3, 0, mesh_alpha(colour ? colour : light, 0.18f), 6);
        c_rect(ox, oy, side, side, radius, 0, edge, off ? 1.0f : root ? 2.5f : 1.5f);
        /* The same note elsewhere: not lit like a pressed square, only marked. */
        int same = echo != ECHO_OFF && !on && !kept && glow < 0.5f && ((sounding >> floormod(note, 12)) & 1);
        unsigned ec = flat ? BLACK : colour ? colour : th->hot;
        if (same) switch (echo) {
        case ECHO_RING: {
            float in = side * 0.12f;
            c_rect(ox + in, oy + in, side - 2 * in, side - 2 * in, radius * 0.7f, 0, mesh_alpha(ec, 0.95f), 3);
            break;
        }
        case ECHO_GLOW:
            if (!flat) c_rect(ox - 8, oy - 8, side + 16, side + 16, radius + 8, mesh_alpha(ec, 0.22f), 0, 0);
            c_rect(ox, oy, side, side, radius, 0, mesh_alpha(ec, 0.9f), 2.5f);
            break;
        }
        /* A latched square: a dot in its corner, as long as it is held. */
        if (kept && !on && in_half(oy + side * 0.16f))
            g_circle(ox + side * 0.84f, oy + side * 0.16f, side * 0.06f,
                        glow > 0.5f ? (flat ? WHITE : th->on_light) : (flat ? BLACK : th->hot), 0, 0);
        if (set_[S_LABELS] != 0 && in_half(oy + side / 2)) {
            float size = side * 0.16f;
            unsigned tc = glow > 0.5f ? (flat ? WHITE : colour ? 0x101010FFu : th->on_light)
                        : mesh_alpha(colour ? colour : light, off ? 0.3f : 0.85f);
            char name[16];
            note_name(name, note, root);
            g_text(ox + side / 2, oy + side / 2 + size * 0.35f, size, tc, MESH_TEXT_CENTER | (root ? MESH_TEXT_BOLD : 0), name);
        }
      }
    }
    }
    hv = &halves[0];
    /* Split: the line between the halves. */
    if (nhalves > 1) {
        float y = (halves[1].bot + halves[0].top) / 2;
        g_line(left - gap / 2, y, left + cell * (float)cols - gap / 2, y, mesh_alpha(flat ? BLACK : th->hot, 0.6f), 2);
    }
    /* A halo that follows each finger. */
    if (!flat && !th->bright)
        for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used)
            g_glow(fingers[k].x, fingers[k].y, cell * 0.9f, mesh_alpha(th->hot, 0.5f), mesh_alpha(th->light, 0));
    draw_bar(th);
    draw_sides(th);
    if (editing()) ed_draw(th);
    mesh_end();
    dirty = 0;
}

/* ---- the interface ------------------------------------------------------------------------------- */
/* Upright or sideways: Auto turns it sideways when the surface is wider than it is tall. */
static void turn_to(float sw, float sh) {
    SW = sw; SH = sh;
    int t = (int)set_[S_TURN];
    turned = t == TURN_SIDEWAYS || (t == TURN_AUTO && sw > sh * 1.05f);
    W = turned ? sh : sw;
    H = turned ? sw : sh;
}

MESH_EXPORT(init) void init(float width, float height, int flags, double t) {
    eink = flags & MESH_EINK; now = t;
    turn_to(width, height);
    layout();
    draw();
}

MESH_EXPORT(instrument) void instrument(int part, float center_note) {
    if (part < 0 || part > 1) return;
    center[part] = center_note;
    if (part == 1) have_top = 1;
    layout();
    dirty = 1;
}

void stop(void);

MESH_EXPORT(setting) void setting(int index, double value) {
    if (index < 0 || index >= S_COUNT) return;
    set_[index] = value;
    if (index == S_LATCH) set_latch(value != 0);
    if (index == S_TURN) turn_to(SW, SH);
    if (index == S_ZONE_EDIT && value != 0) stop();      /* opening the editor lets go of what plays */
    layout();
    clamp_pos();
}

MESH_EXPORT(touch) void touch(int id, int phase, float x, float y, float size, float force, double t) {
    (void)size; (void)force;
    now = t;
    /* The zone editor takes every touch while it is open, on the surface as it is (never turned). */
    if (editing()) { ed_touch(id, phase, x, y); dirty = 1; return; }
    if (turned) { float ux = y, uy = H - x; x = ux; y = uy; }   /* the touch, as on the upright grid */
    id += 1;    /* 0 marks a free side touch */
    if (phase == MESH_DOWN) {
        if (find(id)) up(id);
        layout();
        if (!side_down(id, x, y)) { if (columns() && !locked()) col_down(id, x, y); else down(id, x, y); }
    } else if (phase == MESH_MOVE) {
        SideTouch* s = side_find(id);
        if (s) {
            if (s->control == C_BAR) bar_drag(s, y);
            else if (s->control == C_COL) col_drag(s, y);
            else side_drag(s, frac(s, y));
            return;
        }
        Finger* f = find(id);
        if (!f) return;
        f->x = x; f->y = y;
        if (sliding()) slide_to(f, x, y);
        send(f);
    } else {
        SideTouch* s = side_find(id);
        if (s) side_up(s);
        else up(id);
    }
    dirty = 1;
}

MESH_EXPORT(tick) void tick(double t) {
    float dt = last_tick < 0 ? 0 : (float)((t - last_tick) / 1000.0);
    if (dt < 0) dt = 0;
    if (dt > 0.25f) dt = 0.25f;
    last_tick = t;
    now = t;
    /* The slow position trails the finger by about 0.12 s: a wiggle is faster than that. */
    float k = 1 - expf_(-dt / 0.12f);
    int any = 0;
    for (int j = 0; j < MAX_FINGERS; j++) if (fingers[j].used) {
        Finger* f = &fingers[j];
        f->sx += (f->x - f->sx) * k;
        f->sy += (f->y - f->sy) * k;
        send(f);
        any = 1;
    }
    /* A let-go square's light fades, as the sound does (about half a second; the MESH does not say
       how loud a voice still is). */
    for (int r = 0; r < MAX_RING; r++) if (ring[r].glow > 0) {
        ring[r].glow = eink ? 0 : ring[r].glow * expf_(-dt / 0.25f) - dt * 0.2f;
        if (ring[r].glow < 0.004f) ring[r].glow = 0;
        any = 1;
    }
    if (any && !eink) dirty = 1;
    if (dirty) draw();
}

MESH_EXPORT(stop) void stop(void) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used) { let_go(&fingers[k]); fingers[k].used = 0; }
    for (int k = 0; k < MAX_FINGERS; k++) side_touches[k].id = 0;
    for (int c = 0; c < C_COUNT; c++) touched[c] = 0;
    nspent = 0;
    holding = 0;
    side_bend = 0;
    release_latched();
}
