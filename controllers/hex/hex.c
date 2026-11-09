/*
 * The Hex keyboard as a MESH controller: hexagonal keys in an isomorphic layout, as on a Lumatone
 * or in Hexatone. Any interval, any chord, has the same shape wherever it is played. Its notes come
 * from a layout (Wicki-Hayden, Harmonic table, Bosanquet, or your own two steps) and a tuning (12,
 * 19, 22, 31, 41 or 53 equal, 5-limit just, Pythagorean, quarter-comma meantone): tuning.h.
 *
 * It is the Grid (../grid/grid.c) with hexagons for squares: every way a finger plays a key is the
 * Grid's own code, so it has all of it: dynamics and tone inside a key, sliding out past it to bend
 * or change the tone, vibrato, Slide between notes (with its glide), Latch, Hold and the other
 * controls beside it (moved, sized and turned with two fingers), Inside and Slide out as toggles,
 * the split, Flip 90°. This file gives only what a hexagon changes: where the keys are, which note
 * each plays and how it sounds, how they look, and a touch's size or force as pressure.
 *
 * Laid out as the Grid is: upright, rows of keys across, each row half a key to the right of the one
 * below (on an even row: rows are counted from 0, the bottom one unscrolled), drawn a quarter turned
 * (Flip 90°: upright). A key k counts along the rows, k = row * cols + place in the row, as the
 * Grid's squares do. In axial terms the key at place c in row r is at q = c - floor(r / 2): the key
 * up to the right of (q, r) is (q, r + 1), so its note is the layout's step up to the right (NE)
 * above it, and the key to its right, (q + 1, r), the step to the right (E) above.
 */
#define KEYS_HEX 1
#define CELL "key"
#define CELL_UPPER "KEY"
#define KEYS_MAX_COLS 16
/* Its own settings, after the Grid's (the manifest lists them in this order, at the end). */
#define SCALA_MAX 128           /* tuning.h's HT_MAX_N; the manifest's "scala" max */
#define KEYS_EXTRA_SETTINGS S_HEX_LAYOUT, S_TUNING, S_STEP_E, S_STEP_NE, S_PRESS, S_SCALA, S_SCALA1, \
                            S_SCALA_LAST = S_SCALA1 + SCALA_MAX - 1,
/* A Scala file's tuning is built again when its settings (or the tuning) change. */
static int scala_dirty = 1;
#define KEYS_ON_SETTING(i) do { if ((i) == S_TUNING || (i) >= S_SCALA) scala_dirty = 1; } while (0)
#define KEYS_EXTRA_DEFAULTS [S_STEP_E] = 2, [S_STEP_NE] = 7, [S_PRESS] = 1

#include "../grid/grid.c"
#include "tuning.h"

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

static int note_rc(int r, int c) {
    int e, d;
    steps(&e, &d);
    return ht_note_at(e, d, hv->kshift, c - floordiv(r, 2), r);
}
static float pitch_of(int note) { return (float)ht_pitch(tuning(), key(), key_step(), note); }
/* -1 for a key whose pitch MIDI can't play. */
static int note_of(int k) {
    int note = note_rc(floordiv(k, cols), floormod(k, cols));
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
static void fit(float gx, float gw) {
    float g = clampf((float)set_[S_SPACING] / 100.0f, 0.02f, 0.3f);
    float size = clampf((float)set_[S_SIZE] / 100.0f, 0.5f, 1);
    cell = size * gw / ((float)cols + 0.5f + g);
    gap = cell * g;
    left = gx + (gw - (cell * ((float)cols + 0.5f) + gap)) / 2 + gap;
    row_h = cell * SQRT3_2;
}

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
/* Split, a row is there only when it is wholly on its side of the line (hexagons reach past their
   row's pitch). */
static int row_shown(int r) {
    if (nhalves < 2) return 1;
    float cy = key_cy(r), R = key_w() / 1.7320508f;
    return cy - R >= hv->top - 0.5f && cy + R <= hv->bot + 0.5f;
}
static float square_x(int k) { return key_cx(floordiv(k, cols), floormod(k, cols)) - key_w() / 2; }
static float square_y(int k) { return key_cy(floordiv(k, cols)) - key_w() / 2; }

static int column_at(float x) {
    int c = (int)floorf_((x - left + gap / 2) / cell);
    return c < 0 || c >= cols ? -1 : c;
}

/* The key whose middle is nearest (the hexagons are the places nearest each middle), or none past
   the keys' edges. */
static int square_at(float x, float y) {
    float rf = (flipped_y() ? (y - hv->top) : (hv->bot - y)) / row_h + hv->pos - 0.5f;
    int r0 = (int)floorf_(rf), best = -100000;
    float bd = cell * cell * 0.36f;              /* within 0.6 of a key of its middle */
    for (int r = r0 - 1; r <= r0 + 2; r++) {
        int c = roundi((x - left - key_w() / 2) / cell - 0.5f * (float)floormod(r, 2));
        if (c < 0 || c >= cols || !row_shown(r)) continue;
        float dx = x - key_cx(r, c), dy = y - key_cy(r), dd = dx * dx + dy * dy;
        if (dd < bd) { bd = dd; best = r * cols + c; }
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
        int r0 = (int)floorf_(hv->pos) - 2, r1 = (int)floorf_(hv->pos + hv->shown) + 2;
        for (int r = r0; r <= r1; r++) {
            float cy = key_cy(r);
            if (!row_shown(r) || cy - R > hv->bot || cy + R < hv->top) continue;
            for (int c = 0; c < cols; c++) {
                int k = r * cols + c, note = note_of(k);
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
    }
    hv = &halves[0];
    if (nhalves > 1) {
        float y = (halves[1].bot + halves[0].top) / 2;
        g_line(left - gap / 2, y, left + cell * ((float)cols + 0.5f) - gap / 2, y, mesh_alpha(flat ? BLACK : th->hot, 0.6f), 2);
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
        int r0 = (int)floorf_(hv->pos) - 1, r1 = (int)floorf_(hv->pos + hv->shown) + 1;
        for (int r = r0; r <= r1; r++) {
            int root = -1;
            for (int c = 0; c < cols; c++) { int note = note_of(r * cols + c); if (note >= 0 && floormod(note - ks, n) == 0) { root = note; break; } }
            if (root < 0) continue;
            float y = key_cy(r);
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
