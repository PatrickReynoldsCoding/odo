/*
 * The Grid as a MESH controller: square keys, played landscape. It is laid out upright (as below)
 * and drawn a quarter turned (Flip 90° draws it upright instead, its text the right way up too): the upright grid's bottom (its low notes) on the left, its
 * right edge at the bottom of the screen. So the rows of the upright grid are the screen's
 * columns, the notes climbing up each and on to the next to the right. Each square a note of a
 * scale, or every semitone with the notes outside the scale darkened. A square is lit while its
 * note sounds, and fades after.
 *
 * A finger in its square: its height is the dynamics (loud at the top) and its place across the
 * tone (bright on the right), from where it presses, as it moves. Out past a line just beyond the
 * square's edge, sliding left/right and up/down each do what they are set to (loudness, tone, bend
 * or volume), from that line. A quick wiggle any way is vibrato. The Touch panel (touchEdit) sets
 * all of it, with a square to play.
 *
 * Square k counts along the rows from the bottom, endlessly: the rows run on past the top and
 * bottom of the screen, and the scroll bar (a thin strip the height of the screen, with each
 * root's octave marked) slides them up and down under the finger, as the touch pad's note strip
 * does. At full size the squares fill the width, edge to edge. Either axis can be flipped.
 *
 * Modes (as the rail names them, on the landscape screen): Wrap, the notes climbing up each
 * column and on to the next; Intervals, each row climbing the scale left to right and each row an
 * interval (3rds to octaves) above the one below; Custom, any square any note. Or Sliding rows:
 * the rows are each a scale of their own, and each
 * scrolls on its own. A lock (the Lock button, or the rail) keeps them still while you play;
 * unlocked, a drag on a column slides it and plays nothing.
 * Controls can sit in strips either side: Latch, Hold, Bend, Tone. The colours can mark notes
 * by their place in the key (the 4th always blue, say).
 *
 * Or, Your own notes: any square can be any note. Edit your own notes opens a panel: tap a square
 * (it plays), then pick its note's name, octave, or a semitone up or down. Until the first change
 * every square is as Along the rows has it; that change fixes them all where they are. 8 columns
 * by 16 rows can hold notes, kept 6 to a hidden setting (a byte each, 255: as the rows).
 *
 * Slide between notes: a finger plays whichever square it is on. Move onto another and its note
 * starts (the last one stops), without lifting: no strike zones, nothing across or up and down,
 * no vibrato; every note as loud as the Dynamics fader says.
 *
 * A port of odo's built-in Grid (GridUi.kt, KeyGrid.kt), one section (no split).
 *
 * The Hex keyboard (controllers/hex/hex.c) is this file with hexagons for keys: it defines KEYS_HEX
 * (and its own settings, after these, and words), includes this file, and then gives the functions
 * under "the keys' shape" below. Everything else, every way a finger plays, is the same code.
 */
#include "mesh.h"

/* What a key is called in the Touch panel's words. */
#ifndef CELL
#define CELL "square"
#define CELL_UPPER "SQUARE"
#endif
/* The most keys across a row (here: the squares Your own notes can hold). */
#ifndef KEYS_MAX_COLS
#define KEYS_MAX_COLS 8
#endif
/* For what one shape of key uses and the other does not. */
#define SHARED __attribute__((unused))

#define MAX_FINGERS 10
#define MAX_VOICES 15          /* the MIDI channels MPE has for notes */
#define MAX_RING 32
#define MAX_SIDE 10
#define CUST_COLS 8            /* Your own notes: the squares that can hold one, 8 x 16 */
#define CUST_ROWS 16
#define CUST_WORDS 22          /* 128 squares, 6 to a setting: 8 bits each, inside a double's 53 */
#define CUST_ANY 255           /* a square as Along the rows has it */
#define NOTES_EVEN 281474976710655.0   /* 2^48 - 1: six squares, all CUST_ANY */

/* ---- settings (the manifest's, in order) ---------------------------------------------------- */
enum {
    S_COLS, S_LAYOUT, S_LOCK, S_SPLIT, S_SPLIT_SIZE, S_TOP_WAY, S_TOP_NOTES, S_TOP_SCALE, S_TOP_KEY, S_TOP_OCTAVE, S_SIZE, S_SPACING, S_FLIP_X, S_FLIP_Y, S_CHROMATIC, S_SCALE, S_KEY, S_OCTAVE,
    /* Sliding out of a square: up/down (S_ACROSS, the upright grid's across) and left/right
       (S_UPDOWN, its up and down); the Touch panel open; inside a square; vibrato */
    S_ACROSS, S_UPDOWN, S_BEND_RANGE, S_BEND_SNAP, S_TOUCH_EDIT, S_IN_DYN, S_DYN_LOW, S_VIBRATO, S_LABELS, S_THEME, S_COLOURS, S_ECHO,
    S_LATCH, S_LATCHMODE, S_BAR, S_SIDE0, S_VIB_SENS = S_SIDE0 + 6,
    /* hidden: the Dynamics fader */
    S_DYN,
    /* the two ways' shaping, set in the Touch panel: amount and sensitivity in %, flip */
    S_AX_AMOUNT, S_AX_SENS, S_AX_FLIP, S_UD_AMOUNT, S_UD_SENS, S_UD_FLIP,
    /* Slide between notes: a finger plays the square under it, whichever, until it lifts */
    S_SLIDE,
    /* Your own notes: the editor open, and the notes (hidden) */
    S_NOTE_EDIT, S_NOTES0,
    /* the loudness at the top of a square (the bottom's is S_DYN_LOW), in % */
    S_DYN_HIGH = S_NOTES0 + CUST_WORDS,
    /* Intervals: how far apart the rows of notes are */
    S_INTERVAL,
    /* left/right in a square sets the tone (up/down, the dynamics: S_IN_DYN) */
    S_IN_TONE,
    /* Slide between notes: the glide from one square's note to the next, 0 (each a new note) to 1 */
    S_GLIDE,
    /* which ways it glides (on screen); the other way, the note jumps to the next square's */
    S_GLIDE_LR, S_GLIDE_UD,
    /* Flip 90°: drawn upright (rows across the screen, low notes at the bottom) instead of turned */
    S_FLIP90,
    /* Only slide between notes: slide on, and nothing else a finger does (in a square, past it,
       vibrato); the others keep their settings for when it is off. Its button's place. */
    S_SLIDE_ONLY, S_SIDE_SLIDE,
    /* The controls moved off their gutters (hidden): 0 in the gutter, else where (packed: place()) */
    S_PLACE0, S_PLACE_RESET = S_PLACE0 + 7,
    /* Two fingers on a control move, size and turn it */
    S_MOVE_CTL,
    /* Inside a square (its dynamics and tone) and sliding out past it, each on or off as a whole
       (their own settings kept); the buttons' places; the two new controls' moved places */
    S_INSIDE_ON, S_OUT_ON, S_SIDE_INSIDE, S_SIDE_OUT, S_PLACE7, S_PLACE8,
    /* The Hex keyboard's own, after these (hex.c) */
#ifdef KEYS_EXTRA_SETTINGS
    KEYS_EXTRA_SETTINGS
#endif
    S_COUNT
};
static double set_[S_COUNT] = {3, 0, 1, 0, 50, 0, 0, 0, 0, 0, 100, 9, 0, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 1, 30, 2, 0, 0, 0, 1, 0, 0, 2, 1, 0, 0, 0, 1, 2, 100,
                               1,
                               100, 100, 0, 100, 100, 0, 0, 0,
                               [S_NOTES0 ... S_NOTES0 + CUST_WORDS - 1] = NOTES_EVEN, [S_DYN_HIGH] = 100, [S_INTERVAL] = 2, [S_IN_TONE] = 1, [S_GLIDE_LR] = 1, [S_GLIDE_UD] = 1, [S_MOVE_CTL] = 1, [S_INSIDE_ON] = 1, [S_OUT_ON] = 1
#ifdef KEYS_EXTRA_DEFAULTS
                               , KEYS_EXTRA_DEFAULTS
#endif
                               };
/* Bend range: 0 toward the next note; else semitones at a whole square's slide (as the gyro's). */
static const float BEND_RANGES[7] = {0, 1, 2, 3, 5, 7, 12};

enum { T_NONE, T_LOUDNESS, T_TONE, T_BEND, T_VOLUME };
enum { LATCH_CHORD, LATCH_TAP };
enum { C_LATCH, C_HOLD, C_BEND, C_TONE, C_DYN, C_LOCK, C_SLIDE, C_INSIDE, C_OUT, C_COUNT };   /* the side controls */
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
static const unsigned DEGREE[12] SHARED = {
    0xFF5C5CFFu, 0xFF9F43FFu, 0xFF9F43FFu, 0xFFD43BFFu, 0xFFD43BFFu, 0x4DABF7FFu,
    0x8CA0B8FFu, 0x51CF66FFu, 0x9775FAFFu, 0x9775FAFFu, 0xF06595FFu, 0xF06595FFu};
/* By note name, whatever the key: round the circle of fifths, C red. */
static const unsigned NAMED[12] SHARED = {
    0xFF5C5CFFu, 0x9775FAFFu, 0xFFA94DFFu, 0xF06595FFu, 0xFFE066FFu, 0xE599F7FFu,
    0x69DB7CFFu, 0xFF8787FFu, 0x38D9A9FFu, 0xFFC078FFu, 0x4DABF7FFu, 0xC0EB75FFu};
static const Theme* theme(void) { int k = (int)set_[S_THEME]; return &THEMES[k < 0 || k > 5 ? 0 : k]; }

/* ---- the surface ---------------------------------------------------------------------------- */
static void turn_to(float sw, float sh);
/* The Grid is laid out upright, W x H, and always turned sideways: everything it draws and every
   touch is turned a quarter: the bottom of the upright grid (its low notes) on
   the left, its right edge at the bottom. SW x SH is the surface itself. */
static float W = 400, H = 700, SW = 400, SH = 700;
static int turned;
static int reset_places;           /* Put the moved controls back: asked, not yet done */
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
static float cell = 50, gap = 4, left = 0;               /* cell: a key's pitch across */
static float row_h = 50;                                   /* a row's pitch: cell, for squares */
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
/* A control: its rect on the upright grid (in its gutter, or, moved, as it is before it is moved,
   sized and turned onto the surface: free, with its middle cx, cy on the surface). */
typedef struct { int control; float x, y, w, h; int free; float cx, cy, s, a; } SideRect;
static SideRect sides[MAX_SIDE];
static int nsides;

static int SHARED chromatic(void) { return hv->chrom; }
static int key(void) { return hv->key; }
/* Unflipped, the notes climb right to left along each row (for the right hand) and rows climb up. */
static int mirrored(void) { return set_[S_FLIP_X] == 0; }
static int flipped_y(void) { return hv->flip; }
static int split_on(void) { return set_[S_SPLIT] != 0; }
/* Up each column: every column runs up the scale on its own, and scrolls on its own (while unlocked). */
static int columns(void) { return (int)set_[S_LAYOUT] == 1; }
/* Custom: each square the note it was given. */
static int custom(void) { return (int)set_[S_LAYOUT] == 2; }
/* Intervals: along each row the scale climbs a note at a time (on screen: left to right); each
   row is the interval above the one below it (on screen: the next row up). */
static int SHARED intervals(void) { return (int)set_[S_LAYOUT] == 3; }
static const int INTERVAL_SEMIS[5] = {4, 5, 7, 9, 12};      /* 3rds, 4ths, 5ths, 6ths, octaves */
static int locked(void) { return set_[S_LOCK] != 0; }
/* The Lock button only means something with the columns. */
static int side_of(int c) {
    if (c == C_LOCK && !columns()) return SIDE_OFF;
    return (int)set_[c == C_SLIDE ? S_SIDE_SLIDE : c == C_INSIDE ? S_SIDE_INSIDE : c == C_OUT ? S_SIDE_OUT : S_SIDE0 + c];
}
static int is_button(int c) { return c <= C_HOLD || c == C_LOCK || c >= C_SLIDE; }
/* The toggles (Slide only, Inside, Slide out): a double tap changes one; held, it is the other way
   only while held (flipped[c]). */
static int is_toggle(int c) { return c >= C_SLIDE; }
static int flipped[C_COUNT];
static int toggle_setting(int c) { return c == C_SLIDE ? S_SLIDE_ONLY : c == C_INSIDE ? S_INSIDE_ON : S_OUT_ON; }
static int toggle_on(int c) { return (set_[toggle_setting(c)] != 0) != (flipped[c] != 0); }
/* Only slide between notes. */
static int slide_only(void) { return toggle_on(C_SLIDE); }
/* Inside a square and sliding out past it: on as a whole. */
static int inside_on(void) { return toggle_on(C_INSIDE) && !slide_only(); }
static int out_on(void) { return toggle_on(C_OUT); }

/* ---- a control moved off its gutter --------------------------------------------------------------- */
/* Where a moved control is, on the surface: its middle (as a share of the surface), its size and
   its turn (degrees). Kept in one number: 0 is "in its gutter". */
typedef struct { float fx, fy, scale, deg; } Place;
static int place_index(int c) { return c < 7 ? S_PLACE0 + c : S_PLACE7 + (c - 7); }
static int placed(int c) { return set_[place_index(c)] != 0; }
static Place place_of(int c) {
    double v = set_[place_index(c)] - 1;
    Place p = {0.5f, 0.5f, 1, 0};
    if (v < 0) return p;
    unsigned long long u = (unsigned long long)v;
    p.fx = (float)(u % 4096) / 4095.0f; u /= 4096;
    p.fy = (float)(u % 4096) / 4095.0f; u /= 4096;
    p.scale = 0.4f + (float)(u % 512) * 0.01f; u /= 512;
    p.deg = (float)(u % 360);
    return p;
}
static double pack_place(Place p) {
    unsigned long long x = (unsigned long long)(clampf(p.fx, 0, 1) * 4095 + 0.5f), y = (unsigned long long)(clampf(p.fy, 0, 1) * 4095 + 0.5f);
    unsigned long long s = (unsigned long long)((clampf(p.scale, 0.4f, 5) - 0.4f) / 0.01f + 0.5f);
    int d = (int)(p.deg + 0.5f) % 360;
    if (d < 0) d += 360;
    return 1 + (double)(x + 4096ull * (y + 4096ull * (s + 512ull * (unsigned long long)d)));
}

/* The strip of one side, from the bottom up as the thumb reaches: buttons their own height,
   sliders sharing the rest, none taller than 300. */
static void strip(int where, float x) {
    int list[C_COUNT], n = 0, nb = 0, ns = 0;
    for (int c = 0; c < C_COUNT; c++) if (side_of(c) == where && !placed(c)) { list[n++] = c; if (is_button(c)) nb++; else ns++; }
    if (!n) return;
    float top = SIDE_TOP, bot = H - 10;
    float spare = (bot - top) - 52.0f * (float)nb - 10.0f * (float)(n - 1);
    float sh = ns ? clampf(spare / (float)ns, 40, 300) : 0;
    float total = 52.0f * (float)nb + sh * (float)ns + 10.0f * (float)(n - 1);
    float y = bot - total;
    for (int k = 0; k < n && nsides < MAX_SIDE; k++) {
        float h = is_button(list[k]) ? 52 : sh;
        SideRect r = {list[k], x + 6, y, STRIP - 12, h, 0, 0, 0, 1, 0};
        sides[nsides++] = r;
        y += h + 10;
    }
}

static int index_of(int note);
static int interval_step(void);
static void keep(int index, double v);

/* ---- the keys' shape: squares here, hexagons in hex.c (KEYS_HEX) ------------------------------- */
/* The keys' size and place across the room gx..gx+gw: cell, gap, left and row_h. */
static void fit(float gx, float gw);
/* The current half's shift (kshift) that puts target (a MIDI note) in its middle, unscrolled. */
static int shift_for(int target);
static int in_scale(int note);
static int note_at(int k);
static int note_of(int k);
static void clamp_pos(void);
static float scroll_of(int c);
static int column_at(float x);
static int square_at(float x, float y);
static float square_x(int k);
static float square_y(int k);
/* A note as it sounds, a MIDI note with fractions (a tuning's notes are its own steps). */
static float pitch_of(int note);
/* The note the Touch panel's square plays: the one nearest the middle of the instrument. */
static int demo_note(void);
static unsigned note_colour(int note);
static void note_name(char* out, int note, int octave);

static void layout(void) {
    cols = (int)set_[S_COLS];
    if (cols < 2) cols = 2;
    if (cols > KEYS_MAX_COLS) cols = KEYS_MAX_COLS;
    /* From the outside in, on each side: the scroll bar, then the strip of controls. */
    /* Up each column there is no scroll bar: each column is dragged on its own instead. */
    int nl = 0, nr = 0, bar = columns() ? SIDE_OFF : (int)set_[S_BAR];
    for (int c = 0; c < C_COUNT; c++) if (!placed(c)) { if (side_of(c) == SIDE_LEFT) nl = 1; if (side_of(c) == SIDE_RIGHT) nr = 1; }
    float lw = (bar == SIDE_LEFT ? BAR : 0) + (nl ? STRIP : 0), rw = (bar == SIDE_RIGHT ? BAR : 0) + (nr ? STRIP : 0);
    bar_x = bar == SIDE_LEFT ? 0 : bar == SIDE_RIGHT ? W - BAR : -1;
    nsides = 0;
    strip(SIDE_LEFT, bar == SIDE_LEFT ? BAR : 0);
    strip(SIDE_RIGHT, W - rw);
    /* The moved ones, over the grid: each its gutter size (a slider 240 long), then sized and turned. */
    for (int c = 0; c < C_COUNT && nsides < MAX_SIDE; c++) {
        if (!placed(c) || side_of(c) == SIDE_OFF) continue;
        Place p = place_of(c);
        SideRect r = {c, 0, 0, STRIP - 12, is_button(c) ? 52 : 240, 1, p.fx * SW, p.fy * SH, p.scale, p.deg * 3.14159265f / 180};
        sides[nsides++] = r;
    }
    float gx = lw, gw = W - lw - rw;
    if (gw < 40) gw = 40;
    fit(gx, gw);
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
        a->shown = (a->bot - a->top) / row_h;
        a->mask = MASKS[sc < 0 || sc > 8 ? 0 : sc];
        a->ndeg = 0;
        for (int d = 0; d < 12; d++) if ((a->mask >> d) & 1) a->degs[a->ndeg++] = d;
        hv = a;
        /* At no scroll, the middle of the half holds the note nearest its instrument's middle. */
        float c = bottom || !have_top ? center[0] : center[1];
        int oct = (int)set_[S_OCTAVE] + (bottom ? 0 : (int)set_[S_TOP_OCTAVE]);
        a->kshift = shift_for(roundi(c + 12.0f * (float)oct));
    }
    hv = &halves[0];
    dirty = 1;
}

/* The half at upright height y, or -1 on the line between. */
static int half_at(float y) {
    if (nhalves < 2) return 0;
    return y >= halves[0].top ? 0 : y <= halves[1].bot ? 1 : -1;
}

static void keep(int index, double v);
/* Your own notes: square i's stored note (or CUST_ANY), and setting it. */
static int cust_get(int i) {
    unsigned long long w = (unsigned long long)set_[S_NOTES0 + i / 6];
    return (int)((w >> (8 * (i % 6))) & 0xFF);
}
static void cust_put(int i, int v) {
    unsigned long long w = (unsigned long long)set_[S_NOTES0 + i / 6];
    int s = 8 * (i % 6);
    w = (w & ~(0xFFull << s)) | ((unsigned long long)(v & 0xFF) << s);
    if ((double)w != set_[S_NOTES0 + i / 6]) keep(S_NOTES0 + i / 6, (double)w);
}
/* The stored place of square k (row by row, 8 to a row), or -1 outside the rows that hold notes. */
static int cust_index(int k) {
    int r = floordiv(k, cols), c = floormod(k, cols);
    return r < 0 || r >= CUST_ROWS ? -1 : r * CUST_COLS + c;
}
/* Your own notes are kept as the bottom grid has them; the top's are its octave apart. */
static int top_shift(void) { return half_index() == 1 ? 12 * (int)set_[S_TOP_OCTAVE] : 0; }

/* Intervals: how many notes of the scale (or semitones) apart the rows are. */
static int SHARED interval_step(void) {
    int i = (int)set_[S_INTERVAL];
    int semis = INTERVAL_SEMIS[i < 0 || i > 4 ? 2 : i];
    int s = index_of(key() + semis) - index_of(key());
    return s < 1 ? 1 : s;
}

#ifndef KEYS_HEX
/* Full size: the squares reach both edges, with the spacing for a margin: a row's pitch p, its gap
   g*p, cols*p + g*p across. */
static void fit(float gx, float gw) {
    float g = clampf((float)set_[S_SPACING] / 100.0f, 0.02f, 0.3f);
    float size = clampf((float)set_[S_SIZE] / 100.0f, 0.5f, 1);
    cell = size * gw / ((float)cols + g);
    gap = cell * g;
    left = gx + (gw - (cell * (float)cols + gap)) / 2 + gap;
    row_h = cell;
}

static int shift_for(int target) {
    int mid = columns() ? (int)floorf_(hv->shown / 2)
            : intervals() ? (int)floorf_(hv->shown / 2) + (cols / 2) * interval_step()
            : (int)floorf_(hv->shown / 2) * cols + cols / 2;
    return index_of(target) - mid;
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
static float pitch_of(int note) { return (float)note; }

/* The note of square k (k counts along the rows from row 0, which is at the bottom unscrolled), or
   -1 for none (Custom, outside its 16 rows). Up each column, the note is the row's alone: every
   column holds the same notes, scrolled its own way. */
static int note_of(int k) {
    if (intervals()) {
        /* k counts along the upright rows (the screen's columns): its upright row is the place along
           the screen's row, its column (counted) the screen's row. */
        int note = note_at(floordiv(k, cols) + floormod(k, cols) * interval_step() + hv->kshift);
        return note < 0 ? 0 : note > 127 ? 127 : note;
    }
    if (custom()) {
        int i = cust_index(k);
        if (i < 0) return -1;
        int v = cust_get(i);
        if (v != CUST_ANY) { int note = v + top_shift(); return note < 0 ? 0 : note > 127 ? 127 : note; }
    }
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
        if (custom()) { float hi = (float)CUST_ROWS - hv->shown; hv->pos = clampf(hv->pos, 0, hi > 0 ? hi : 0); }
        else if (intervals()) clamp_one(&hv->pos, 1);
        else if (!columns()) clamp_one(&hv->pos, cols);
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
#endif

/* ---- fingers and their notes ------------------------------------------------------------------ */
typedef struct {
    int used, id, voice, n, note, half;   /* half: which grid it plays (the top is part 1) */
    float x0, y0, x, y, sx, sy;   /* where it came down, where it is, and its slow position */
    float rx, ry, rs;             /* its square (upright: left, top, side) */
    float ux, uy;                 /* the way it wiggles, learnt as it goes (a unit vector, either sign) */
    float glide;                  /* sliding with a glide: semitones toward the squares beside it */
    float strike;                 /* how loud it started */
    float press0, press;          /* how big or hard the touch was when it came down, and is now */
#ifdef KEYS_FINGER_EXTRA
    KEYS_FINGER_EXTRA             /* what the Hex keyboard keeps of a finger besides */
#endif
} Finger;
#define DEMO_N (-99999)           /* the square in the Touch panel, not one of the grid's */
/* The touch now being handled: its size, or its force when the MESH has that (0: neither). */
static float touch_press;

/* The keys' shape, again: the note a bend aims at, sliding out of finger f's key across (1) or up
   and down (0), toward dir (1: right or up, upright); and with Slide between notes' glide, how far
   toward the keys around it the finger at (x, y) on key n has moved the note, in semitones. */
static int bend_to(const Finger* f, int across, int dir);
static float glide_semis(const Finger* f, int n, float x, float y, float g);

/* Sliding with a glide: none of the way in the middle of the square, half at the border, all of it
   at the next square's middle. Glide 1: evenly; less: held on the note longer, the move squeezed
   toward the border. */
static float glide_curve(float t, float g) { return g >= 0.995f ? t : clampf((t - (1 - g) / 2) / g, 0, 1); }

#ifndef KEYS_HEX
static int bend_to(const Finger* f, int across, int dir) {
    /* The panel's square: the next note of the scale, the way the grid's own squares would go
       (across: as beside() runs, mirrored or not; up and down: as the rows climb). */
    if (f->n == DEMO_N) return note_at(index_of(f->note) + (across ? (mirrored() ? -dir : dir) : (flipped_y() ? -dir : dir)));
    /* Toward the note you drag to: across, the next square or the one before (unflipped the next
       is to the left); up and down, the row above or below. */
    return across ? note_of(beside(f->n, dir)) : note_of(f->n + dir * (flipped_y() ? -cols : cols));
}

static float glide_semis(const Finger* f, int n, float x, float y, float g) {
    float half_ = (cell - gap) / 2;
    float du = (x - (f->rx + half_)) / cell, dv = (y - (f->ry + half_)) / cell;
    float semis = 0;
    /* across the upright grid (on screen: up and down): the square beside it */
    int across = beside(n, du >= 0 ? 1 : -1);
    if (set_[S_GLIDE_UD] != 0 && note_of(across) >= 0) semis += glide_curve(absf(du), g) * (float)(note_of(across) - f->note);
    /* up and down the upright grid (on screen: left and right): the next row */
    int row = n + ((dv >= 0) != (flipped_y() != 0) ? -cols : cols);
    if (set_[S_GLIDE_LR] != 0 && note_of(row) >= 0) semis += glide_curve(absf(dv), g) * (float)(note_of(row) - f->note);
    return semis;
}

static int demo_note(void) {
    Half* was = hv;
    hv = &halves[0];
    int note = note_at(index_of(roundi(center[0] + 12.0f * (float)set_[S_OCTAVE])));
    hv = was;
    return note < 0 ? 0 : note > 127 ? 127 : note;
}
#endif
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
/* Editing your own notes: the square chosen (its index k, or -1) and its half; Reset all asked once. */
static int sel = -1, sel_half, resetting;

/* cancel: taken by a two-finger move, it does nothing more; sx, sy: where it is on the surface. */
typedef struct { int id, control, col, half; float from, top, h; float start, y; int cancel; float sx, sy; } SideTouch;
enum { C_COL = 98, C_BAR = 99 };     /* a finger scrolling a column (unlocked), or on the scroll bar */
static SideTouch side_touches[MAX_FINGERS];
static int touched[C_COUNT];

static Finger* find(int id) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used && fingers[k].id == id) return &fingers[k];
    return 0;
}
static int nfingers(void) { int c = 0; for (int k = 0; k < MAX_FINGERS; k++) c += fingers[k].used; return c; }

/* ---- a finger in its square ------------------------------------------------------------------ */
/* The note's own volume for a level: the level itself, so 100% is full. (It also plays as hard as
   the level, for the instruments that listen to that.) */
static float strike_gain(float level) { return clampf(level, 0, 1); }
/* The loudness at the top and the bottom of a square, 0..1, the bottom no louder than the top. */
static float dyn_high(void) { return clampf((float)set_[S_DYN_HIGH] / 100.0f, 0, 1); }
static float dyn_low(void) { float l = clampf((float)set_[S_DYN_LOW] / 100.0f, 0, 1), h = dyn_high(); return l < h ? l : h; }

/* How far past the line round a square, in squares, beyond which sliding counts (0: not past it). */
#define MARGIN 0.15f
static float past(float v) { return v < -MARGIN ? v + MARGIN : v > 1 + MARGIN ? v - 1 - MARGIN : 0; }
/* A point as the screen shows it in the finger's square: across, 0 left to 1 right; down, 0 top
   to 1 bottom. (Upright y up is the screen's right; upright x right is its down.) */
static float across_of(const Finger* f, float y) { return (f->ry + f->rs - y) / f->rs; }
static float down_of(const Finger* f, float x) { return (x - f->rx) / f->rs; }
/* The dynamics at a point: from the bottom's loudness at the foot to the top's (up/down off: the
   top's, wherever it is pressed). */
static float level_at(const Finger* f, float x, float y) {
    (void)y;
    float lo = dyn_low(), hi = dyn_high();
    if (set_[S_IN_DYN] == 0 || !inside_on()) return hi;
    return lo + (hi - lo) * (1 - clampf(down_of(f, x), 0, 1));
}
static float tone_at(const Finger* f, float y) {
    if (set_[S_IN_TONE] == 0 || !inside_on()) return 0.5f;
    return clampf(across_of(f, y), 0, 1);
}

typedef struct { float pitch, pressure, slide, volume; } Expr;

/* What a key's shape adds to how a finger plays: the Hex keyboard takes a touch's size or force as
   pressure, and as how hard it struck. Squares: nothing. */
static void press_shape(const Finger* f, Expr* e);
static void press_strike(Finger* f);
#ifndef KEYS_HEX
static void press_shape(const Finger* f, Expr* e) { (void)f; (void)e; }
static void press_strike(Finger* f) { (void)f; }
#endif

/* The two ways out of a square, from how far the finger (slowly) is past the line round it;
   vibrato from the rest. Loudness, tone and volume reach their full change half a square past it
   (or as sensitive as set); bend aims at the neighbouring note, a whole square past. */
#define REACH 0.5f
static void SHARED apply(Expr* e, int target, float d, int across, const Finger* f, float* semis) {
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
        float base = e->pressure;
        float level = clampf(base + (w >= 0 ? 1 - base : base - 0.02f) * w, 0.02f, 1);
        e->pressure = level;
        e->volume = clampf(0.4f + level, 0, 1);
        break;
    }
    case T_TONE: e->slide = clampf(e->slide + 0.5f * w, 0, 1); break;
    case T_BEND: {
        float amount = clampf((absf(v) - 0.08f) / 0.92f, 0, 1);
        int r = (int)set_[S_BEND_RANGE];
        float range = BEND_RANGES[r < 0 || r > 6 ? 0 : r];
        if (range > 0) { *semis += (v >= 0 ? range : -range) * amount; break; }
        /* Toward the note you drag to: across, the next square or the one before (unflipped the
           next is to the left); up and down, the row above or below. */
        int to = bend_to(f, across, v >= 0 ? 1 : -1);
        if (to < 0) to = f->note;
        *semis += (pitch_of(to) - pitch_of(f->note)) * amount;
        break;
    }
    case T_VOLUME: e->volume = clampf(0.7f + (w >= 0 ? 0.3f : 0.7f) * w, 0, 1); break;   /* both ways, from 0.7 */
    default: break;
    }
}

static Expr expression_in(const Finger* f);
#ifdef KEYS_HEX
/* The Hex keyboard's own touch (hex.c): a second finger on a held key pairs with the first, the
   two slid out of it bend the note; a new finger starts with nothing of that; and its panel. */
static float slide_semis(const Finger* f);
static int pair_down(int id, int h, int n, float x, float y);
static int pair_move(int id, float x, float y);
static int pair_up(int id);
static void pair_init(Finger* f);
#endif
/* A finger's notes are its own half's. */
static Expr expression(const Finger* f) {
    Half* was = hv;
    hv = &halves[f->half < nhalves ? f->half : 0];
    Expr e = expression_in(f);
    hv = was;
    return e;
}

static int sliding(void) { return set_[S_SLIDE] != 0 || slide_only(); }

static Expr expression_in(const Finger* f) {
    /* In the square: the dynamics and tone where the finger is (slowly), held at the square's edge
       once it is out; full volume (or Volume's 0.7). */
    Expr e = {0, level_at(f, f->rx + clampf(down_of(f, f->sx), 0, 1) * f->rs, 0), tone_at(f, f->sy), 1};
    if ((int)set_[S_ACROSS] == T_VOLUME || (int)set_[S_UPDOWN] == T_VOLUME) e.volume = 0.7f;
    /* The level also sets the note's own volume, so the dynamics work on every instrument, even
       one that ignores how hard it is played (an organ). */
    e.volume *= strike_gain(e.pressure);
    float semis = 0;
#ifdef KEYS_HEX
    /* The Hex keyboard bends its own way: two fingers slid out of a key (hex.c, slide_semis). */
    semis += slide_semis(f);
#else
    /* Out past the line: what each way is set to do; sliding between notes, nothing (the next
       square plays instead). */
    if (!sliding() && out_on()) {
        apply(&e, (int)set_[S_ACROSS], past(down_of(f, f->sx)), 1, f, &semis);
        apply(&e, (int)set_[S_UPDOWN], past(across_of(f, f->sy)), 0, f, &semis);
    }
#endif
    semis += side_bend;
    /* Snap the bend onto semitones, as the gyro's does: the middle of each semitone holds the note
       and the move to the next is squeezed toward the edges (all of it, at full snap). Not the
       vibrato, added after. */
    float g = 1 - clampf((float)set_[S_BEND_SNAP] / 100.0f, 0, 1);
    if (g < 0.995f && semis != 0) {
        float base = floorf_(semis), t = semis - base;
        semis = base + (g < 0.005f ? (t < 0.5f ? 0 : 1) : clampf((t - (1 - g) / 2) / g, 0, 1));
    }
    press_shape(f, &e);
    /* The Dynamics fader: an expression pedal over every note. */
    float dyn = clampf((float)set_[S_DYN], 0, 1);
    e.volume *= dyn;
    e.pressure *= 0.4f + 0.6f * dyn;
    e.slide = clampf(e.slide + side_tone, 0, 1);
    int vi = (int)set_[S_VIBRATO];
#ifdef KEYS_HEX
    float depth = 0;                  /* no wiggle: a Hex key's pitch moves only by its slide */
    (void)vi;
#else
    float depth = slide_only() ? 0 : VIBRATO_DEPTH[vi < 0 || vi > 3 ? 0 : vi];
#endif
    if (depth > 0) {
        /* A wiggle: what the slow position misses, along the way the finger rocks, once it is more
           than the wiggle sensitivity lets by (so a slow move never wobbles the pitch). */
        float sens = clampf((float)set_[S_VIB_SENS] / 100.0f, 0.25f, 4);
        float along = (f->x - f->sx) * f->ux + (f->y - f->sy) * f->uy, th = 4 / sens;
        float h = along > th ? along - th : along < -th ? along + th : 0;
        semis += clampf(h * 0.08f * depth / 0.5f * sens, -depth, depth);
    }
    e.pitch = pitch_of(f->note) + semis + f->glide;
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
    if (n == -100000 || note_of(n) < 0) return;
#ifdef KEYS_HEX
    if (pair_down(id, h, n, x, y)) return;      /* a second finger on a held key: no note of its own */
#endif
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
    f->rx = square_x(n); f->ry = square_y(n); f->rs = cell - gap;
    f->ux = 1; f->uy = 0; f->glide = 0;
    f->press0 = f->press = touch_press;
    f->strike = level_at(f, x, y);
    press_strike(f);
#ifdef KEYS_HEX
    pair_init(f);
#endif
    for (int r = 0; r < MAX_RING; r++) if (ring[r].n == n && ring[r].half == h) ring[r].glow = 0;
    Expr e = expression(f);
    if (e.volume < 1) mesh_note_volume(f->voice, e.volume, now);   /* before: the note starts at it */
    mesh_note_on(f->voice, h, e.pitch, e.pressure, e.slide, f->strike, now);
    dirty = 1;
}

/* Sliding: the finger has moved onto another square (well inside it, so a finger resting on the
   line between two does not flicker between them). Its note stops and the new square's starts,
   on a voice of its own; a held (latched) note there is played again, not doubled. */
/* Sliding with a glide (as the touch pad's): one note follows the finger. Its square is the one
   under it (its note, and its dynamics); toward the square beside it, each way, the pitch moves
   toward that square's note: none of the way in the middle of the square, half at the border, all
   of it at the next square's middle (glide_curve, glide_semis). */
static void glide_to(Finger* f, float x, float y) {
    float g = clampf((float)set_[S_GLIDE], 0.05f, 1);
    Half* was = hv;
    hv = &halves[f->half < nhalves ? f->half : 0];
    int n = square_at(x, y);
    if (n != -100000 && note_of(n) >= 0) {
        if (n != f->n) {
            f->n = n; f->note = note_of(n);
            f->rx = square_x(n); f->ry = square_y(n); f->rs = cell - gap;
        }
        f->glide = glide_semis(f, n, x, y, g);
    }
    hv = was;
    dirty = 1;
}

static void slide_to(Finger* f, float x, float y) {
    int h = half_at(y);
    if (h < 0) h = f->half;
    Half* was = hv;
    hv = &halves[h];
    int n = square_at(x, y);
    if (n != -100000 && note_of(n) < 0) n = -100000;
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
    f->rx = square_x(n); f->ry = square_y(n); f->rs = cell - gap;
    f->glide = 0;
    f->press0 = f->press = touch_press;
    f->strike = level_at(f, x, y);
    press_strike(f);
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

/* Small maths for turning controls (no libm in here). */
static float sinf_(float x) {
    const float PI = 3.14159265f;
    x = x - 2 * PI * floorf_(x / (2 * PI) + 0.5f);              /* -pi..pi */
    float y = 1.27323954f * x - 0.40528473f * x * absf(x);
    return 0.225f * (y * absf(y) - y) + y;
}
static float cosf_(float x) { return sinf_(x + 1.57079633f); }
static float atan2f_(float y, float x) {
    const float PI = 3.14159265f;
    if (x == 0 && y == 0) return 0;
    float ax = absf(x), ay = absf(y), a = (ax > ay ? ay / ax : ax / ay), s2 = a * a;
    float r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
    if (ay > ax) r = PI / 2 - r;
    if (x < 0) r = PI - r;
    return y < 0 ? -r : r;
}

/* A moved control is drawn as it would be in its gutter, then sized and turned about its middle
   onto its place: xf is that control while it is drawn. Its text stays upright. */
static const SideRect* xf;
static void to_surface(float x, float y, float* sx, float* sy) {
    float px = tx(x, y), py = ty(x, y);
    if (!xf) { *sx = px; *sy = py; return; }
    float mx = xf->x + xf->w / 2, my = xf->y + xf->h / 2;
    float dx = (px - tx(mx, my)) * xf->s, dy = (py - ty(mx, my)) * xf->s, c = cosf_(xf->a), n = sinf_(xf->a);
    *sx = xf->cx + dx * c - dy * n;
    *sy = xf->cy + dx * n + dy * c;
}
/* A point on the surface to the upright grid, inside a moved control r (as its gutter rect). */
static void from_surface(const SideRect* r, float sx, float sy, float* x, float* y) {
    float mx = r->x + r->w / 2, my = r->y + r->h / 2, c = cosf_(-r->a), n = sinf_(-r->a);
    float dx = (sx - r->cx) / r->s, dy = (sy - r->cy) / r->s;
    float px = tx(mx, my) + dx * c - dy * n, py = ty(mx, my) + dx * n + dy * c;
    if (turned) { *x = py; *y = H - px; } else { *x = px; *y = py; }
}

static void g_clear(unsigned c) { mesh_clear(c); }
static void g_rect(float x, float y, float w, float h, float r, unsigned fill, unsigned stroke, float sw) {
    if (xf) {
        /* Turned: a polygon, its corners rounded in four steps each. */
        float pts[2 * 20];
        int n = 0;
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        float cxs[4] = {x + w - r, x + w - r, x + r, x + r}, cys[4] = {y + r, y + h - r, y + h - r, y + r};
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 5; i++) {
                float a = (-90.0f + 90.0f * (float)k + 22.5f * (float)i) * 3.14159265f / 180;
                to_surface(cxs[k] + r * cosf_(a), cys[k] + r * sinf_(a), &pts[2 * n], &pts[2 * n + 1]);
                n++;
            }
        mesh_poly(n, pts, fill, stroke, sw * xf->s);
        return;
    }
    if (turned) mesh_rect(H - (y + h), x, h, w, r, fill, stroke, sw);
    else mesh_rect(x, y, w, h, r, fill, stroke, sw);
}
static void g_circle(float x, float y, float r, unsigned fill, unsigned stroke, float sw) {
    float sx, sy; to_surface(x, y, &sx, &sy);
    float k = xf ? xf->s : 1;
    mesh_circle(sx, sy, r * k, fill, stroke, sw * k);
}
static void g_line(float x1, float y1, float x2, float y2, unsigned c, float w) {
    float ax, ay, bx, by; to_surface(x1, y1, &ax, &ay); to_surface(x2, y2, &bx, &by);
    mesh_line(ax, ay, bx, by, c, w * (xf ? xf->s : 1));
}
static void g_glow(float x, float y, float r, unsigned a, unsigned b) {
    float sx, sy; to_surface(x, y, &sx, &sy);
    mesh_glow(sx, sy, r * (xf ? xf->s : 1), a, b);
}
/* Text centred on an upright point, then moved by (dx, dy) on the surface itself: lines of a
   button stay one above the other, turned or not (and on a moved control, however it is turned). */
static void g_text_at(float x, float y, float dx, float dy, float size, unsigned c, int flags, const char* str) {
    float k = xf ? xf->s : 1, sx, sy;
    to_surface(x, y, &sx, &sy);
    mesh_text(sx + dx * k, sy + (dy + size * 0.35f) * k, size * k, c, flags, str);
}
/* Text stays upright: its middle is moved, and it is drawn level there. */
static void g_text(float x, float y, float size, unsigned c, int flags, const char* str) {
    float my = y - size * 0.35f, k = xf ? xf->s : 1, sx, sy;
    to_surface(x, my, &sx, &sy);
    mesh_text(sx, sy + size * 0.35f * k, size * k, c, flags, str);
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
    float rows = (y - s->y) / row_h;
    hv = &halves[s->half];
    hv->pos = s->start + (flipped_y() ? -rows : rows);
    hv = &halves[0];
    clamp_pos();
    dirty = 1;
}

/* Up each column, unlocked: the column follows the finger, and settles on a whole row when let go
   (so the rows line up across). */
static void col_drag(SideTouch* s, float y) {
    float rows = (y - s->y) / row_h;
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

/* A toggle: down, it is the other way at once (held, it stays so until let go); a second tap
   soon after a short first one changes it for good. */
static double toggle_last_up[C_COUNT], toggle_last_down[C_COUNT];
static int toggle_kept[MAX_FINGERS + 1];        /* by side touch: this press changed it for good */
static void toggle_down(SideTouch* s, int c) {
    int j = (int)(s - side_touches);
    int quick = toggle_last_up[c] > 0 && toggle_last_up[c] - toggle_last_down[c] < 300 && now - toggle_last_up[c] < 350;
    toggle_last_down[c] = now;
    toggle_kept[j] = quick;
    if (quick) keep(toggle_setting(c), toggle_on(c) ? 0 : 1);    /* double tap: for good */
    else { flipped[c] = 1; resend_all(); }                         /* held: for now */
}
static void toggle_up(SideTouch* s, int c) {
    int j = (int)(s - side_touches);
    toggle_last_up[c] = toggle_kept[j] ? 0 : now;              /* a double tap does not start another */
    if (!toggle_kept[j]) flipped[c] = 0;
    resend_all();
}

/* A finger down on control r, at upright height y in its rect. */
static void side_start(int id, SideRect* r, float y, float sx, float sy) {
    for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == 0) {
        SideTouch* s = &side_touches[j];
        s->id = id; s->control = r->control; s->top = r->y; s->h = r->h; s->cancel = 0; s->sx = sx; s->sy = sy;
        s->from = frac(s, y);
        touched[r->control]++;
        if (r->control == C_HOLD) hold(1);
        if (is_toggle(r->control)) toggle_down(s, r->control);
        side_drag(s, s->from);
        break;
    }
}

/* Inside control r (slack: points more all round)? (x, y) upright, (sx, sy) on the surface. */
static int in_control(const SideRect* r, float x, float y, float sx, float sy, float slack, float* ly) {
    if (r->free) {
        from_surface(r, sx, sy, &x, &y);
        slack /= r->s;
    }
    *ly = y;
    return x >= r->x - 5 - slack && x <= r->x + r->w + 5 + slack && y >= r->y - slack && y <= r->y + r->h + slack;
}

static int side_down(int id, float x, float y, float sx, float sy) {
    float ly;
    /* The moved controls first: they are over everything (the last drawn on top). */
    for (int k = nsides - 1; k >= 0; k--)
        if (sides[k].free && in_control(&sides[k], x, y, sx, sy, 0, &ly)) { side_start(id, &sides[k], ly, sx, sy); return 1; }
    if (bar_x >= 0 && x >= bar_x - 4 && x <= bar_x + BAR + 4) {
        for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == 0) {
            SideTouch* s = &side_touches[j];
            int h = half_at(y);
            s->half = h < 0 ? 0 : h;
            s->id = id; s->control = C_BAR; s->start = halves[s->half].pos; s->y = y; s->cancel = 0;
            break;
        }
        return 1;
    }
    for (int k = 0; k < nsides; k++)
        if (!sides[k].free && in_control(&sides[k], x, y, sx, sy, 0, &ly)) { side_start(id, &sides[k], ly, sx, sy); return 1; }
    return 0;
}

/* ---- two fingers on a control: move it, size it (pinch), turn it (twist) ---------------------------- */
static struct { int on, c, id1, id2; float p1x, p1y, p2x, p2y, q1x, q1y, q2x, q2y; Place p0; } gest;

static SideRect* rect_of(int c) {
    for (int k = 0; k < nsides; k++) if (sides[k].control == c) return &sides[k];
    return 0;
}

/* A second finger near a control a finger is on: the control is picked up. */
static int gesture_down(int id, float x, float y, float sx, float sy) {
    if (set_[S_MOVE_CTL] == 0 || gest.on) return 0;
    /* On another control: that one is pressed (two buttons side by side, one finger each). */
    for (int k = 0; k < nsides; k++) {
        float ly;
        if (!in_control(&sides[k], x, y, sx, sy, 0, &ly)) continue;
        int held = 0;
        for (int j = 0; j < MAX_FINGERS; j++) held |= side_touches[j].id && !side_touches[j].cancel && side_touches[j].control == sides[k].control;
        if (!held) return 0;
    }
    for (int j = 0; j < MAX_FINGERS; j++) {
        SideTouch* s1 = &side_touches[j];
        if (s1->id == 0 || s1->cancel || s1->control >= C_COUNT) continue;
        SideRect* r = rect_of(s1->control);
        float ly;
        if (!r || !in_control(r, x, y, sx, sy, 40, &ly)) continue;
        gest.on = 1; gest.c = s1->control; gest.id1 = s1->id; gest.id2 = id;
        gest.p1x = gest.q1x = s1->sx; gest.p1y = gest.q1y = s1->sy;
        gest.p2x = gest.q2x = sx; gest.p2y = gest.q2y = sy;
        if (r->free) gest.p0 = place_of(r->control);
        else {
            /* From its gutter: where it is now, as it is. */
            float cx = tx(r->x + r->w / 2, r->y + r->h / 2), cy = ty(r->x + r->w / 2, r->y + r->h / 2);
            Place p = {cx / SW, cy / SH, 1, 0};
            gest.p0 = p;
        }
        /* The first finger stops playing the control. */
        s1->cancel = 1;
        if (s1->control == C_HOLD) hold(0);
        if (s1->control == C_BEND) { side_bend = 0; resend_all(); }
        if (is_toggle(s1->control)) { flipped[s1->control] = 0; resend_all(); }
        for (int k = 0; k < MAX_FINGERS; k++) if (side_touches[k].id == 0) {
            SideTouch* s2 = &side_touches[k];
            s2->id = id; s2->control = s1->control; s2->cancel = 1; s2->sx = sx; s2->sy = sy;
            touched[s1->control]++;
            break;
        }
        dirty = 1;
        return 1;
    }
    return 0;
}

static void gesture_move(void) {
    const float PI = 3.14159265f;
    float d0x = gest.p2x - gest.p1x, d0y = gest.p2y - gest.p1y, d1x = gest.q2x - gest.q1x, d1y = gest.q2y - gest.q1y;
    float l0 = __builtin_sqrtf(d0x * d0x + d0y * d0y), l1 = __builtin_sqrtf(d1x * d1x + d1y * d1y);
    float ds = l0 > 12 ? l1 / l0 : 1, da = l0 > 12 && l1 > 12 ? atan2f_(d1y, d1x) - atan2f_(d0y, d0x) : 0;
    Place p = gest.p0;
    float scale = clampf(p.scale * ds, 0.4f, 5);
    ds = scale / p.scale;
    /* The point between the fingers keeps its place on the control. */
    float vx = p.fx * SW - (gest.p1x + gest.p2x) / 2, vy = p.fy * SH - (gest.p1y + gest.p2y) / 2, c = cosf_(da), n = sinf_(da);
    float cx = (gest.q1x + gest.q2x) / 2 + (vx * c - vy * n) * ds, cy = (gest.q1y + gest.q2y) / 2 + (vx * n + vy * c) * ds;
    float deg = p.deg + da * 180 / PI;
    deg -= 360 * floorf_(deg / 360);
    float q = 90 * floorf_(deg / 90 + 0.5f);              /* square to the screen when nearly so */
    if (absf(deg - q) < 6) deg = q;
    Place out = {clampf(cx, 0, SW) / SW, clampf(cy, 0, SH) / SH, scale, deg};
    set_[place_index(gest.c)] = pack_place(out);
    layout();
    dirty = 1;
}

static void gesture_up(void) {
    keep(place_index(gest.c), set_[place_index(gest.c)]);    /* kept, and in presets */
    gest.on = 0;
}

static SideTouch* side_find(int id) {
    for (int j = 0; j < MAX_FINGERS; j++) if (side_touches[j].id == id) return &side_touches[j];
    return 0;
}

static void side_up(SideTouch* s) {
    if (s->control == C_BAR) { s->id = 0; dirty = 1; return; }
    if (gest.on && (s->id == gest.id1 || s->id == gest.id2)) gesture_up();
    if (s->cancel && s->control < C_COUNT) { touched[s->control]--; s->id = 0; dirty = 1; return; }
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
    else if (is_toggle(s->control)) toggle_up(s, s->control);
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
    if (!turned && !xf && lw > r->w - 4) {      /* moved, it has room: the whole name */
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
        xf = r->free ? r : 0;
        switch (r->control) {
        case C_LATCH:
            if (!latch_on) put(v, "off");
            else if (nlatched > 0) put(put_int(v, nlatched, 0), " held");
            else put(v, "on");
            draw_button(r, "Latch", v, latch_on, th, ink);
            break;
        case C_HOLD: draw_button(r, "Hold", holding ? "holding" : "press", holding, th, ink); break;
        case C_LOCK: draw_button(r, "Lock", locked() ? "on" : "off", !locked(), th, ink); break;
        case C_SLIDE: draw_button(r, "Slide", flipped[C_SLIDE] ? "for now" : slide_only() ? "only" : "all", slide_only(), th, ink); break;
        case C_INSIDE: draw_button(r, "Inside", flipped[C_INSIDE] ? "for now" : toggle_on(C_INSIDE) ? "on" : "off", toggle_on(C_INSIDE), th, ink); break;
        case C_OUT: draw_button(r, "Slide out", flipped[C_OUT] ? "for now" : out_on() ? "on" : "off", out_on(), th, ink); break;
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
    xf = 0;
}


/* ---- the Touch panel, over the grid ------------------------------------------------------------ */
/* Opened by the rail's Touch (the touchEdit switch); drawn and touched in surface coordinates, at
   the surface's left, beside the rail. One square, as in the grid, to play: it sounds, with every
   change made here. An arrow each side of it says what sliding out that way does; the dashed line
   round it is where sliding starts. Below: what moving inside a square does, what sliding out each
   way does (how much, how readily, which way is more; bend's range and snap), and vibrato, with
   how readily a wiggle is taken for it. Scrolled by dragging its background. */
enum { K_DONE, K_SLIDE, K_SLIDE_ONLY, K_INSIDE_ON, K_OUT_ON, K_INSIDE, K_AXIS, K_TARGET, K_FLIP, K_BRANGE, K_VIB, K_SLIDER, K_RANGE, K_DYN_FULL, K_GLIDE_WAY };
typedef struct { float x, y, w, h; int kind, arg; } Hit;
#define MAX_HITS 64
static Hit hits[MAX_HITS];
static int nhits, ed_finger = -1, ed_slider = -1, ed_dragging, te_axis, ed_thumb = -1;   /* ed_thumb: the loudness range's end being dragged */
static float ed_scroll, ed_from, ed_scroll0, ed_room, ed_content;
static float te_x = -1, te_y, te_s;     /* the square to play, on the surface (te_x < 0: not shown) */

static const char* TARGET_NAME[5] = {"Nothing", "Loudness", "Tone", "Bend", "Volume"};
static const char* BEND_NAME[7] = {"Next note", "\xC2\xB1" "1", "\xC2\xB1" "2", "\xC2\xB1" "3", "\xC2\xB1" "5", "\xC2\xB1" "7", "\xC2\xB1" "12"};
static const char* VIB_NAME[4] = {"Off", "Light", "Normal", "Wide"};

/* The sliders: the setting each moves, its range and step, and how its value reads. */
typedef struct { int index; float min, max, step; const char* label; int kind; } Slider;   /* kind 1 %, 2 dB, 3 two decimals */
enum { SL_AX_AMOUNT, SL_AX_SENS, SL_UD_AMOUNT, SL_UD_SENS, SL_BEND_SNAP, SL_VIB_SENS, SL_GLIDE, SL_COUNT };
static const Slider SLIDERS[SL_COUNT] = {
    {S_AX_AMOUNT, 0, 100, 5, "Strength", 1}, {S_AX_SENS, 25, 400, 5, "Sensitivity", 1},
    {S_UD_AMOUNT, 0, 100, 5, "Strength", 1}, {S_UD_SENS, 25, 400, 5, "Sensitivity", 1},
    {S_BEND_SNAP, 0, 100, 5, "Snap to semitones", 1}, {S_VIB_SENS, 25, 400, 5, "Wiggle sensitivity", 1},
    {S_GLIDE, 0, 1, 0.05f, "Glide", 3},
};

static int editing(void) { return set_[S_TOUCH_EDIT] != 0; }

static void hit(float x, float y, float w, float h, int kind, int arg) {
    if (nhits < MAX_HITS) { Hit t = {x, y, w, h, kind, arg}; hits[nhits++] = t; }
}

static void keep(int index, double v) { set_[index] = v; mesh_set_setting(index, v); dirty = 1; }

static void chip(float x, float y, float w, float h, const char* label, int on, const Theme* th, int kind, int arg) {
    unsigned ink = eink ? BLACK : th->light;
    mesh_rect(x, y, w, h, 8, on ? (eink ? BLACK : mesh_alpha(ink, 0.85f)) : (eink ? WHITE : th->square), mesh_alpha(ink, on ? 1 : 0.45f), 1.5f);
    /* As big as fits: at most 11, smaller for a long name in a narrow chip (text runs about a size per letter). */
    int n = 0;                     /* letters, not bytes: a sharp is three */
    for (const char* c = label; *c; c++) n += (*c & 0xC0) != 0x80;
    float size = n ? clampf((w - 8) / (float)n * 1.6f, 7, 12) : 12;
    mesh_text(x + w / 2, y + h / 2 + size * 0.35f, size, on ? (eink ? WHITE : th->on_light) : ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, label);
    hit(x, y, w, h, kind, arg);
}

/* A row of chips across the width. */
static float SHARED chips(float x, float y, float cw, const char** names, int n, int chosen, const Theme* th, int kind, int arg0) {
    float g = 6, w = (cw - g * (float)(n - 1)) / (float)n;
    for (int k = 0; k < n; k++) chip(x + (float)k * (w + g), y, w, 30, names[k], k == chosen, th, kind, arg0 + k);
    return y + 38;
}

static float SHARED slider(float x, float y, float w, int id, const Theme* th) {
    const Slider* sl = &SLIDERS[id];
    unsigned ink = eink ? BLACK : th->light;
    float v = (float)set_[sl->index], t = clampf((v - sl->min) / (sl->max - sl->min), 0, 1);
    char val[24];
    if (sl->kind == 3) {                 /* 0 to 1 in hundredths, as the rail shows a number: 0, 0.35, 1 */
        int c = roundi(v * 100);
        char* p = put_int(val, c / 100, 0);
        if (c % 100) { *p++ = '.'; *p++ = (char)('0' + (c % 100) / 10); if (c % 10) *p++ = (char)('0' + c % 10); *p = 0; }
    } else {
        char* p = put_int(val, roundi(v), 0);
        put(p, sl->kind == 1 ? "%" : sl->kind == 2 ? " dB" : "");
    }
    mesh_text(x, y + 13, 11, mesh_alpha(ink, 0.85f), 0, sl->label);
    mesh_text(x + w, y + 13, 11, ink, MESH_TEXT_RIGHT | MESH_TEXT_BOLD, val);
    float ty = y + 28;
    mesh_rect(x, ty - 3, w, 6, 3, mesh_alpha(ink, 0.25f), 0, 0);
    mesh_rect(x, ty - 3, w * t, 6, 3, eink ? BLACK : mesh_alpha(ink, 0.8f), 0, 0);
    mesh_circle(x + w * t, ty, 9, eink ? BLACK : th->hot, eink ? WHITE : th->background, 2);
    hit(x - 6, y, w + 12, 40, K_SLIDER, id);
    return y + 46;
}

static void SHARED slider_to(int id, float x) {
    for (int k = 0; k < nhits; k++) if (hits[k].kind == K_SLIDER && hits[k].arg == id) {
        const Slider* sl = &SLIDERS[id];
        float t = clampf((x - (hits[k].x + 6)) / (hits[k].w - 12), 0, 1);
        float v = sl->min + t * (sl->max - sl->min);
        v = sl->min + (float)roundi((v - sl->min) / sl->step) * sl->step;
        if ((double)v != set_[sl->index]) keep(sl->index, v);
        return;
    }
}

/* The loudness range: one track, a handle for the bottom of a square and one for its top. */
static float range_slider(float x, float y, float w, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    float lo = dyn_low(), hi = dyn_high();
    char a[40], b[40];
    put(put_int(put(a, "Bottom of a " CELL " "), roundi(lo * 100), 0), "%");
    put(put_int(put(b, "Top "), roundi(hi * 100), 0), "%");
    mesh_text(x, y + 13, 11, ink, MESH_TEXT_BOLD, a);
    mesh_text(x + w, y + 13, 11, ink, MESH_TEXT_RIGHT | MESH_TEXT_BOLD, b);
    float ty = y + 30;
    mesh_rect(x, ty - 3, w, 6, 3, mesh_alpha(ink, 0.25f), 0, 0);
    mesh_rect(x + w * lo, ty - 3, w * (hi - lo), 6, 3, eink ? BLACK : mesh_alpha(ink, 0.8f), 0, 0);
    mesh_circle(x + w * lo, ty, 9, eink ? WHITE : th->background, eink ? BLACK : th->hot, 2.5f);
    mesh_circle(x + w * hi, ty, 9, eink ? BLACK : th->hot, eink ? WHITE : th->background, 2);
    mesh_text(x, ty + 22, 10, mesh_alpha(ink, 0.6f), 0, "silent");
    mesh_text(x + w, ty + 22, 10, mesh_alpha(ink, 0.6f), MESH_TEXT_RIGHT, "full");
    hit(x - 10, y, w + 20, 46, K_RANGE, 0);
    return y + 62;
}

/* The loudness range's handle being dragged to x: the nearer one when it was pressed. */
static void range_to(float x) {
    for (int k = 0; k < nhits; k++) if (hits[k].kind == K_RANGE) {
        float w = hits[k].w - 20, t = clampf((x - (hits[k].x + 10)) / w, 0, 1);
        double v = (double)(roundi(t * 20) * 5);
        if (ed_thumb < 0) ed_thumb = absf((float)v - (float)set_[S_DYN_LOW]) <= absf((float)v - (float)set_[S_DYN_HIGH]) && v <= set_[S_DYN_HIGH] ? 0 : 1;
        if (ed_thumb == 0) { if (v > set_[S_DYN_HIGH]) v = set_[S_DYN_HIGH]; if (v != set_[S_DYN_LOW]) keep(S_DYN_LOW, v); }
        else { if (v < set_[S_DYN_LOW]) v = set_[S_DYN_LOW]; if (v != set_[S_DYN_HIGH]) keep(S_DYN_HIGH, v); }
        return;
    }
}

static float section(float x, float y, const char* title, const Theme* th) {
    mesh_text(x, y + 18, 11, mesh_alpha(eink ? BLACK : th->light, 0.7f), MESH_TEXT_BOLD, title);
    return y + 28;
}

/* What sliding out of a square one way does: axis 0 left/right (positive: right), 1 up/down
   (positive: down). */
static const char* arrow_word(int axis, int positive) {
    if (sliding()) return "the next " CELL;
    int t = (int)set_[axis ? S_ACROSS : S_UPDOWN];
    int more = positive != (set_[axis ? S_AX_FLIP : S_UD_FLIP] != 0);
    switch (t) {
    case T_LOUDNESS: return more ? "louder" : "softer";
    case T_TONE: return more ? "brighter" : "darker";
    case T_VOLUME: return more ? "louder" : "quieter";
    case T_BEND: return set_[S_BEND_RANGE] == 0 ? "to the next note" : more ? "bend up" : "bend down";
    default: return "nothing";
    }
}

static void arrow_box(float x, float y, float w, float h, const char* way, int axis, int positive, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    int sel = te_axis == axis, more = positive != (set_[axis ? S_AX_FLIP : S_UD_FLIP] != 0), none = set_[axis ? S_ACROSS : S_UPDOWN] == 0;
    mesh_rect(x, y, w, h, 10, eink ? WHITE : th->square, mesh_alpha(ink, sel ? 0.95f : 0.35f), sel ? 2.5f : 1.2f);
    mesh_text(x + w / 2, y + h / 2 - 4, 10, mesh_alpha(ink, 0.7f), MESH_TEXT_CENTER, way);
    mesh_text(x + w / 2, y + h / 2 + 11, 11, none ? mesh_alpha(ink, 0.5f) : more && !eink ? th->hot : ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, arrow_word(axis, positive));
    hit(x, y, w, h, K_AXIS, axis);
}

static Finger* demo_finger(void) {
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used && fingers[k].n == DEMO_N) return &fingers[k];
    return 0;
}

/* The square to play, its arrows and the slide line round it; then what it does now, in a line. */
static float SHARED demo_part(float x, float y, float cw, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    float aw = 76, gap_ = 14, m = 0;
    float s = clampf(cw - 2 * (aw + gap_) - 20, 90, 170);
    m = s * MARGIN;
    float sx = x + (cw - s) / 2, ah = 40, sy = y + ah + gap_ + m;
    arrow_box(sx, y, s, ah, "slide out up", 1, 0, th);
    arrow_box(sx - m - gap_ - aw, sy, aw, s, "slide out left", 0, 0, th);
    arrow_box(sx + s + m + gap_, sy, aw, s, "slide out right", 0, 1, th);
    arrow_box(sx, sy + s + m + gap_, s, ah, "slide out down", 1, 1, th);
    Finger* f = demo_finger();
    int out = 0;
    if (f) {
        float u = (f->ry + f->rs - f->sy) / f->rs, v = (f->sx - f->rx) / f->rs;
        out = past(u) != 0 || past(v) != 0;
    }
    /* The line where sliding starts. */
    mesh_rect(sx - m, sy - m, s + 2 * m, s + 2 * m, 14 + m, 0, out && !eink ? th->hot : mesh_alpha(ink, 0.3f), out ? 2 : 1);
    /* The square: its height the dynamics (lit at the top), across it the tone. */
    int dyn = set_[S_IN_DYN] != 0, tone = set_[S_IN_TONE] != 0, live = dyn || tone;
    mesh_rect(sx, sy, s, s, s * 0.08f, eink ? WHITE : th->square, 0, 0);
    if (dyn && !eink) mesh_vgradient(sx + 2, sy + 2, s - 4, s - 4, mesh_alpha(th->light, 0.45f), mesh_alpha(th->light, 0.04f));
    if (tone && !eink) for (int k = 0; k < 6; k++) mesh_rect(sx + 2 + (s - 4) * (float)k / 6, sy + 2, (s - 4) / 6, s - 4, 0, mesh_alpha(th->hot, 0.025f * (float)k), 0, 0);
    mesh_rect(sx, sy, s, s, s * 0.08f, 0, out && !eink ? th->hot : mesh_alpha(ink, 0.8f), 2);
    if (live) {
        char top[16], bot[16];
        put(put_int(top, roundi(dyn_high() * 100), 0), "%");
        put(put_int(bot, roundi(dyn_low() * 100), 0), "%");
        if (dyn) mesh_text(sx + s / 2, sy + 14, 10, mesh_alpha(ink, 0.8f), MESH_TEXT_CENTER | MESH_TEXT_BOLD, top);
        if (dyn) mesh_text(sx + s / 2, sy + s - 6, 10, mesh_alpha(ink, 0.8f), MESH_TEXT_CENTER | MESH_TEXT_BOLD, bot);
        if (tone) mesh_text(sx + 6, sy + s / 2 + 4, 10, mesh_alpha(ink, 0.8f), 0, "dark");
        if (tone) mesh_text(sx + s - 6, sy + s / 2 + 4, 10, mesh_alpha(ink, 0.8f), MESH_TEXT_RIGHT, "bright");
    } else mesh_text(sx + s / 2, sy + s / 2 + 4, 11, mesh_alpha(ink, 0.8f), MESH_TEXT_CENTER, "press to play");
    (void)live;
    te_x = sx; te_y = sy; te_s = s;
    float y2 = sy + s + m + gap_ + ah + 10;
    if (f) {
        /* the finger (it is kept upright: back to the surface), and the note now */
        float fx = H - f->y, fy = f->x;
        mesh_circle(fx, fy, 13, eink ? 0 : mesh_alpha(th->hot, 0.35f), eink ? BLACK : th->hot, 2);
        Expr e = expression(f);
        char line[96], *p = line;
        p = put(p, "Loudness ");
        p = put_int(p, roundi(e.pressure * 100), 0);
        p = put(p, "%   Tone ");
        p = put(p, e.slide > 0.52f ? "brighter" : e.slide < 0.48f ? "darker" : "middle");
        p = put(p, "   Pitch ");
        int c = roundi((e.pitch - pitch_of(f->note)) * 100);
        p = put_int(p, c, 1);
        put(p, " cents");
        mesh_text(x + cw / 2, y2 + 12, 12, ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, line);
    } else mesh_text(x + cw / 2, y2 + 12, 11, mesh_alpha(ink, 0.7f), MESH_TEXT_CENTER, "Play the " CELL ": press, move, wiggle, slide out");
    return y2 + 26;
}

#ifdef KEYS_HEX
/* The Hex keyboard's Touch panel is its own (hex.c): its keys, inside them, and the two-finger slide. */
static void ed_draw(const Theme* th);
static void ed_touch(int id, int phase, float x, float y);
#else
static void ed_draw(const Theme* th) {
    nhits = 0;
    te_x = -1;
    unsigned ink = eink ? BLACK : th->light, panel = eink ? WHITE : th->background;
    float pw = SW - 16 < 520 ? SW - 16 : 520, ph = SH - 16, px = 8, py = 8;
    float top = py + 56, bottom = py + ph - 8;
    ed_room = bottom - top;
    mesh_rect(0, 0, SW, SH, 0, eink ? 0xFFFFFFCCu : 0x000000B0u, 0, 0);
    mesh_rect(px, py, pw, ph, 16, panel, mesh_alpha(ink, 0.6f), 1.5f);
    float in = 18, x = px + in, cw = pw - 2 * in, y = top - ed_scroll;
    int sliding_ = set_[S_SLIDE] != 0;
    chip(x, y, cw, 32, sliding_ ? "Slide between notes: on" : "Slide between notes: off", sliding_, th, K_SLIDE, 0);
    y += 40;
    chip(x, y, cw, 32, slide_only() ? "Only slide between notes: on" : "Only slide between notes: off", slide_only(), th, K_SLIDE_ONLY, 0);
    y += 40;
    {
        float hw = (cw - 6) / 2;
        int in = set_[S_INSIDE_ON] != 0, out = set_[S_OUT_ON] != 0;
        chip(x, y, hw, 30, in ? "Inside a " CELL ": on" : "Inside a " CELL ": off", in, th, K_INSIDE_ON, 0);
        chip(x + hw + 6, y, hw, 30, out ? "Sliding out: on" : "Sliding out: off", out, th, K_OUT_ON, 0);
        y += 38;
    }
    {
        y = demo_part(x, y, cw, th);
        y = section(x, y, "INSIDE A " CELL_UPPER, th);
        int dyn = set_[S_IN_DYN] != 0, tone = set_[S_IN_TONE] != 0;
        float hw = (cw - 6) / 2;
        chip(x, y, hw, 30, dyn ? "Up/down: dynamics" : "Up/down: off", dyn, th, K_INSIDE, 0);
        chip(x + hw + 6, y, hw, 30, tone ? "Left/right: tone" : "Left/right: off", tone, th, K_INSIDE, 1);
        y += 38;
        if (dyn) y = range_slider(x, y, cw, th);
        /* The Dynamics fader (an expression pedal over every note) stays where it was left: say so
           when it is turned down, as everything is quieter for it. */
        if (set_[S_DYN] < 0.99) {
            char d[64];
            put(put_int(put(d, "The Dynamics fader is at "), roundi((float)set_[S_DYN] * 100), 0), "%: set it to 100%");
            chip(x, y, cw, 30, d, 0, th, K_DYN_FULL, 0);
            y += 38;
        }
        if (sliding_) {
            y = section(x, y, "SLIDING OUT", th);
            mesh_text(x, y + 10, 11, mesh_alpha(ink, 0.8f), 0, set_[S_GLIDE] > 0.001 ? "Sliding between notes: one note glides to the next square's."
                                                                                   : "Sliding between notes: out of a " CELL ", the next one plays.");
            y += 22;
            y = slider(x, y, cw, SL_GLIDE, th);
            if (set_[S_GLIDE] > 0.001) {
                int lr = set_[S_GLIDE_LR] != 0, ud = set_[S_GLIDE_UD] != 0;
                float hw = (cw - 6) / 2;
                chip(x, y, hw, 30, lr ? "Glide left/right" : "Left/right: jump", lr, th, K_GLIDE_WAY, 0);
                chip(x + hw + 6, y, hw, 30, ud ? "Glide up/down" : "Up/down: jump", ud, th, K_GLIDE_WAY, 1);
                y += 38;
            }
        } else {
            y = section(x, y, "SLIDING OUT, PAST THE LINE", th);
            static const char* AXIS[2] = {"Left/right", "Up/down"};
            y = chips(x, y, cw, AXIS, 2, te_axis, th, K_AXIS, 0);
            int target = (int)set_[te_axis ? S_ACROSS : S_UPDOWN];
            y = chips(x, y, cw, TARGET_NAME, 5, target, th, K_TARGET, te_axis * 8);
            if (target != T_NONE) {
                y = slider(x, y, cw, te_axis ? SL_AX_AMOUNT : SL_UD_AMOUNT, th);
                y = slider(x, y, cw, te_axis ? SL_AX_SENS : SL_UD_SENS, th);
                static const char* LR[2] = {"Right is more", "Left is more"}, *UD[2] = {"Down is more", "Up is more"};
                y = chips(x, y, cw, te_axis ? UD : LR, 2, set_[te_axis ? S_AX_FLIP : S_UD_FLIP] != 0, th, K_FLIP, te_axis * 8);
            }
            if (target == T_BEND) {
                y = chips(x, y, cw, BEND_NAME, 7, (int)set_[S_BEND_RANGE], th, K_BRANGE, 0);
                y = slider(x, y, cw, SL_BEND_SNAP, th);
            }
        }
        y = section(x, y, "WIGGLE: VIBRATO, ANY WAY", th);
        y = chips(x, y, cw, VIB_NAME, 4, (int)set_[S_VIBRATO], th, K_VIB, 0);
        if (set_[S_VIBRATO] != 0) y = slider(x, y, cw, SL_VIB_SENS, th);
    }
    ed_content = y + ed_scroll - top;
    /* The header and the foot over what scrolled under them. */
    mesh_rect(px + 1, py + 1, pw - 2, top - py - 1, 15, panel, 0, 0);
    mesh_rect(px + 1, bottom, pw - 2, py + ph - bottom - 1, 15, panel, 0, 0);
    mesh_rect(px, 0, pw, py, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py + ph, pw, SH - py - ph, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py, pw, ph, 16, 0, mesh_alpha(ink, 0.6f), 1.5f);
    mesh_text(px + 18, py + 30, 17, ink, MESH_TEXT_BOLD, "Touch");
    mesh_text(px + 18, py + 47, 10, mesh_alpha(ink, 0.7f), 0, ed_content > ed_room ? "How a " CELL " answers your finger. Drag to scroll" : "How a " CELL " answers your finger");
    /* Hits under the header or foot can't be touched; Done always can. */
    int k = 0;
    for (int j = 0; j < nhits; j++) if (hits[j].y + hits[j].h > top && hits[j].y < bottom) hits[k++] = hits[j];
    nhits = k;
    if (te_x >= 0 && (te_y < top || te_y + te_s > bottom)) te_x = -1;   /* scrolled out of sight: not playable */
    chip(px + pw - 94, py + 12, 80, 32, "Done", 1, th, K_DONE, 0);
}

/* A touch on the panel: the square plays; tap the buttons; drag a slider; drag anywhere else to scroll. */
static void demo_down(int id, float x, float y);
static void ed_touch(int id, int phase, float x, float y) {
    Finger* df = find(id);
    if (df && df->n == DEMO_N) {
        /* the square being played: as a finger on the grid, kept upright */
        if (phase == MESH_MOVE) { df->x = y; df->y = H - x; df->press = touch_press; send(df); }
        else if (phase != MESH_DOWN) up(id);
        dirty = 1;
        return;
    }
    if (phase != MESH_DOWN && id != ed_finger) return;
    if (phase == MESH_UP || phase == MESH_CANCEL) { ed_finger = -1; ed_slider = -1; ed_thumb = -1; ed_dragging = 0; return; }
    if (phase == MESH_MOVE) {
        if (ed_slider >= 0) { slider_to(ed_slider, x); return; }
        if (ed_thumb >= 0) { range_to(x); return; }
        if (ed_dragging) {
            float most = ed_content - ed_room;
            ed_scroll = clampf(ed_scroll0 + (ed_from - y), 0, most > 0 ? most : 0);
            dirty = 1;
        }
        return;
    }
    if (te_x >= 0 && x >= te_x && x <= te_x + te_s && y >= te_y && y <= te_y + te_s) { demo_down(id, x, y); return; }
    ed_finger = id; ed_slider = -1; ed_dragging = 0;
    for (int k = 0; k < nhits; k++) {
        Hit* h = &hits[k];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        switch (h->kind) {
        case K_DONE: keep(S_TOUCH_EDIT, 0); break;
        case K_SLIDE: keep(S_SLIDE, set_[S_SLIDE] != 0 ? 0 : 1); break;
        case K_SLIDE_ONLY: keep(S_SLIDE_ONLY, set_[S_SLIDE_ONLY] != 0 ? 0 : 1); break;
        case K_INSIDE_ON: keep(S_INSIDE_ON, set_[S_INSIDE_ON] != 0 ? 0 : 1); break;
        case K_OUT_ON: keep(S_OUT_ON, set_[S_OUT_ON] != 0 ? 0 : 1); break;
        case K_INSIDE: { int s = h->arg ? S_IN_TONE : S_IN_DYN; keep(s, set_[s] != 0 ? 0 : 1); break; }
        case K_AXIS: te_axis = h->arg; break;
        case K_TARGET: keep(h->arg >= 8 ? S_ACROSS : S_UPDOWN, h->arg % 8); break;
        case K_FLIP: keep(h->arg >= 8 ? S_AX_FLIP : S_UD_FLIP, h->arg % 8); break;
        case K_BRANGE: keep(S_BEND_RANGE, h->arg); break;
        case K_VIB: keep(S_VIBRATO, h->arg); break;
        case K_SLIDER: ed_slider = h->arg; slider_to(h->arg, x); break;
        case K_RANGE: ed_thumb = -1; range_to(x); break;
        case K_DYN_FULL: keep(S_DYN, 1); resend_all(); break;
        case K_GLIDE_WAY: { int s = h->arg ? S_GLIDE_UD : S_GLIDE_LR; keep(s, set_[s] != 0 ? 0 : 1); break; }
        }
        dirty = 1;
        return;
    }
    ed_dragging = 1; ed_from = y; ed_scroll0 = ed_scroll;
}

/* The square in the panel, pressed: a finger on it, as on the grid's own squares, playing the
   note nearest the middle of the instrument (in the key and octave set). */
static void demo_down(int id, float x, float y) {
    Finger* f = 0;
    for (int j = 0; j < MAX_FINGERS; j++) if (!fingers[j].used) { f = &fingers[j]; break; }
    if (!f || nfingers() + nlatched >= MAX_VOICES) return;
    f->used = 1; f->id = id; f->voice = next_voice++; f->n = DEMO_N; f->half = 0;
    f->note = demo_note();
    f->x0 = f->x = f->sx = y; f->y0 = f->y = f->sy = H - x;     /* upright, as every finger */
    f->rx = te_y; f->ry = H - te_x - te_s; f->rs = te_s;
    f->ux = 1; f->uy = 0; f->glide = 0;
    f->press0 = f->press = touch_press;
    f->strike = level_at(f, f->x, f->y);
    press_strike(f);
    Expr e = expression(f);
    if (e.volume < 1) mesh_note_volume(f->voice, e.volume, now);
    mesh_note_on(f->voice, 0, e.pitch, e.pressure, e.slide, f->strike, now);
    dirty = 1;
}
#endif  /* KEYS_HEX: the Grid's own panel */

/* ---- Your own notes: the editor ------------------------------------------------------------- */
/* A panel across the top of the surface (or the bottom, when the square being changed is under the
   top half): the square's note, the twelve names, octave and semitone steps, and back as the rows.
   Tap a square to choose it (it plays as usual). Drawn and touched in surface coordinates. */
static void note_name(char* out, int note, int octave);
enum { K_NE_DONE = 40, K_NE_USE, K_NE_NAME, K_NE_OCT, K_NE_SEMI, K_NE_RESET, K_NE_RESETALL };
static float ne_y, ne_h;
static int ne_finger = -1;

static int note_editing(void) { return set_[S_NOTE_EDIT] != 0; }

/* The chosen square's note (its own half's), or -1. */
static int sel_note(void) {
    if (sel < 0) return -1;
    Half* was = hv;
    hv = &halves[sel_half < nhalves ? sel_half : 0];
    int n = note_of(sel);
    hv = was;
    return n;
}

/* The first change fixes every square where it is now (else they would move with the window and
   the sound, as the rows' do). */
static void bake(void) {
    Half* was = hv;
    hv = &halves[0];
    for (int r = 0; r < CUST_ROWS; r++)
        for (int c = 0; c < cols && c < CUST_COLS; c++) {
            int i = r * CUST_COLS + c;
            if (cust_get(i) == CUST_ANY) cust_put(i, note_at(r * cols + c + hv->kshift) & 0x7F);
        }
    hv = was;
}

/* The chosen square to note (as heard: the top's octave is taken off to keep it). */
static void ne_set(int note) {
    if (sel < 0) return;
    Half* was = hv;
    hv = &halves[sel_half < nhalves ? sel_half : 0];
    int i = cust_index(sel);
    if (i >= 0) {
        bake();
        note -= top_shift();
        cust_put(i, note < 0 ? 0 : note > 127 ? 127 : note);
    }
    hv = was;
}

static void ne_draw(const Theme* th) {
    nhits = 0;
    unsigned ink = eink ? BLACK : th->light, panel = eink ? WHITE : th->background;
    float pw = SW - 16 > 600 ? 600 : SW - 16, px = (SW - pw) / 2, in = 10, cw = pw - 2 * in, g = 5;
    int wrap = (cw - 11 * g) / 12 < 30;          /* the names in two rows of six */
    float ph = custom() ? 44 + (wrap ? 2 : 1) * 38 + 38 + 4 : 44 + 38 + 4;
    /* Out of the way of the square being changed. */
    float sy = -1;
    if (sel >= 0 && custom()) {
        Half* was = hv;
        hv = &halves[sel_half < nhalves ? sel_half : 0];
        float ux = square_x(sel) + (cell - gap) / 2, uy = square_y(sel) + (cell - gap) / 2;
        sy = turned ? ux : uy;
        hv = was;
    }
    ne_y = sy >= 0 && sy < ph + 40 ? SH - ph - 8 : 8;
    ne_h = ph;
    mesh_rect(px, ne_y, pw, ph, 14, panel, mesh_alpha(ink, 0.6f), 1.5f);
    float y = ne_y + 8;
    char title[40];
    int note = sel_note();
    if (!custom()) put(title, "Notes run: choose Your own notes");
    else if (note < 0) put(title, "Tap a square to change its note");
    else { char* p = put(title, "This square: "); note_name(p, note, 1); }
    mesh_text(px + in + 4, y + 20, 14, ink, MESH_TEXT_BOLD, title);
    chip(px + pw - in - 70, y, 70, 30, "Done", 1, th, K_NE_DONE, 0);
    y += 36;
    if (!custom()) { chip(px + in, y, cw, 32, "Use your own notes", 0, th, K_NE_USE, 0); return; }
    int per = wrap ? 6 : 12;
    float nw = (cw - (float)(per - 1) * g) / (float)per;
    for (int d = 0; d < 12; d++)
        chip(px + in + (float)(d % per) * (nw + g), y + (float)(d / per) * 38, nw, 32, NAMES[d], note >= 0 && note % 12 == d, th, K_NE_NAME, d);
    y += (wrap ? 2 : 1) * 38;
    float aw = (cw - 5 * g) / 6;
    static const char* ACT[6] = {"Oct \xE2\x88\x92", "Oct +", ("\xE2\x88\x92" "1"), "+1", "As the rows", "Reset all"};
    static const int KIND[6] = {K_NE_OCT, K_NE_OCT, K_NE_SEMI, K_NE_SEMI, K_NE_RESET, K_NE_RESETALL};
    for (int a = 0; a < 6; a++)
        chip(px + in + (float)a * (aw + g), y, aw, 32, a == 5 && resetting ? "Sure?" : ACT[a], a == 5 && resetting, th, KIND[a], a % 2 ? 1 : -1);
}

/* A touch on the panel (1: it was), on surface coordinates. A touch elsewhere plays, and chooses. */
static int ne_touch(int id, int phase, float x, float y) {
    if (phase != MESH_DOWN) {
        if (id != ne_finger) return 0;
        if (phase == MESH_UP || phase == MESH_CANCEL) ne_finger = -1;
        return 1;
    }
    if (y < ne_y || y > ne_y + ne_h) return 0;
    ne_finger = id;
    int note = sel_note();
    for (int k = 0; k < nhits; k++) {
        Hit* h = &hits[k];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        if (h->kind != K_NE_RESETALL) resetting = 0;
        switch (h->kind) {
        case K_NE_DONE: keep(S_NOTE_EDIT, 0); sel = -1; break;
        case K_NE_USE: keep(S_LAYOUT, 2); layout(); clamp_pos(); break;
        case K_NE_NAME: if (note >= 0) ne_set(note - note % 12 + h->arg); break;
        case K_NE_OCT: if (note >= 0) ne_set(note + 12 * h->arg); break;
        case K_NE_SEMI: if (note >= 0) ne_set(note + h->arg); break;
        case K_NE_RESET: {
            int i = sel >= 0 ? cust_index(sel) : -1;
            if (i >= 0) cust_put(i, CUST_ANY);
            break;
        }
        case K_NE_RESETALL:
            if (!resetting) { resetting = 1; break; }
            resetting = 0;
            for (int w = 0; w < CUST_WORDS; w++) keep(S_NOTES0 + w, NOTES_EVEN);
            break;
        }
        break;
    }
    return 1;
}

#ifndef KEYS_HEX
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
#endif

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
static void SHARED c_rect(float x, float y, float w, float h, float r, unsigned fill, unsigned stroke, float sw) {
    if (nhalves > 1) {
        float t = hv->top, b = hv->bot;
        if (y < t) { h -= t - y; y = t; }
        if (y + h > b) h = b - y;
        if (h <= 0.5f) return;
        if (r > h / 2) r = h / 2;
    }
    g_rect(x, y, w, h, r, fill, stroke, sw);
}
static int SHARED in_half(float y) { return y >= hv->top && y <= hv->bot; }

/* The keys' shape, again: drawing them and the scroll bar beside them. */
static void draw_keys(const Theme* th);
static void draw_bar(const Theme* th);

#ifndef KEYS_HEX
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
        for (int c = 0; c < (intervals() ? 1 : cols); c++) { int note = note_of(r * cols + c); if (note >= 0 && floormod(note - key(), 12) == 0) { root = note; break; } }
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
static void draw_keys(const Theme* th) {
    int flat = eink;
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
        if (note < 0) continue;
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
        if (note_editing() && n == sel && h == sel_half)
            c_rect(ox - 4, oy - 4, side + 8, side + 8, radius + 4, 0, flat ? BLACK : th->hot, 4);
        if ((set_[S_LABELS] != 0 || note_editing()) && in_half(oy + side / 2)) {
            float size = side * 0.16f;
            unsigned tc = glow > 0.5f ? (flat ? WHITE : colour ? 0x101010FFu : th->on_light)
                        : mesh_alpha(colour ? colour : light, off ? 0.3f : 0.85f);
            char name[16];
            note_name(name, note, 1);
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
}
#endif

static void draw(void) {
    layout();
    clamp_pos();
    const Theme* th = theme();
    int flat = eink;
    mesh_begin();
    g_clear(flat ? WHITE : th->background);
    draw_keys(th);
    /* A halo that follows each finger. */
    if (!flat && !th->bright)
        for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used)
            g_glow(fingers[k].x, fingers[k].y, cell * 0.9f, mesh_alpha(th->hot, 0.5f), mesh_alpha(th->light, 0));
    draw_bar(th);
    draw_sides(th);
    if (editing()) ed_draw(th);
    else if (note_editing()) ne_draw(th);
    mesh_end();
    dirty = 0;
}

/* ---- the interface ------------------------------------------------------------------------------- */
/* Upright or sideways: Auto turns it sideways when the surface is wider than it is tall. */
static void turn_to(float sw, float sh) {
    SW = sw; SH = sh;
    turned = set_[S_FLIP90] == 0;
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
#ifdef KEYS_ON_SETTING
    KEYS_ON_SETTING(index);
#endif
    if (index == S_FLIP90) turn_to(SW, SH);
    if (index == S_PLACE_RESET && value != 0) reset_places = 1;   /* done in tick(), outside the host's call */
    if (index == S_LATCH) set_latch(value != 0);
    if (index == S_TOUCH_EDIT) { ed_scroll = 0; if (value != 0) stop(); }   /* opening the panel lets go of what plays */
    if (index == S_NOTE_EDIT) { sel = -1; resetting = 0; if (value != 0) stop(); }
    layout();
    clamp_pos();
}

MESH_EXPORT(touch) void touch(int id, int phase, float x, float y, float size, float force, double t) {
    touch_press = force > 0 ? force : size > 0 ? size : 0;
    now = t;
    /* The Touch panel takes every touch while it is open, on the surface as it is. */
    if (editing()) { ed_touch(id + 1, phase, x, y); dirty = 1; return; }
    if (note_editing() && ne_touch(id, phase, x, y)) { dirty = 1; return; }
    float sx = x, sy = y;                                        /* on the surface, for moved controls */
    if (turned) { float ux = y, uy = H - x; x = ux; y = uy; }   /* the touch, as on the upright grid */
    id += 1;    /* 0 marks a free side touch */
    if (phase == MESH_DOWN) {
        if (find(id)) up(id);
        layout();
        if (gesture_down(id, x, y, sx, sy)) { dirty = 1; return; }
        if (!side_down(id, x, y, sx, sy)) { if (columns() && !locked()) col_down(id, x, y); else down(id, x, y); }
        /* Editing your own notes: the square played is the one to change. */
        Finger* f = note_editing() ? find(id) : 0;
        if (f) { sel = f->n; sel_half = f->half; resetting = 0; }
    } else if (phase == MESH_MOVE) {
        SideTouch* s = side_find(id);
        if (s) {
            s->sx = sx; s->sy = sy;
            if (gest.on && (id == gest.id1 || id == gest.id2)) {
                if (id == gest.id1) { gest.q1x = sx; gest.q1y = sy; } else { gest.q2x = sx; gest.q2y = sy; }
                gesture_move();
                return;
            }
            if (s->cancel) return;
            if (s->control == C_BAR) bar_drag(s, y);
            else if (s->control == C_COL) col_drag(s, y);
            else {
                SideRect* r = rect_of(s->control);
                if (r && r->free) { float lx; from_surface(r, sx, sy, &lx, &y); }
                side_drag(s, frac(s, y));
            }
            return;
        }
#ifdef KEYS_HEX
        if (pair_move(id, x, y)) { dirty = 1; return; }
#endif
        Finger* f = find(id);
        if (!f) return;
        f->x = x; f->y = y; f->press = touch_press;
        if (sliding() && !note_editing()) { if (set_[S_GLIDE] > 0.001) glide_to(f, x, y); else slide_to(f, x, y); }
        send(f);
    } else {
        SideTouch* s = side_find(id);
        if (s) side_up(s);
#ifdef KEYS_HEX
        else if (!pair_up(id)) up(id);
#else
        else up(id);
#endif
    }
    dirty = 1;
}

MESH_EXPORT(tick) void tick(double t) {
    float dt = last_tick < 0 ? 0 : (float)((t - last_tick) / 1000.0);
    if (dt < 0) dt = 0;
    if (dt > 0.25f) dt = 0.25f;
    last_tick = t;
    now = t;
    if (reset_places) {
        reset_places = 0;
        for (int c = 0; c < C_COUNT; c++) if (placed(c)) keep(place_index(c), 0);
        keep(S_PLACE_RESET, 0);
        layout();
    }
    /* The slow position trails the finger by about 0.12 s: a wiggle is faster than that. */
    float k = 1 - expf_(-dt / 0.12f);
    int any = 0;
    for (int j = 0; j < MAX_FINGERS; j++) if (fingers[j].used) {
        Finger* f = &fingers[j];
        f->sx += (f->x - f->sx) * k;
        f->sy += (f->y - f->sy) * k;
        /* The way it rocks: the quick part's direction, either sign, followed in about 0.15 s. */
        float hx = f->x - f->sx, hy = f->y - f->sy, m = __builtin_sqrtf(hx * hx + hy * hy);
        if (m > 1) {
            float s = hx * f->ux + hy * f->uy < 0 ? -1.0f : 1.0f, a = clampf(dt / 0.15f, 0, 1);
            float ux = f->ux + (s * hx / m - f->ux) * a, uy = f->uy + (s * hy / m - f->uy) * a, n = __builtin_sqrtf(ux * ux + uy * uy);
            if (n > 1e-4f) { f->ux = ux / n; f->uy = uy / n; }
        }
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
    gest.on = 0;
    for (int c = 0; c < C_COUNT; c++) flipped[c] = 0;
    for (int c = 0; c < C_COUNT; c++) touched[c] = 0;
    nspent = 0;
    holding = 0;
    side_bend = 0;
    release_latched();
}
