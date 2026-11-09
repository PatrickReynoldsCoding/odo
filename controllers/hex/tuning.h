/*
 * The Hex keyboard's notes, as data: its tunings and its layouts. Plain C with no library, so the
 * controller (hex.c, built for WebAssembly) and the engine's tests (engine/tests/test_hex.cpp) use
 * the same code. docs/plans/HEX_CONTROLLER.md says where the choices come from.
 *
 * A note is a whole number of the tuning's steps, counted from C-1: in n steps to the octave,
 * note k is step k % n of octave k / n. The key's root sounds where 12-tone equal temperament has
 * it (its MIDI note), and every other note is tuned from it, so a tuning never moves the key.
 *
 * A layout is two steps: to the right (E) and up to the right (NE) on a keyboard of hexagons in
 * rows, each row half a key to the right of the one below (up to the left: NE - E). It is given as
 * intervals, as ratios of 2, 3 and 5 (exponents), and each tuning turns them into its own steps (its
 * val: the nearest whole steps to 2, 3 and 5), so one layout works in every tuning.
 */
#ifndef HEX_TUNING_H
#define HEX_TUNING_H

/* ---- small maths, as the controller has no library -------------------------------------------- */
static inline int ht_floordiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
static inline int ht_floormod(int a, int b) { return a - b * ht_floordiv(a, b); }
static inline int ht_round(double x) { return (int)(x < 0 ? x - 0.5 : x + 0.5); }
static inline double ht_fmod1200(double c) { c -= 1200.0 * (double)(long long)(c / 1200.0); return c < 0 ? c + 1200 : c; }

/* ---- tunings --------------------------------------------------------------------------------- */
/* Equal: n steps to the octave. Chain of fifths: 12 notes, each as many fifths of `fifth` cents from
   the root as its place on the chain (FIFTHS below). A table: the cents of each of n notes above the
   root. */
/* A Scala file's tuning (from the MESH, as settings) is a table too, with `period`, the interval it
   repeats at in cents (0: the octave, 1200), and `val`, its nearest steps to 2, 3 and 5 (0: worked
   out from n, as for an equal tuning). */
typedef struct { const char* name; int n; double fifth; const double* table; double period; const int* val; } HexTuning;
/* The most notes a tuning has before it repeats (a Scala file's, as the manifest's "scala" max). */
#define HT_MAX_N 128

/* 5-limit just intonation (Hexatone's 12-JI-5L): 1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8. */
static const double HT_JUST5[12] = {0, 111.7313, 203.9100, 315.6413, 386.3137, 498.0450,
                                    590.2237, 701.9550, 813.6863, 884.3587, 1017.5963, 1088.2687};

/* The order the rail lists them in: add one at the end (a preset keeps the index). */
enum { HT_12, HT_19, HT_22, HT_31, HT_41, HT_53, HT_JUST, HT_PYTHAGOREAN, HT_MEANTONE, HT_COUNT };
static const HexTuning HT_TUNINGS[HT_COUNT] = {
    {"12 equal", 12, 0, 0, 0, 0},
    {"19 equal", 19, 0, 0, 0, 0},
    {"22 equal", 22, 0, 0, 0, 0},
    {"31 equal", 31, 0, 0, 0, 0},
    {"41 equal", 41, 0, 0, 0, 0},
    {"53 equal", 53, 0, 0, 0, 0},
    {"Just (5-limit)", 12, 0, HT_JUST5, 0, 0},
    {"Pythagorean", 12, 701.955, 0, 0, 0},                  /* pure fifths, 3/2 */
    {"Meantone (1/4 comma)", 12, 696.5784, 0, 0, 0},        /* pure major thirds, 5/4 */
};
static inline double ht_period(const HexTuning* t) { return t->period > 0 ? t->period : 1200.0; }
static inline const HexTuning* ht_tuning(int i) { return &HT_TUNINGS[i < 0 || i >= HT_COUNT ? 0 : i]; }

/* Each semitone above the root, as a place on the chain of fifths (0 the root, 1 its fifth, -1 its
   fourth): the minor 2nd is a flat (-5), the tritone the augmented 4th (6). */
static const int HT_FIFTHS[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
/* The keys the rail names (C, C♯, D, E♭, E, F, F♯, G, A♭, A, B♭, B), on the chain from C. */
static const int HT_KEY_FIFTHS[12] = {0, 7, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};

/* The tuning's val: the nearest whole steps to the octave, 3 and 5. Twelve-note tunings use 12's. */
static inline void ht_val(const HexTuning* t, int v[3]) {
    if (t->val) { v[0] = t->val[0]; v[1] = t->val[1]; v[2] = t->val[2]; return; }
    double n = (double)t->n;
    v[0] = t->n;
    v[1] = ht_round(n * 1.5849625007211562);      /* log2 3 */
    v[2] = ht_round(n * 2.3219280948873622);      /* log2 5 */
}
/* An interval (exponents of 2, 3 and 5) in the tuning's steps. */
static inline int ht_steps(const HexTuning* t, const int e[3]) {
    int v[3];
    ht_val(t, v);
    return e[0] * v[0] + e[1] * v[1] + e[2] * v[2];
}
/* The fifth, in steps. */
static inline int ht_fifth(const HexTuning* t) { int v[3]; ht_val(t, v); return v[1] - v[0]; }

/* The cents of step i (0..n-1) above the root. */
static inline double ht_cents(const HexTuning* t, int i) {
    i = ht_floormod(i, t->n);
    if (t->table) return t->table[i];
    if (t->fifth > 0) return ht_fmod1200((double)HT_FIFTHS[i] * t->fifth);
    return 1200.0 * (double)i / (double)t->n;
}

/* The step (0..n-1) of a key (0..11, as the rail names them) above C. */
static inline int ht_key_step(const HexTuning* t, int key) {
    key = ht_floormod(key, 12);
    if (t->n == 12) return key;
    if (t->period > 0) return 0;              /* a Scala file's: its first note is the root */
    return ht_floormod(HT_KEY_FIFTHS[key] * ht_fifth(t), t->n);
}

/* Where the root's repeats are counted from: the key in the octave below MIDI note 0, so with an
   octave the root plays where 12-tone has it. A tuning that repeats at another interval (a Scala
   file's, Bohlen-Pierce's tritave) counts its repeats from the key above middle C, which plays there. */
static inline double ht_base(const HexTuning* t, int key) {
    return (double)(60 + ht_floormod(key, 12)) - 5.0 * ht_period(t) / 100.0;
}
/* The pitch of a note, as a MIDI note with fractions: the root (key, at step ks) where 12-tone has
   it, the rest tuned from it. */
static inline double ht_pitch(const HexTuning* t, int key, int ks, int note) {
    int rel = note - ks;
    return ht_base(t, key) + ht_period(t) / 100.0 * (double)ht_floordiv(rel, t->n) + ht_cents(t, rel) / 100.0;
}
/* The note nearest to a pitch that is a root (key at step ks): its octave of it. */
static inline int ht_root_near(const HexTuning* t, int key, int ks, double pitch) {
    int oct = ht_round((pitch - ht_base(t, key)) / (ht_period(t) / 100.0));
    return ks + oct * t->n;
}

/* Where step i (0..n-1) above the root is on the chain of fifths: the place nearest the root,
   sharps first on a tie (12 notes: HT_FIFTHS). A step the fifth never reaches (in some Scala files'
   tunings; none of the others) is HT_OFF_CHAIN, past any sharp or flat. */
#define HT_OFF_CHAIN 99
static inline int ht_chain(const HexTuning* t, int i) {
    i = ht_floormod(i, t->n);
    if (t->n == 12) return HT_FIFTHS[i];
    int f = ht_fifth(t);
    for (int d = 0; d <= t->n; d++) {
        if (ht_floormod(d * f, t->n) == i) return d;
        if (ht_floormod(-d * f, t->n) == i) return -d;
    }
    return HT_OFF_CHAIN;
}

/* Is step i above the root in the scale? mask: bit d for d semitones above the root (12 bits, all
   set: every step). Other tunings take each of the scale's notes by its place on the chain of fifths. */
static inline int ht_in_scale(const HexTuning* t, int mask, int i) {
    i = ht_floormod(i, t->n);
    if ((mask & 0xFFF) == 0xFFF) return 1;
    if (t->n == 12) return (mask >> i) & 1;
    int f = ht_fifth(t);
    for (int d = 0; d < 12; d++) if (((mask >> d) & 1) && ht_floormod(HT_FIFTHS[d] * f, t->n) == i) return 1;
    return 0;
}

/* A note's name, its letter and sharps or flats (UTF-8), from its place on the chain of fifths
   from C; 0 when it would need more than two of them (the caller shows its step instead). */
static inline int ht_name(const HexTuning* t, int note, char* out) {
    static const char* NAMES12[12] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G",
                                      "A\xE2\x99\xAD", "A", "B\xE2\x99\xAD", "B"};
    char* p = out;
    if (t->n == 12) {
        for (const char* s = NAMES12[ht_floormod(note, 12)]; *s; s++) *p++ = *s;
        *p = 0;
        return 1;
    }
    if (t->period > 0) { *p = 0; return 0; }   /* a Scala file's other than 12 notes: by step */
    /* The chain from C: F C G D A E B are -1..5; each 7 further a sharp more (or a flat). */
    int c = ht_chain(t, ht_floormod(note, t->n));
    int acc = ht_floordiv(c + 1, 7);
    if (acc > 2 || acc < -2) { *p = 0; return 0; }
    *p++ = "FCGDAEB"[ht_floormod(c + 1, 7)];
    for (int k = 0; k < (acc < 0 ? -acc : acc); k++) {
        const char* s = acc > 0 ? "\xE2\x99\xAF" : "\xE2\x99\xAD";
        while (*s) *p++ = *s++;
    }
    *p = 0;
    return 1;
}

/* ---- layouts --------------------------------------------------------------------------------- */
/* Two intervals, to the right and up to the right, as exponents of 2, 3 and 5. */
typedef struct { const char* name; int e[3], ne[3]; } HexLayout;
enum { HL_WICKI, HL_HARMONIC, HL_BOSANQUET, HL_OWN, HL_COUNT };
static const HexLayout HT_LAYOUTS[HL_OWN] = {
    /* Wicki-Hayden: whole tones (9/8) along a row, fifths (3/2) up to the right, fourths up to the left. */
    {"Wicki-Hayden", {-3, 2, 0}, {-1, 1, 0}},
    /* Harmonic table: major thirds (5/4) up to the right, minor thirds (6/5) up to the left, so the
       row is their difference (25/24, a semitone in 12) and two rows up a fifth. */
    {"Harmonic table", {-3, -1, 2}, {-2, 0, 1}},
    /* Bosanquet (Lumatone's standard): whole tones along a row, the sharp (the apotome, 2187/2048)
       up to the right, so the diatonic semitone (256/243) is down to the right. */
    {"Bosanquet", {-3, 2, 0}, {-11, 7, 0}},
};

/* A layout's two steps in a tuning: to the right and up to the right. Your own: as given. */
static inline void ht_layout_steps(const HexTuning* t, int layout, int own_e, int own_ne, int* e, int* ne) {
    if (layout < 0 || layout >= HL_OWN) { *e = own_e; *ne = own_ne; return; }
    *e = ht_steps(t, HT_LAYOUTS[layout].e);
    *ne = ht_steps(t, HT_LAYOUTS[layout].ne);
}

/* The note of the key at (q, r): q along the row (axial: the key up to the right has the same q), r
   rows up; `base` the note of (0, 0). */
static inline int ht_note_at(int e, int ne, int base, int q, int r) { return base + q * e + r * ne; }

/* The greatest common divisor of the two steps: a layout reaches only multiples of it (Harmonic
   table in 31 equal: every other step). */
static inline int ht_gcd(int a, int b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int t = a % b; a = b; b = t; }
    return a;
}

/* ---- a Scala file's tuning ---------------------------------------------------------------------- */
/* Its step nearest to `cents` above the root (counting on through the repeats). */
static inline int ht_nearest(const HexTuning* t, double cents) {
    double P = ht_period(t);
    int best = 0;
    double bd = 1e300;
    int top = t->n * ((int)(cents / P) + 2);
    if (top > 20000) top = 20000;
    for (int k = 0; k <= top; k++) {
        double c = P * (double)ht_floordiv(k, t->n) + ht_cents(t, k);
        double d = c > cents ? c - cents : cents - c;
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}
/* Fill *t from a Scala file's notes: cents[0..n-1] above the first, the last where it repeats.
   table and val are the tuning's own (HT_MAX_N and 3 long). 0 if they can't make a tuning. */
static inline int ht_scala(HexTuning* t, double* table, int* val, const double* cents, int n) {
    if (n < 1 || n > HT_MAX_N || !(cents[n - 1] >= 1.0)) return 0;
    table[0] = 0;
    for (int k = 1; k < n; k++) table[k] = cents[k - 1];
    t->name = "Scala file";
    t->n = n;
    t->fifth = 0;
    t->table = table;
    t->period = cents[n - 1];
    t->val = 0;
    val[0] = ht_nearest(t, 1200.0);
    val[1] = ht_nearest(t, 1901.9550008653874);
    val[2] = ht_nearest(t, 2786.3137138648348);
    t->val = val;
    return 1;
}

#endif
