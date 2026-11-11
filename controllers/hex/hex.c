/*
 * The Hex keyboard as a MESH controller: hexagonal keys in an isomorphic layout, as on a Lumatone
 * or in Hexatone. Any interval, any chord, has the same shape wherever it is played. Its notes come
 * from a layout (Wicki-Hayden, Harmonic table, Bosanquet, or your own two steps) and a tuning (12,
 * 19, 22, 31, 41 or 53 equal, 5-limit just, Pythagorean, quarter-comma meantone): tuning.h.
 *
 * It is the Grid (../grid/grid.c) with hexagons for squares: inside a key, the Grid's dynamics and
 * tone; Latch, Hold and the other controls beside it (moved, sized and turned with two fingers), the
 * split, Flip 90°. Its touch past that is its own (below, "two fingers to slide"): one finger plays a
 * key and stays on its note wherever it goes. Two fingers in the same key, slid out of it, bend the
 * note: by how near the finger is to the keys before and after it along its row (on the turned
 * keyboard, above and below), exactly that key's note at its middle and half way on the line between
 * (C to D: C♯ on their line), snapped to so many points between keys if Snap is on. Once sliding, one
 * finger plays it, and the other is free for another key. No vibrato, no Slide between notes.
 *
 * Laid out as the Grid is: upright, rows of keys across, each row half a key to the right of the one
 * below (on an even row: rows are counted from 0, the bottom one unscrolled), drawn a quarter turned
 * (Flip 90°: upright). A key k counts along the rows (kk below: its row and its place in the row),
 * as the Grid's squares do. In axial terms the key at place c in row r is at q = c - floor(r / 2): the key
 * up to the right of (q, r) is (q, r + 1), so its note is the layout's step up to the right (NE)
 * above it, and the key to its right, (q + 1, r), the step to the right (E) above.
 *
 * The rows run on both ways without end, and the keys can be moved, sized and turned as a whole (the
 * view, below: "Move the keyboard (two fingers)", or the Move button): every finger on a key is kept
 * where it is on the keys as they lie unmoved (the plane), so how a key plays, its dynamics and the
 * slide along its row, is the same however the keyboard lies on the screen. Key k is (row, place)
 * packed: k = row * KSTRIDE + place + KOFF (kk, kr, kc), places from -KOFF on.
 */
#define KEYS_HEX 1
#define CELL "key"
#define CELL_UPPER "KEY"
#define KEYS_MAX_COLS 16
/* Its own settings, after the Grid's (the manifest lists them in this order, at the end). */
#define SCALA_MAX 128           /* tuning.h's HT_MAX_N; the manifest's "scala" max */
/* The view (moving the keyboard): its mode, put back, then where it is (hidden): moved by so many keys
   across and up (upright, at its size before it is sized), its size and its turn (degrees, clockwise
   on the screen); the Move button's side, and its own moved place. */
#define KEYS_EXTRA_SETTINGS S_HEX_LAYOUT, S_TUNING, S_STEP_E, S_STEP_NE, S_PRESS, S_SCALA, S_SCALA1, \
                            S_SCALA_LAST = S_SCALA1 + SCALA_MAX - 1, S_SNAP, S_SNAP_POINTS, \
                            S_MOVE_KEYS, S_VIEW_RESET, S_VIEW_X, S_VIEW_Y, S_VIEW_SCALE, S_VIEW_TURN, S_SIDE_MOVE, S_PLACE9,
/* Two fingers to slide: a finger's second (its id, 0: none, and where it is), and its slide: 0 none,
   1 two fingers in its key, 2 sliding (the two have left the key; one finger plays it on). */
#define KEYS_FINGER_EXTRA int mate, slide; float mx, my, held;
/* A Scala file's tuning is built again when its settings (or the tuning) change. */
static int scala_dirty = 1;
static int view_back;           /* Put the keyboard back: asked, done in tick() (outside the host's call) */
#define KEYS_ON_SETTING(i) do { if ((i) == S_TUNING || ((i) >= S_SCALA && (i) <= S_SCALA_LAST)) scala_dirty = 1; \
                                if ((i) == S_VIEW_RESET && set_[i] != 0) view_back = 1; } while (0)
#define KEYS_EXTRA_DEFAULTS [S_STEP_E] = 2, [S_STEP_NE] = 7, [S_PRESS] = 1, [S_SNAP_POINTS] = 4, [S_VIEW_SCALE] = 1, [S_SIDE_MOVE] = 1

#include "../grid/grid.c"
#include "tuning.h"

static void draw_slides(const Theme* th);

/* The colours past the Grid's own (Plain, Every degree, Chord tones, Root 4th 5th, By note name). */
enum { COL_WHITE = 5, COL_ACCIDENTALS, COL_RAINBOW, COL_DISTANCE };
/* A touch's size or force: off, as pressure (held, like a Lumatone's aftertouch), or as pressure and
   how hard the key was struck (its velocity) too. */
enum { PRESS_OFF, PRESS_PRESSURE, PRESS_BOTH };

/* ---- the notes ----------------------------------------------------------------------------------- */
/* The tuning the rail lists after the built-in ones: a Scala file's, which the MESH reads and gives
   as settings (scala: how many notes; scala1.. each one's cents, the last where it repeats). Until
   one is given, 12 equal. */
enum { HT_SCALA = HT_COUNT };
static HexTuning scala_t;
static double scala_table[HT_MAX_N];
static int scala_val[3], scala_ok;
static const HexTuning* tuning(void) {
    if ((int)set_[S_TUNING] != HT_SCALA) return ht_tuning((int)set_[S_TUNING]);
    if (scala_dirty) {
        double cents[HT_MAX_N];
        int n = (int)set_[S_SCALA];
        for (int k = 0; k < n && k < HT_MAX_N; k++) cents[k] = set_[S_SCALA1 + k];
        scala_ok = ht_scala(&scala_t, scala_table, scala_val, cents, n);
        scala_dirty = 0;
    }
    return scala_ok ? &scala_t : ht_tuning(HT_12);
}
/* The current half's key's root, in the tuning's steps (0..n-1 above C). */
static int key_step(void) { return ht_key_step(tuning(), key()); }

/* The layout's two steps as the keys lie on the surface now: to the right (e) and up to the right
   (d: from (q, r) to (q, r + 1); down to the right when the rows run downwards, flipped, so the
   notes are mirrored with them). Mirrored (the Grid's default, the notes climbing right to left),
   to the right is the layout's left: -E, and up to the right its up to the left: NE - E. */
static void steps(int* e, int* d) {
    int se, sne;
    ht_layout_steps(tuning(), (int)set_[S_HEX_LAYOUT], (int)set_[S_STEP_E], (int)set_[S_STEP_NE], &se, &sne);
    if (mirrored()) { *e = -se; *d = sne - se; }
    else { *e = se; *d = sne; }
}

/* Key k: its row and its place in the row (any, from -KOFF on), packed. */
#define KSTRIDE 1024
#define KOFF 512
static int kk(int r, int c) { return r * KSTRIDE + c + KOFF; }
static int kr(int k) { return floordiv(k, KSTRIDE); }
static int kc(int k) { return floormod(k, KSTRIDE) - KOFF; }

static int note_rc(int r, int c) {
    int e, d;
    steps(&e, &d);
    return ht_note_at(e, d, hv->kshift, c - floordiv(r, 2), r);
}
static float pitch_of(int note) { return (float)ht_pitch(tuning(), key(), key_step(), note); }
/* -1 for a key whose pitch MIDI can't play. */
static int note_of(int k) {
    int note = note_rc(kr(k), kc(k));
    float p = pitch_of(note);
    return p < 0 || p > 127 ? -1 : note;
}
/* Every key is a note of the tuning: these are only for the Grid's Your own notes (not offered). */
static int note_at(int k) { return k; }
static int index_of(int note) { return note; }
static int in_scale(int note) { return ht_in_scale(tuning(), hv->mask, note - key_step()); }

/* Unscrolled, the key in the middle of the half (its middle row, the middle of the row) plays the
   key's root nearest to target. */
static int mid_row(void) { return (int)floorf_(hv->shown / 2); }
static int shift_for(int target) {
    const HexTuning* t = tuning();
    int root = ht_root_near(t, key(), key_step(), (double)target);
    int e, d, r = mid_row();
    steps(&e, &d);
    return root - ht_note_at(e, d, 0, cols / 2 - floordiv(r, 2), r);
}

static int demo_note(void) {
    Half* was = hv;
    hv = &halves[0];
    int note = ht_root_near(tuning(), key(), key_step(), center[0] + 12.0 * set_[S_OCTAVE]);
    hv = was;
    return note;
}

/* ---- where the keys are ---------------------------------------------------------------------------- */
#define SQRT3_2 0.8660254f
/* A row is cols keys and half a key (every other row sits half a key over), and the spacing; rows
   are closer than keys across, as hexagons nest. */
static float ax0, ax1 = 400;            /* the keys' room across (upright), between the side strips */
static void fit(float gx, float gw) {
    ax0 = gx; ax1 = gx + gw;
    float g = clampf((float)set_[S_SPACING] / 100.0f, 0.02f, 0.3f);
    float size = clampf((float)set_[S_SIZE] / 100.0f, 0.5f, 1);
    cell = size * gw / ((float)cols + 0.5f + g);
    gap = cell * g;
    left = gx + (gw - (cell * ((float)cols + 0.5f) + gap)) / 2 + gap;
    row_h = cell * SQRT3_2;
}

/* ---- the view: the keyboard moved, sized and turned ---------------------------------------------- */
/* The keys lie on the plane as they always have (the Grid's layout, rows without end); the view puts
   the plane on the screen: about the middle of each half's room M, moved by T (upright, in keys at
   their unsized pitch), sized by s and turned by a. A point P of the plane is on the screen (upright)
   at  L = M + T + s R(a) (P - M). Drawn through the moved controls' transform (xf). */
#define VIEW_MIN 0.35f
#define VIEW_MAX 3.0f
#define VIEW_PAN 150.0f                   /* keys, either way */
static float view_s(void) { return clampf((float)set_[S_VIEW_SCALE], VIEW_MIN, VIEW_MAX); }
static float view_a(void) { return (float)set_[S_VIEW_TURN] * 3.14159265f / 180; }
static int view_moved(void) {
    return set_[S_VIEW_X] != 0 || set_[S_VIEW_Y] != 0 || view_s() != 1 || set_[S_VIEW_TURN] != 0;
}
static int moving(void) { return set_[S_MOVE_KEYS] != 0; }
static void view_mid(int h, float* mx, float* my) {
    const Half* a = &halves[h < nhalves ? h : 0];
    *mx = (ax0 + ax1) / 2; *my = (a->top + a->bot) / 2;
}
/* The plane to the screen (upright), on half h; and back. */
static void plane_to_screen(int h, float px, float py, float* x, float* y) {
    if (!view_moved()) { *x = px; *y = py; return; }
    float mx, my, s = view_s(), a = view_a(), c = cosf_(a), n = sinf_(a);
    view_mid(h, &mx, &my);
    float dx = px - mx, dy = py - my;
    *x = mx + (float)set_[S_VIEW_X] * cell + s * (dx * c - dy * n);
    *y = my + (float)set_[S_VIEW_Y] * cell + s * (dx * n + dy * c);
}
static void screen_to_plane(int h, float x, float y, float* px, float* py) {
    if (!view_moved()) { *px = x; *py = y; return; }
    float mx, my, s = view_s(), a = view_a(), c = cosf_(a), n = sinf_(a);
    view_mid(h, &mx, &my);
    float dx = (x - mx - (float)set_[S_VIEW_X] * cell) / s, dy = (y - my - (float)set_[S_VIEW_Y] * cell) / s;
    *px = mx + dx * c + dy * n;
    *py = my - dx * n + dy * c;
}
/* Drawing on half h's plane: through xf, as a moved control is drawn (none while it is not moved). */
static SideRect view_rect;
static void view_xf(int h) {
    if (!view_moved()) { xf = 0; return; }
    const Half* a = &halves[h < nhalves ? h : 0];
    float mx, my;
    view_mid(h, &mx, &my);
    float px = mx + (float)set_[S_VIEW_X] * cell, py = my + (float)set_[S_VIEW_Y] * cell;
    SideRect r = {C_COUNT, ax0, a->top, ax1 - ax0, a->bot - a->top, 1, tx(px, py), ty(px, py), view_s(), view_a()};
    view_rect = r;
    xf = &view_rect;
}
/* A touch on the current half's keys (upright) to its plane: none outside the keys' room. */
static int to_plane(float* x, float* y) {
    if (*x < ax0 || *x > ax1) return 0;
    screen_to_plane(half_index(), *x, *y, x, y);
    return 1;
}
/* A finger already on a key (or the second of two in one) moving: on its own half's plane. */
static void finger_to_plane(int id, float* x, float* y) {
    for (int k = 0; k < MAX_FINGERS; k++) {
        const Finger* f = &fingers[k];
        if (f->used && f->n != DEMO_N && (f->id == id || f->mate == id)) { screen_to_plane(f->half, *x, *y, x, y); return; }
    }
}
/* The scroll bar drags the rows as the screen shows them: sized, and the other way turned over. */
static float bar_scale(void) { return cosf_(view_a()) < -0.1f ? -view_s() : view_s(); }

/* The scroll goes as far as keeps the note in the middle of the half inside MIDI's notes. Straight
   up, two rows climb d + (d - e) steps; a layout whose rows do not climb (Bosanquet's in 12: up to
   the right a sharp, up to the left a flat, the same note two rows up) does not scroll. */
static void clamp_pos(void) {
    Half* was = hv;
    for (int h = 0; h < nhalves; h++) {
        hv = &halves[h];
        int e, d;
        steps(&e, &d);
        float per = (float)d - (float)e / 2;                   /* steps a row, straight up */
        float semis = per * (float)(ht_period(tuning()) / 100.0) / (float)tuning()->n;
        if (absf(semis) < 0.25f) { hv->pos = 0; continue; }
        float mid = pitch_of(note_rc(mid_row(), cols / 2));
        float a = (8 - mid) / semis, b = (120 - mid) / semis;
        float lo = a < b ? a : b, hi = a < b ? b : a;
        hv->pos = clampf(hv->pos, lo, hi);
    }
    hv = was;
}
static float SHARED scroll_of(int c) { (void)c; return hv->pos; }

static float key_w(void) { return cell - gap; }
/* A key's middle, upright. */
static float key_cx(int r, int c) { return left + ((float)c + 0.5f * (float)floormod(r, 2)) * cell + key_w() / 2; }
static float key_cy(int r) {
    float f = ((float)r - hv->pos + 0.5f) * row_h;
    return flipped_y() ? hv->top + f : hv->bot - f;
}
/* Split, a key is there only when it is wholly on its side of the line (hexagons reach past their
   row's pitch); drawn (seen) only if some of it is in the keys' room. */
static int key_shown(int r, int c, int seen) {
    if (nhalves < 2 && !seen) return 1;
    float x, y, R = key_w() / 1.7320508f * view_s();
    plane_to_screen(half_index(), key_cx(r, c), key_cy(r), &x, &y);
    if (nhalves > 1 && (y - R < hv->top - 0.5f || y + R > hv->bot + 0.5f)) return 0;
    return !seen || (x + R > ax0 && x - R < ax1 && y + R > hv->top && y - R < hv->bot);
}
static float square_x(int k) { return key_cx(kr(k), kc(k)) - key_w() / 2; }
static float square_y(int k) { return key_cy(kr(k)) - key_w() / 2; }

static int column_at(float x) {
    int c = (int)floorf_((x - left + gap / 2) / cell);
    return c < 0 || c >= cols ? -1 : c;
}

/* The key whose middle is nearest (the hexagons are the places nearest each middle), or none past
   the keys' edges. */
static int square_at(float x, float y) {
    float rf = (flipped_y() ? (y - hv->top) : (hv->bot - y)) / row_h + hv->pos - 0.5f;
    int r0 = (int)floorf_(rf), best = NO_KEY;
    float bd = cell * cell * 0.36f;              /* within 0.6 of a key of its middle */
    for (int r = r0 - 1; r <= r0 + 2; r++) {
        int c = roundi((x - left - key_w() / 2) / cell - 0.5f * (float)floormod(r, 2));
        if (c < -KOFF || c >= KOFF || !key_shown(r, c, 0)) continue;
        float dx = x - key_cx(r, c), dy = y - key_cy(r), dd = dx * dx + dy * dy;
        if (dd < bd) { bd = dd; best = kk(r, c); }
    }
    return best;
}

/* The six keys round one: to the right, then on round (upright, y down), each as its way across
   and up (a unit vector) and its note's steps above the middle key's. */
static void around(int e, int d, int off[6]) {
    /* Up to the right is (q, r + 1): d; rows running down (flipped), it is (q + 1, r - 1): e - d. */
    int ur = flipped_y() ? e - d : d, ul = ur - e;
    off[0] = e; off[1] = ur; off[2] = ul; off[3] = -e; off[4] = -ur; off[5] = -ul;
}

/* Sliding out of a key: across, the key beside it; up or down, the one of the two above (or below)
   on the side the finger is. */
static int bend_to(const Finger* f, int across, int dir) {
    int e, d, off[6];
    steps(&e, &d);
    around(e, d, off);
    if (across) return f->note + (dir > 0 ? off[0] : off[3]);
    int right = f->n == DEMO_N || f->sx >= f->rx + f->rs / 2;
    if (dir > 0) return f->note + (right ? off[1] : off[2]);
    return f->note + (right ? off[5] : off[4]);
}

/* With a glide: the way from the key's middle, split between the two keys round it either side of
   it, each taking the note toward its own as far as the finger has gone toward it (glide_curve:
   half way at the border). Across (to the keys beside) follows Glide up/down, the Grid's across;
   the four up and down, Glide left/right. */
static float glide_semis(const Finger* f, int n, float x, float y, float g) {
    (void)n;
    int e, d, off[6];
    steps(&e, &d);
    around(e, d, off);
    float dx = (x - (f->rx + f->rs / 2)) / cell, dy = (y - (f->ry + f->rs / 2)) / cell;
    float len = __builtin_sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return 0;
    const float PI = 3.14159265f, SIXTH = PI / 3;
    float a = atan2f_(-dy, dx);
    if (a < 0) a += 2 * PI;
    int j = (int)floorf_(a / SIXTH);
    if (j > 5) j = 5;
    float phi = a - (float)j * SIXTH;
    float s60 = SQRT3_2;
    float w[2] = {len * sinf_(SIXTH - phi) / s60, len * sinf_(phi) / s60};
    int dirs[2] = {j, (j + 1) % 6};
    float base = pitch_of(f->note), semis = 0;
    for (int i = 0; i < 2; i++) {
        int across = dirs[i] == 0 || dirs[i] == 3;
        if (set_[across ? S_GLIDE_UD : S_GLIDE_LR] == 0) continue;
        semis += glide_curve(clampf(w[i], 0, 1), g) * (pitch_of(f->note + off[dirs[i]]) - base);
    }
    return semis;
}

/* ---- a touch's size or force ------------------------------------------------------------------- */
/* A Lumatone key senses how far it is pressed, all the time: past the bottom of its travel that is
   aftertouch. A touch screen's nearest is how big (or, on some, how hard) the touch is: pressing
   harder spreads the finger. So pressing on, past how the touch came down, adds pressure; half as
   big again (or as hard) is all of it. */
static void press_shape(const Finger* f, Expr* e) {
    if (set_[S_PRESS] == PRESS_OFF || f->press0 <= 0) return;
    float more = clampf((f->press - f->press0) / (f->press0 * 0.5f + 0.02f), 0, 1);
    e->pressure = e->pressure + (1 - e->pressure) * more;
}
/* As velocity: how big or hard the touch was when it landed, from soft to full. */
static void press_strike(Finger* f) {
    if (set_[S_PRESS] != PRESS_BOTH || f->press0 <= 0) return;
    f->strike = clampf(0.15f + f->press0 * 1.2f, 0.1f, 1) * f->strike;
}

/* ---- how they look -------------------------------------------------------------------------- */
static unsigned hsv(float h, float s, float v) {
    h -= floorf_(h);
    float r = 0, g = 0, b = 0, i = floorf_(h * 6), fr = h * 6 - i;
    float p = v * (1 - s), q = v * (1 - s * fr), t = v * (1 - s * (1 - fr));
    switch ((int)i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return ((unsigned)(r * 255 + 0.5f) << 24) | ((unsigned)(g * 255 + 0.5f) << 16) | ((unsigned)(b * 255 + 0.5f) << 8) | 0xFFu;
}

/* Lumatone's way: the key's naturals white, its sharps one colour, its flats another, and those
   further round the chain of fifths (in tunings with more notes) others again. */
static const unsigned NATURAL = 0xEDEDF7FFu, SHARP = 0x4DABF7FFu, FLAT = 0x51CF66FFu, SHARP2 = 0x9775FAFFu,
                      FLAT2 = 0xFFA94DFFu, FURTHER = 0x8CA0B8FFu, BLACK_KEY = 0x3A3F4AFFu;

static unsigned note_colour(int note) {
    const HexTuning* t = tuning();
    int scheme = (int)set_[S_COLOURS], rel = floormod(note - key_step(), t->n);
    int p = ht_chain(t, rel);                     /* its place on the chain of fifths from the root */
    int iv = floormod(p * 7, 12);                 /* as an interval of 12 */
    switch (scheme) {
    case COL_DEGREES: return in_scale(note) ? DEGREE[iv] : 0;
    case COL_CHORD: return in_scale(note) && (iv == 0 || iv == 3 || iv == 4 || iv == 7) ? DEGREE[iv] : 0;
    case COL_PILLARS: return in_scale(note) && (iv == 0 || iv == 5 || iv == 7) ? DEGREE[iv] : 0;
    case COL_NAMES: return NAMED[floormod(ht_chain(t, floormod(note, t->n)) * 7, 12)];
    case COL_WHITE: return p >= -1 && p <= 5 ? NATURAL : BLACK_KEY;
    case COL_ACCIDENTALS:
        return p >= -1 && p <= 5 ? NATURAL : p >= 6 && p <= 12 ? SHARP : p <= -2 && p >= -8 ? FLAT
             : p >= 13 && p <= 19 ? SHARP2 : p <= -9 && p >= -15 ? FLAT2 : FURTHER;
    case COL_RAINBOW: return hsv((float)(ht_cents(t, rel) / ht_period(t)), 0.6f, 1);
    case COL_DISTANCE: {
        int a = p < 0 ? -p : p;
        return hsv(0.66f * (float)(a > 12 ? 12 : a) / 12.0f, p < 0 ? 0.45f : 0.7f, a > 12 ? 0.7f : 1);
    }
    default: return 0;
    }
}

static void note_name(char* out, int note, int octave) {
    const HexTuning* t = tuning();
    char* p = out;
    if (!ht_name(t, note, out)) p = put_int(out, floormod(note, t->n), 0);
    else while (*p) p++;
    if (octave) put_int(p, floordiv(note, t->n) - 1, 0);
}

/* A hexagon, upright (pointed up and down), its middle (x, y) and width w: on the surface. */
static void hexagon(float x, float y, float w, unsigned fill, unsigned stroke, float sw) {
    static const float CS[6] = {0, 0.8660254f, 0.8660254f, 0, -0.8660254f, -0.8660254f};
    static const float SN[6] = {-1, -0.5f, 0.5f, 1, 0.5f, -0.5f};
    float r = w / 1.7320508f, pts[12];
    for (int i = 0; i < 6; i++) to_surface(x + CS[i] * r, y + SN[i] * r, &pts[2 * i], &pts[2 * i + 1]);
    mesh_poly(6, pts, fill, stroke, sw);
}

/* The keys: as the Grid draws its squares (lit while held, a fading light after, the root's outline
   strongest, the same notes elsewhere marked), as hexagons. */
static void draw_keys(const Theme* th) {
    int flat = eink;
    float w = key_w(), R = w / 1.7320508f;
    unsigned light = flat ? BLACK : th->light;
    int n = tuning()->n;
    unsigned char sounding[HT_MAX_N] = {0};      /* which of the tuning's notes are playing, in any octave */
    int echo = (int)set_[S_ECHO];
    for (int k = 0; k < MAX_FINGERS; k++) if (fingers[k].used) sounding[floormod(fingers[k].note, n)] = 1;
    for (int k = 0; k < nlatched; k++) sounding[floormod(latched[k].note, n)] = 1;
    for (int h = 0; h < nhalves; h++) {
        hv = &halves[h];
        int ks = key_step();
        /* The rows and places the half's room shows: its corners on the plane. */
        float lo_x = 1e9f, hi_x = -1e9f, lo_r = 1e9f, hi_r = -1e9f;
        for (int j = 0; j < 4; j++) {
            float px, py;
            screen_to_plane(h, j & 1 ? ax1 : ax0, j & 2 ? hv->bot : hv->top, &px, &py);
            float rf = (flipped_y() ? (py - hv->top) : (hv->bot - py)) / row_h + hv->pos - 0.5f;
            if (px < lo_x) lo_x = px;
            if (px > hi_x) hi_x = px;
            if (rf < lo_r) lo_r = rf;
            if (rf > hi_r) hi_r = rf;
        }
        int r0 = (int)floorf_(lo_r) - 2, r1 = (int)floorf_(hi_r) + 2;
        int c0 = (int)floorf_((lo_x - left) / cell) - 2, c1 = (int)floorf_((hi_x - left) / cell) + 2;
        if (c0 < -KOFF) c0 = -KOFF;
        if (c1 > KOFF - 1) c1 = KOFF - 1;
        if (r1 - r0 > 400) r1 = r0 + 400;
        view_xf(h);
        for (int r = r0; r <= r1; r++) {
            float cy = key_cy(r);
            for (int c = c0; c <= c1; c++) {
                if (!key_shown(r, c, 1)) continue;
                int k = kk(r, c), note = note_of(k);
                if (note < 0) continue;
                float cx = key_cx(r, c);
                int root = floormod(note - ks, n) == 0;
                int off = !in_scale(note);
                int on = is_held(k), kept = is_kept(k);
                float glow = on || kept ? 1 : glow_of(k);
                unsigned colour = flat ? 0 : note_colour(note);
                unsigned lit = colour ? colour : light;
                if (!flat) {
                    hexagon(cx, cy, w, off ? mesh_alpha(th->square, 0.3f) : th->square, 0, 0);
                    if (colour && glow < 1) hexagon(cx, cy, w, mesh_alpha(colour, off ? 0.08f : th->bright ? 0.35f : 0.24f), 0, 0);
                }
                if (glow > 0) {
                    if (!flat && !th->bright) hexagon(cx, cy, w + 12, mesh_alpha(lit, 0.28f * glow), 0, 0);
                    hexagon(cx, cy, w, mesh_alpha(lit, (flat ? 1 : 0.92f) * glow), 0, 0);
                }
                unsigned edge;
                if (on || glow > 0.6f) edge = flat ? BLACK : colour ? WHITE : th->hot;
                else if (off) edge = mesh_alpha(colour ? colour : light, flat ? 0.25f : 0.14f);
                else if (colour) edge = mesh_alpha(colour, root ? 1 : 0.8f);
                else edge = root ? light : mesh_alpha(light, flat ? 0.8f : 0.38f);
                if (root && !flat && !on && !th->bright) hexagon(cx, cy, w + 6, 0, mesh_alpha(colour ? colour : light, 0.18f), 6);
                hexagon(cx, cy, w, 0, edge, off ? 1.0f : root ? 2.5f : 1.5f);
                int same = echo != ECHO_OFF && !on && !kept && glow < 0.5f && sounding[floormod(note, n)];
                unsigned ec = flat ? BLACK : colour ? colour : th->hot;
                if (same) {
                    if (echo == ECHO_RING) hexagon(cx, cy, w * 0.72f, 0, mesh_alpha(ec, 0.95f), 3);
                    else {
                        if (!flat) hexagon(cx, cy, w + 16, mesh_alpha(ec, 0.22f), 0, 0);
                        hexagon(cx, cy, w, 0, mesh_alpha(ec, 0.9f), 2.5f);
                    }
                }
                /* A latched key: a dot near its top, as long as it is held. */
                if (kept && !on)
                    g_circle(cx, cy - R * 0.55f, w * 0.06f, glow > 0.5f ? (flat ? WHITE : th->on_light) : (flat ? BLACK : th->hot), 0, 0);
                if (set_[S_LABELS] != 0) {
                    float size = w * 0.2f;
                    unsigned tc = glow > 0.5f ? (flat ? WHITE : colour ? 0x101010FFu : th->on_light)
                                : mesh_alpha(colour ? colour : light, off ? 0.35f : 0.9f);
                    char name[24];
                    note_name(name, note, root);
                    g_text(cx, cy + size * 0.35f, size, tc, MESH_TEXT_CENTER | (root ? MESH_TEXT_BOLD : 0), name);
                }
            }
        }
        xf = 0;
    }
    draw_slides(th);
    hv = &halves[0];
    /* The rows run on past the keys' room: what reaches past it, into the strips beside, is covered. */
    unsigned bg = flat ? WHITE : th->background;
    if (ax0 > 0.5f) g_rect(-2, -2, ax0 + 2, H + 4, 0, bg, 0, 0);
    if (ax1 < W - 0.5f) g_rect(ax1, -2, W - ax1 + 2, H + 4, 0, bg, 0, 0);
    if (nhalves > 1) {
        float y = (halves[1].bot + halves[0].top) / 2;
        g_line(ax0 + gap / 2, y, ax1 - gap / 2, y, mesh_alpha(flat ? BLACK : th->hot, 0.6f), 2);
    }
}

/* The scroll bar, as the Grid's: a mark and its octave at each row that holds the key's root. */
static void draw_bar(const Theme* th) {
    if (bar_x < 0) return;
    int flat = eink;
    unsigned ink = flat ? BLACK : th->light;
    int n = tuning()->n;
    for (int h = 0; h < nhalves; h++) {
        hv = &halves[h];
        int ks = key_step();
        g_rect(bar_x + 3, hv->top + 4, BAR - 6, hv->bot - hv->top - 8, 8, flat ? WHITE : mesh_alpha(th->square, 0.9f), mesh_alpha(ink, 0.35f), 1);
        /* Turned part way, the rows do not cross the bar: no marks. */
        float sn = sinf_(view_a());
        if (absf(sn) > 0.02f) continue;
        float pt, pb, px;
        screen_to_plane(h, (ax0 + ax1) / 2, hv->top, &px, &pt);
        screen_to_plane(h, (ax0 + ax1) / 2, hv->bot, &px, &pb);
        if (pt > pb) { float t = pt; pt = pb; pb = t; }
        float ra = (flipped_y() ? (pt - hv->top) : (hv->bot - pt)) / row_h + hv->pos, rb = (flipped_y() ? (pb - hv->top) : (hv->bot - pb)) / row_h + hv->pos;
        int r0 = (int)floorf_(ra < rb ? ra : rb) - 1, r1 = (int)floorf_(ra < rb ? rb : ra) + 1;
        if (r1 - r0 > 400) r1 = r0 + 400;
        for (int r = r0; r <= r1; r++) {
            int root = -1;
            for (int c = 0; c < cols; c++) { int note = note_of(kk(r, c)); if (note >= 0 && floormod(note - ks, n) == 0) { root = note; break; } }
            if (root < 0) continue;
            float x, y;
            plane_to_screen(h, key_cx(r, cols / 2), key_cy(r), &x, &y);
            if (y < hv->top + 10 || y > hv->bot - 10) continue;
            unsigned c = note_colour(root);
            unsigned mark = c && !flat ? c : ink;
            g_line(bar_x + 5, y, bar_x + BAR - 5, y, mesh_alpha(mark, 0.8f), 2);
            char name[24];
            note_name(name, root, 1);
            if (turned) g_text_at(bar_x + BAR / 2, y, 6, 0, 10, mark, MESH_TEXT_LEFT | MESH_TEXT_BOLD, name);
            else g_text_at(bar_x + BAR / 2, y, 0, -9, 10, mark, MESH_TEXT_CENTER | MESH_TEXT_BOLD, name);
        }
    }
    hv = &halves[0];
}

/* ---- two fingers to slide ------------------------------------------------------------------------ */
/* A key's middle and whether (x, y) is inside it: upright hexagons, pointed up and down. */
static float key_mx(const Finger* f) { return f->rx + f->rs / 2; }
static float key_my(const Finger* f) { return f->ry + f->rs / 2; }
static int in_key(const Finger* f, float x, float y) {
    float dx = absf(x - key_mx(f)), dy = absf(y - key_my(f)), w = f->rs, R = w / 1.7320508f;
    return dx <= w / 2 && dy <= R - dx / 1.7320508f;
}

static void pair_init(Finger* f) { f->mate = 0; f->slide = 0; f->mx = f->x; f->my = f->y; f->held = 0; }

/* A finger down on a key another finger holds, alone: the two are one, ready to slide. */
static int pair_down(int id, int h, int n, float x, float y) {
    for (int k = 0; k < MAX_FINGERS; k++) {
        Finger* f = &fingers[k];
        if (!f->used || f->half != h || f->n != n || f->mate || f->slide) continue;
        f->mate = id; f->slide = 1; f->mx = x; f->my = y;
        dirty = 1;
        return 1;
    }
    return 0;
}

/* Both fingers out of the key: it slides. The one further out plays it on; the other is let go of
   (it plays nothing more until it lifts and comes down again). */
static void slide_check(Finger* f) {
    if (f->slide != 1 || in_key(f, f->x, f->y) || in_key(f, f->mx, f->my)) return;
    float a = (f->x - key_mx(f)) * (f->x - key_mx(f)) + (f->y - key_my(f)) * (f->y - key_my(f));
    float b = (f->mx - key_mx(f)) * (f->mx - key_mx(f)) + (f->my - key_my(f)) * (f->my - key_my(f));
    int other = f->mate;
    if (b > a) { other = f->id; f->id = f->mate; f->x = f->mx; f->y = f->my; }
    if (nspent < MAX_FINGERS) spent[nspent++] = other;
    f->mate = 0;
    /* Sliding, it keeps the dynamics it had in the key: the slide bends, it does not change how
       loud (pressing harder still does). */
    f->held = level_at(f, f->rx + clampf(down_of(f, f->sx), 0, 1) * f->rs, 0);
    f->slide = 2;
}

/* Every finger on a key moves here: its second too. Nothing slides between notes; a lone finger
   keeps its note wherever it goes. */
static int pair_move(int id, float x, float y) {
    for (int k = 0; k < MAX_FINGERS; k++) {
        Finger* f = &fingers[k];
        if (!f->used || f->n == DEMO_N) continue;
        if (f->mate == id) { f->mx = x; f->my = y; slide_check(f); send(f); return 1; }
        if (f->id == id) { f->x = x; f->y = y; f->press = touch_press; slide_check(f); send(f); return 1; }
    }
    return 0;
}

/* The second lifted before they slid: back to one finger. The first lifted: the second plays on. */
static int pair_up(int id) {
    for (int k = 0; k < MAX_FINGERS; k++) {
        Finger* f = &fingers[k];
        if (!f->used || !f->mate) continue;
        if (f->mate == id) { f->mate = 0; f->slide = 0; dirty = 1; return 1; }
        if (f->id == id) { f->id = f->mate; f->x = f->mx; f->y = f->my; f->mate = 0; f->slide = 0; dirty = 1; return 1; }
    }
    return 0;
}

/* Snap: the points between one key's note and the next's a slide stops at (1: only the notes). */
static int snap_points(void) {
    if (set_[S_SNAP] == 0) return 0;
    int n = (int)set_[S_SNAP_POINTS];
    return n < 1 ? 1 : n > 48 ? 48 : n;
}

/* How far along its row the finger is from its key's middle, in keys (upright: across; on the
   turned keyboard, down the screen), snapped if Snap is on. */
static float slide_keys(const Finger* f) {
    float t = (f->x - key_mx(f)) / cell;
    int n = snap_points();
    if (n > 0) t = (float)roundi(t * (float)n) / (float)n;
    return t;
}

/* The bend, sliding: between the notes of the keys either side along the row, by how near it is
   to each (their own notes at their middles, so a just or unequal tuning bends to its own notes). */
static float slide_semis(const Finger* f) {
    if (f->slide != 2) return 0;
    int e, d;
    steps(&e, &d);
    float t = slide_keys(f), k = floorf_(t), fr = t - k;
    float p0 = pitch_of(f->note + (int)k * e), p1 = pitch_of(f->note + ((int)k + 1) * e);
    return clampf(p0 + (p1 - p0) * fr - pitch_of(f->note), -47, 47);
}

/* On the keys: two fingers in a key, a ring round it; sliding, the row it slides along with its
   snap points, and where it is. */
static void draw_slides(const Theme* th) {
    unsigned hot = eink ? BLACK : th->hot, ink = eink ? BLACK : th->light;
    for (int k = 0; k < MAX_FINGERS; k++) {
        Finger* f = &fingers[k];
        if (!f->used || f->n == DEMO_N || !f->slide) continue;
        hv = &halves[f->half < nhalves ? f->half : 0];
        view_xf(f->half);
        float cx = key_mx(f), cy = key_my(f), w = key_w();
        if (f->slide == 1) {
            hexagon(cx, cy, w + 10, 0, hot, 3);
            g_circle(f->mx, f->my, w * 0.12f, 0, mesh_alpha(hot, 0.8f), 2);
            continue;
        }
        float t = slide_keys(f), span = absf(t) + 1.2f;
        g_line(cx - span * cell, cy, cx + span * cell, cy, mesh_alpha(ink, 0.5f), 2);
        int n = snap_points();
        if (n > 0) {
            int a = (int)floorf_(-span * (float)n), b = (int)floorf_(span * (float)n);
            for (int j = a; j <= b && b - a < 400; j++) {
                float px = cx + (float)j / (float)n * cell, r = j % n == 0 ? 4.0f : 2.0f;
                g_circle(px, cy, r, mesh_alpha(ink, j % n == 0 ? 0.9f : 0.55f), 0, 0);
            }
        }
        g_circle(cx + t * cell, cy, w * 0.14f, mesh_alpha(hot, 0.85f), eink ? WHITE : th->background, 2);
    }
    xf = 0;
    hv = &halves[0];
}

/* ---- the Touch panel: its keys, inside them, the slide ------------------------------------------ */
enum { K_SNAP = 60, K_POINTS };
static int ed_points;          /* dragging the snap points' slider */

/* The snap points' slider: 1 to 24. */
static float points_slider(float x, float y, float w, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light;
    int n = (int)set_[S_SNAP_POINTS];
    if (n < 1) n = 1;
    if (n > 24) n = 24;
    char val[24];
    put_int(val, n, 0);
    mesh_text(x, y + 13, 11, mesh_alpha(ink, 0.85f), 0, n == 1 ? "Points between keys: only the notes" : "Points between keys");
    mesh_text(x + w, y + 13, 11, ink, MESH_TEXT_RIGHT | MESH_TEXT_BOLD, val);
    float ty = y + 28, t = (float)(n - 1) / 23.0f;
    mesh_rect(x, ty - 3, w, 6, 3, mesh_alpha(ink, 0.25f), 0, 0);
    mesh_rect(x, ty - 3, w * t, 6, 3, eink ? BLACK : mesh_alpha(ink, 0.8f), 0, 0);
    mesh_circle(x + w * t, ty, 9, eink ? BLACK : th->hot, eink ? WHITE : th->background, 2);
    hit(x - 6, y, w + 12, 40, K_POINTS, 0);
    return y + 46;
}
static void points_to(float x) {
    for (int k = 0; k < nhits; k++) if (hits[k].kind == K_POINTS) {
        float t = clampf((x - (hits[k].x + 6)) / (hits[k].w - 12), 0, 1);
        double v = (double)(1 + roundi(t * 23));
        if (v != set_[S_SNAP_POINTS]) keep(S_SNAP_POINTS, v);
        return;
    }
}

/* A key drawn on the panel (surface coordinates): its middle, width, name, and lit or not. */
static void panel_key(float x, float y, float w, int note, int lit, const Theme* th) {
    static const float CS[6] = {0, 0.8660254f, 0.8660254f, 0, -0.8660254f, -0.8660254f};
    static const float SN[6] = {-1, -0.5f, 0.5f, 1, 0.5f, -0.5f};
    unsigned ink = eink ? BLACK : th->light, c = eink ? 0 : note_colour(note);
    float r = w / 1.7320508f, pts[12];
    for (int i = 0; i < 6; i++) {
        /* turned, as the keyboard is: its points left and right */
        pts[2 * i] = turned ? x + SN[i] * r : x + CS[i] * r;
        pts[2 * i + 1] = turned ? y + CS[i] * r : y + SN[i] * r;
    }
    mesh_poly(6, pts, eink ? WHITE : lit ? mesh_alpha(c ? c : th->light, 0.35f) : th->square, c && !eink ? c : mesh_alpha(ink, 0.8f), lit ? 2.5f : 1.5f);
    char name[24];
    note_name(name, note, 1);
    mesh_text(x, y + 5, 13, ink, MESH_TEXT_CENTER | MESH_TEXT_BOLD, name);
}

/* Three keys along a row (on the turned keyboard, one above the other, as they are there), with
   the slide's line through them, its snap points and the note half way. */
static float slide_picture(float x, float y, float cw, const Theme* th) {
    unsigned ink = eink ? BLACK : th->light, hot = eink ? BLACK : th->hot;
    int e, d;
    steps(&e, &d);
    int mid = demo_note();
    /* Turned, the screen's down is the row's way on (+e); upright, its right is. */
    float w = turned ? 62 : clampf((cw - 40) / 3.4f, 50, 80), step = turned ? w * 1.02f : w * 1.08f;
    float cx = x + cw / 2, cy = turned ? y + step + w * 0.6f : y + w * 0.7f;
    for (int j = -1; j <= 1; j++) {
        float kx = turned ? cx : cx + (float)j * step, ky = turned ? cy + (float)j * step : cy;
        panel_key(kx, ky, w, mid + j * e, j == 0, th);
    }
    /* The line it slides along and its points: at the notes big, between them small. */
    float ax = turned ? cx + w * 0.75f : cx - step * 1.3f, ay = turned ? cy - step * 1.3f : cy + w * 0.75f;
    float bx = turned ? ax : cx + step * 1.3f, by = turned ? cy + step * 1.3f : ay;
    mesh_line(ax, ay, bx, by, mesh_alpha(ink, 0.5f), 2);
    int n = snap_points();
    for (int j = -1; j <= 1; j++) {
        float px = turned ? ax : cx + (float)j * step, py = turned ? cy + (float)j * step : ay;
        mesh_circle(px, py, 4.5f, ink, 0, 0);
        if (j < 1 && n > 1) for (int s = 1; s < n; s++) {
            float f = (float)s / (float)n;
            mesh_circle(turned ? px : px + f * step, turned ? py + f * step : py, 2.2f, mesh_alpha(ink, 0.6f), 0, 0);
        }
    }
    {
        /* On the line between the middle key and the one above (upright: before), half way between
           their notes: C to D, C♯. */
        float hx = turned ? ax : cx - step / 2, hy = turned ? cy - step / 2 : ay;
        mesh_circle(hx, hy, 5, hot, 0, 0);
        if (turned) mesh_text(hx + 9, hy + 4, 10, hot, MESH_TEXT_LEFT | MESH_TEXT_BOLD, "half way");
        else mesh_text(hx, hy + 18, 10, hot, MESH_TEXT_CENTER | MESH_TEXT_BOLD, "half way");
    }
    /* Two fingers in the middle key, below its name. */
    float fy = turned ? cy + w * 0.24f : cy + w * 0.24f;
    mesh_circle(cx - w * 0.17f, fy, 8, eink ? 0 : mesh_alpha(hot, 0.35f), hot, 2);
    mesh_circle(cx + w * 0.17f, fy, 8, eink ? 0 : mesh_alpha(hot, 0.35f), hot, 2);
    char a[48], b[48], *p;
    p = put(a, turned ? "Up: toward " : "Left: toward ");
    note_name(p, mid - e, 1);
    p = put(b, turned ? "Down: toward " : "Right: toward ");
    note_name(p, mid + e, 1);
    float ty = turned ? cy + step * 1.75f : cy + w * 1.4f;
    mesh_text(x + cw / 2, ty, 11, mesh_alpha(ink, 0.85f), MESH_TEXT_CENTER, a);
    mesh_text(x + cw / 2, ty + 16, 11, mesh_alpha(ink, 0.85f), MESH_TEXT_CENTER, b);
    return ty + 30;
}

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
    unsigned dim = mesh_alpha(ink, 0.8f);

    y = section(x, y, "INSIDE A KEY", th);
    {
        int on = set_[S_INSIDE_ON] != 0, dyn = set_[S_IN_DYN] != 0, tone = set_[S_IN_TONE] != 0;
        chip(x, y, cw, 30, on ? "Inside a key (dynamics, tone): on" : "Inside a key (dynamics, tone): off", on, th, K_INSIDE_ON, 0);
        y += 38;
        if (on) {
            float hw = (cw - 6) / 2;
            chip(x, y, hw, 30, dyn ? "Up/down: dynamics" : "Up/down: off", dyn, th, K_INSIDE, 0);
            chip(x + hw + 6, y, hw, 30, tone ? "Left/right: tone" : "Left/right: off", tone, th, K_INSIDE, 1);
            y += 38;
            if (dyn) y = range_slider(x, y, cw, th);
        }
        if (set_[S_DYN] < 0.99) {
            char dd[64];
            put(put_int(put(dd, "The Dynamics fader is at "), roundi((float)set_[S_DYN] * 100), 0), "%: set it to 100%");
            chip(x, y, cw, 30, dd, 0, th, K_DYN_FULL, 0);
            y += 38;
        }
    }
    y = section(x, y, "SLIDING: TWO FINGERS", th);
    mesh_text(x, y + 10, 11, dim, 0, "Put two fingers in a key, then slide both out of it:");
    mesh_text(x, y + 26, 11, dim, 0, "the note bends toward the keys either way, as near");
    mesh_text(x, y + 42, 11, dim, 0, "as you are to them. Then one finger plays it on, and");
    mesh_text(x, y + 58, 11, dim, 0, "the other can play another key. One finger: no bend.");
    y += 72;
    y = slide_picture(x, y, cw, th);
    {
        int snap = set_[S_SNAP] != 0;
        chip(x, y, cw, 30, snap ? "Snap to points: on" : "Snap to points: off", snap, th, K_SNAP, 0);
        y += 38;
        if (snap) y = points_slider(x, y, cw, th);
        else { mesh_text(x, y + 10, 11, dim, 0, "Free: any pitch between the keys' notes."); y += 24; }
    }
    ed_content = y + ed_scroll - top;
    mesh_rect(px + 1, py + 1, pw - 2, top - py - 1, 15, panel, 0, 0);
    mesh_rect(px + 1, bottom, pw - 2, py + ph - bottom - 1, 15, panel, 0, 0);
    mesh_rect(px, 0, pw, py, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py + ph, pw, SH - py - ph, 0, eink ? WHITE : 0x000000FFu, 0, 0);
    mesh_rect(px, py, pw, ph, 16, 0, mesh_alpha(ink, 0.6f), 1.5f);
    mesh_text(px + 18, py + 30, 17, ink, MESH_TEXT_BOLD, "Touch");
    mesh_text(px + 18, py + 47, 10, mesh_alpha(ink, 0.7f), 0, ed_content > ed_room ? "How a key answers your fingers. Drag to scroll" : "How a key answers your fingers");
    int k = 0;
    for (int j = 0; j < nhits; j++) if (hits[j].y + hits[j].h > top && hits[j].y < bottom) hits[k++] = hits[j];
    nhits = k;
    chip(px + pw - 94, py + 12, 80, 32, "Done", 1, th, K_DONE, 0);
}

static void ed_touch(int id, int phase, float x, float y) {
    if (phase != MESH_DOWN && id != ed_finger) return;
    if (phase == MESH_UP || phase == MESH_CANCEL) { ed_finger = -1; ed_points = 0; ed_thumb = -1; ed_dragging = 0; return; }
    if (phase == MESH_MOVE) {
        if (ed_points) { points_to(x); return; }
        if (ed_thumb >= 0) { range_to(x); return; }
        if (ed_dragging) {
            float most = ed_content - ed_room;
            ed_scroll = clampf(ed_scroll0 + (ed_from - y), 0, most > 0 ? most : 0);
            dirty = 1;
        }
        return;
    }
    ed_finger = id; ed_points = 0; ed_dragging = 0;
    for (int k = 0; k < nhits; k++) {
        Hit* h = &hits[k];
        if (x < h->x || x > h->x + h->w || y < h->y || y > h->y + h->h) continue;
        switch (h->kind) {
        case K_DONE: keep(S_TOUCH_EDIT, 0); break;
        case K_INSIDE_ON: keep(S_INSIDE_ON, set_[S_INSIDE_ON] != 0 ? 0 : 1); break;
        case K_INSIDE: { int s = h->arg ? S_IN_TONE : S_IN_DYN; keep(s, set_[s] != 0 ? 0 : 1); break; }
        case K_RANGE: ed_thumb = -1; range_to(x); break;
        case K_DYN_FULL: keep(S_DYN, 1); resend_all(); break;
        case K_SNAP: keep(S_SNAP, set_[S_SNAP] != 0 ? 0 : 1); break;
        case K_POINTS: ed_points = 1; points_to(x); break;
        default: break;
        }
        dirty = 1;
        return;
    }
    ed_dragging = 1; ed_from = y; ed_scroll0 = ed_scroll;
}

/* ---- moving the keyboard ------------------------------------------------------------------------ */
/* While "Move the keyboard (two fingers)" is on (the rail's Layout, or the Move button), a finger
   anywhere on the keys drags them, and two pinch them bigger or smaller and twist them round, the
   point between the fingers staying under them; nothing plays. The view is kept (in presets and takes
   too) when the last finger lifts. A third finger does nothing. */
enum { K_VIEW_DONE = 70, K_VIEW_BACK };
static struct { int n, id[2], half; float x[2], y[2], bx[2], by[2], t0x, t0y, s0, a0; } vg;

/* From the fingers as they are now and the view as it is. */
static void view_begin(void) {
    for (int i = 0; i < vg.n; i++) { vg.bx[i] = vg.x[i]; vg.by[i] = vg.y[i]; }
    vg.t0x = (float)set_[S_VIEW_X] * cell; vg.t0y = (float)set_[S_VIEW_Y] * cell;
    vg.s0 = view_s(); vg.a0 = view_a();
}

static void view_apply(void) {
    const float PI = 3.14159265f;
    float pmx = vg.bx[0], pmy = vg.by[0], qmx = vg.x[0], qmy = vg.y[0], ds = 1, da = 0;
    if (vg.n > 1) {
        float d0x = vg.bx[1] - vg.bx[0], d0y = vg.by[1] - vg.by[0], d1x = vg.x[1] - vg.x[0], d1y = vg.y[1] - vg.y[0];
        float l0 = __builtin_sqrtf(d0x * d0x + d0y * d0y), l1 = __builtin_sqrtf(d1x * d1x + d1y * d1y);
        if (l0 > 12) ds = l1 / l0;
        if (l0 > 12 && l1 > 12) da = atan2f_(d1y, d1x) - atan2f_(d0y, d0x);
        pmx = (vg.bx[0] + vg.bx[1]) / 2; pmy = (vg.by[0] + vg.by[1]) / 2;
        qmx = (vg.x[0] + vg.x[1]) / 2; qmy = (vg.y[0] + vg.y[1]) / 2;
    }
    float s = clampf(vg.s0 * ds, VIEW_MIN, VIEW_MAX);
    ds = s / vg.s0;
    /* Its turn, in degrees: square to the screen, or along a row of the hexagons, when nearly so. */
    float deg = (vg.a0 + da) * 180 / PI;
    deg -= 360 * floorf_(deg / 360);
    float q = 30 * floorf_(deg / 30 + 0.5f);
    if (absf(deg - q) < 4) deg = q;
    if (deg >= 359.95f) deg = 0;
    da = deg * PI / 180 - vg.a0;
    /* The point of the plane under the fingers' middle when they came down stays under it now. */
    float mx, my, c = cosf_(da), n = sinf_(da);
    view_mid(vg.half, &mx, &my);
    float vx = pmx - mx - vg.t0x, vy = pmy - my - vg.t0y;
    float px = qmx - mx - ds * (vx * c - vy * n), py = qmy - my - ds * (vx * n + vy * c);
    set_[S_VIEW_X] = clampf(px / cell, -VIEW_PAN, VIEW_PAN);
    set_[S_VIEW_Y] = clampf(py / cell, -VIEW_PAN, VIEW_PAN);
    set_[S_VIEW_SCALE] = s;
    set_[S_VIEW_TURN] = deg;
    dirty = 1;
}

static void view_keep(void) {
    keep(S_VIEW_X, set_[S_VIEW_X]); keep(S_VIEW_Y, set_[S_VIEW_Y]);
    keep(S_VIEW_SCALE, set_[S_VIEW_SCALE]); keep(S_VIEW_TURN, set_[S_VIEW_TURN]);
}

/* Put the keyboard back: unmoved, its keys their first size (the old Keys top to bottom and Key size
   too, which a preset from before the view may have set), its rows unscrolled. */
static void view_put_back(void) {
    keep(S_VIEW_X, 0); keep(S_VIEW_Y, 0); keep(S_VIEW_SCALE, 1); keep(S_VIEW_TURN, 0);
    keep(S_COLS, 5); keep(S_SIZE, 100);
    for (int h = 0; h < 2; h++) halves[h].pos = 0;
    vg.n = 0;
    layout();
}

static void view_tick(void) {
    if (!view_back) return;
    view_back = 0;
    view_put_back();
    keep(S_VIEW_RESET, 0);
}

static void view_stop(void) { vg.n = 0; }

static int view_find(int id) {
    for (int i = 0; i < vg.n; i++) if (vg.id[i] == id) return i;
    return -1;
}

/* A touch while moving: 1 if it is the view's (x, y upright; sx, sy on the surface). */
static int view_touch(int id, int phase, float x, float y, float sx, float sy) {
    if (phase == MESH_DOWN) {
        if (!moving()) return 0;
        for (int k = 0; k < nhits; k++) {
            Hit* t = &hits[k];
            if (sx < t->x || sx > t->x + t->w || sy < t->y || sy > t->y + t->h) continue;
            if (t->kind == K_VIEW_DONE) { keep(S_MOVE_KEYS, 0); vg.n = 0; }
            else if (t->kind == K_VIEW_BACK) view_put_back();
            else continue;
            if (nspent < MAX_FINGERS) spent[nspent++] = id;
            return 1;
        }
        if (vg.n >= 2) { if (nspent < MAX_FINGERS) spent[nspent++] = id; return 1; }
        if (vg.n == 0) { int h = half_at(y); vg.half = h < 0 ? 0 : h; }
        vg.id[vg.n] = id; vg.x[vg.n] = x; vg.y[vg.n] = y; vg.n++;
        view_begin();
        return 1;
    }
    int i = view_find(id);
    if (i < 0) return 0;
    if (phase == MESH_MOVE) {
        vg.x[i] = x; vg.y[i] = y;
        if (moving()) view_apply();
        return 1;
    }
    for (int j = i; j < vg.n - 1; j++) { vg.id[j] = vg.id[j + 1]; vg.x[j] = vg.x[j + 1]; vg.y[j] = vg.y[j + 1]; }
    vg.n--;
    if (vg.n > 0) view_begin();
    else view_keep();
    return 1;
}

/* While moving: a frame round the keys, and what to do, with Put back and Done. */
static void view_draw(const Theme* th) {
    if (!moving() || editing() || note_editing()) return;
    nhits = 0;
    unsigned ink = eink ? BLACK : th->light, hot = eink ? BLACK : th->hot;
    g_rect(ax0 + 3, 3, ax1 - ax0 - 6, H - 6, 14, 0, mesh_alpha(hot, 0.8f), 3);
    /* Across the top of the keys' room on the screen. */
    float rx = turned ? 0 : ax0, rw = turned ? SW : ax1 - ax0, top = turned ? ax0 : 0;
    float bw = rw - 24 < 560 ? rw - 24 : 560, bh = 52, bx = rx + (rw - bw) / 2, by = top + 12;
    mesh_rect(bx, by, bw, bh, 12, eink ? WHITE : mesh_alpha(th->background, 0.92f), mesh_alpha(hot, 0.8f), 1.5f);
    float cw = 80, cb = 96, cy = by + 10, ch = bh - 20;
    chip(bx + bw - 10 - cw, cy, cw, ch, "Done", 1, th, K_VIEW_DONE, 0);
    chip(bx + bw - 16 - cw - cb, cy, cb, ch, "Put back", 0, th, K_VIEW_BACK, 0);
    int wide = bw - 40 - cw - cb > 230;
    mesh_text(bx + 14, by + 22, wide ? 14 : 12, ink, MESH_TEXT_BOLD, "Moving the keyboard");
    mesh_text(bx + 14, by + 40, wide ? 11 : 9, mesh_alpha(ink, 0.8f), 0, wide ? "One finger drags it; two size and turn it" : "Drag; pinch; twist");
}
