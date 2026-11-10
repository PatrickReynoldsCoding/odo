/*
 * The Tonnetz as a MESH controller: Euler's net of notes. Across is a fifth, up and to the
 * right a major third, up and to the left a minor third, so every small triangle is a triad
 * (pointing up major, down minor) and neighbouring triangles share two notes. Touch a node for
 * its note, the middle of a triangle for its chord; slide to the next triangle and the notes
 * the two share keep sounding.
 *
 * Node (i, j) is the note key + 7i + 4j. The cell (i, j) holds two triangles: pointing up,
 * (i, j) (i+1, j) (i, j+1), the major chord on node (i, j); pointing down, (i+1, j) (i, j+1)
 * (i+1, j+1), the minor chord on node (i, j+1).
 *
 * A port of odo's built-in Tonnetz (TonnetzUi.kt), the first controller in the package format.
 */
#include "mesh.h"

#define MAX_TOUCHES 10
#define MAX_NOTES 5
#define TRAIL 24
#define MAX_FADES 8

/* ---- settings (the manifest's, in order) ---------------------------------------------------- */
enum { S_KEY, S_OCTAVE, S_SIZE, S_CHORDS, S_BASS, S_STRUM, S_LABELS, S_LITKEY, S_TRAILS, S_COUNT };
static double set_[S_COUNT] = {0, 0, 7, 0, 0, 0, 0, 1, 1};
static const double STRUM_MS[] = {0, 25, 70, 140};

static int key(void) { return (int)set_[S_KEY]; }

/* ---- the surface ---------------------------------------------------------------------------- */
static float W = 400, H = 300, dx = 57, dy = 49;
static int eink;
static float center = 60;          /* the middle of the instrument, as a MIDI note */
static double now;
static int dirty = 1;

static float sqrtf_(float x) { return __builtin_sqrtf(x); }
static float floorf_(float x) { return __builtin_floorf(x); }
static int roundi(float x) { return (int)floorf_(x + 0.5f); }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int mod12(int v) { return ((v % 12) + 12) % 12; }

static void layout(void) {
    int size = (int)set_[S_SIZE];
    if (size < 2) size = 2;
    dx = W / (float)size;
    dy = dx * 0.8660254f;
    dirty = 1;
}

static int pc(int i, int j) { return mod12(key() + 7 * i + 4 * j); }
static float node_x(int i, int j) { return W / 2 + ((float)i + (float)j / 2) * dx; }
static float node_y(int j) { return H / 2 - (float)j * dy; }

/* What a touch is on: a node, or a triangle. */
typedef struct { int node; int i, j; int up; } Target;
static int same(Target a, Target b) { return a.node == b.node && a.i == b.i && a.j == b.j && (a.node || a.up == b.up); }
static float dist2(float ax, float ay, float bx, float by) { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); }

static void corners(Target t, int* ci, int* cj) {
    if (t.up) { ci[0] = t.i; cj[0] = t.j; ci[1] = t.i + 1; cj[1] = t.j; ci[2] = t.i; cj[2] = t.j + 1; }
    else { ci[0] = t.i + 1; cj[0] = t.j; ci[1] = t.i; cj[1] = t.j + 1; ci[2] = t.i + 1; cj[2] = t.j + 1; }
}

static Target hit(float x, float y) {
    float fj = (H / 2 - y) / dy;
    float fi = (x - W / 2) / dx - fj / 2;
    int ci = (int)floorf_(fi), cj = (int)floorf_(fj);
    Target tri = {0, ci, cj, (fi - (float)ci) + (fj - (float)cj) < 1.0f};
    int is[3], js[3];
    corners(tri, is, js);
    int best = 0;
    for (int k = 1; k < 3; k++)
        if (dist2(x, y, node_x(is[k], js[k]), node_y(js[k])) < dist2(x, y, node_x(is[best], js[best]), node_y(js[best]))) best = k;
    if (sqrtf_(dist2(x, y, node_x(is[best], js[best]), node_y(js[best]))) < dx * 0.27f) {
        Target n = {1, is[best], js[best], 0};
        return n;
    }
    return tri;
}

static void centre(Target t, float* x, float* y) {
    if (t.node) { *x = node_x(t.i, t.j); *y = node_y(t.j); return; }
    int is[3], js[3];
    corners(t, is, js);
    *x = (node_x(is[0], js[0]) + node_x(is[1], js[1]) + node_x(is[2], js[2])) / 3;
    *y = (node_y(js[0]) + node_y(js[1]) + node_y(js[2])) / 3;
}

/* A triangle's root (pitch class) and whether it is major. */
static int chord_root(Target t) { return t.up ? pc(t.i, t.j) : pc(t.i, t.j + 1); }

/* A pitch class played within the octave around the middle. */
static int fold(int p) {
    int low = roundi(center + 12.0f * (float)set_[S_OCTAVE]) - 6;
    return low + mod12(p - low);
}

static int notes_of(Target t, int* out) {
    if (t.node) { out[0] = fold(pc(t.i, t.j)); return 1; }
    int root = fold(chord_root(t)), n = 0;
    out[n++] = root;
    out[n++] = root + (t.up ? 4 : 3);
    out[n++] = root + 7;
    if (set_[S_CHORDS] == 1) out[n++] = root + (t.up ? 11 : 10);
    if (set_[S_BASS] != 0) out[n++] = root - 12;
    for (int k = 0; k < n; k++) out[k] = out[k] < 0 ? 0 : out[k] > 127 ? 127 : out[k];
    return n;
}

/* ---- fingers and their notes ------------------------------------------------------------------ */
typedef struct { int voice; int note; int started; double due; } Note;
typedef struct {
    int used, id;
    Target target;
    float x, y, ax, ay;           /* where it is, and where it came onto its target */
    Note notes[MAX_NOTES];
    int n;
    float trail[TRAIL * 2];
    int tn;
} Touch;
static Touch touches[MAX_TOUCHES];
static int next_voice = 1;
static float glow[12];
static const float LEVEL = 0.8f;

typedef struct { float pts[TRAIL * 2]; int n; float life; } Fade;
static Fade fades[MAX_FADES];

static Touch* find(int id) {
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used && touches[k].id == id) return &touches[k];
    return 0;
}

/* Rocking the finger inside its node or triangle shapes the notes: up brighter and a little
   louder, side to side (on a node) a vibrato. */
static void shape_of(Touch* t, float* pitch_off, float* pressure, float* slide) {
    float up = clampf((t->ay - t->y) / (dx * 0.5f), -1, 1);
    *pressure = clampf(LEVEL + 0.2f * up, 0.01f, 1);
    *slide = clampf(0.55f + 0.45f * up, 0, 1);
    *pitch_off = t->target.node ? clampf((t->x - t->ax) / (dx * 0.25f), -1, 1) * 0.35f : 0;
}

static void start(Touch* t, Note* v) {
    float off, pr, sl;
    shape_of(t, &off, &pr, &sl);
    mesh_note_on(v->voice, 0, (float)v->note + off, pr, sl, LEVEL, now);
    v->started = 1;
    glow[mod12(v->note)] = 1;
    dirty = 1;
}

static void stop_note(Note* v) {
    if (v->started) mesh_note_off(v->voice, now);
    v->started = 0;
}

static void reshape(Touch* t) {
    float off, pr, sl;
    shape_of(t, &off, &pr, &sl);
    for (int k = 0; k < t->n; k++)
        if (t->notes[k].started) mesh_note_change(t->notes[k].voice, (float)t->notes[k].note + off, pr, sl, now);
}

/* Sound t's target: notes it already plays keep sounding; the rest stop; new ones start
   (strummed, low first). */
static void voice(Touch* t) {
    int want[MAX_NOTES], wn = notes_of(t->target, want);
    /* sort low first */
    for (int a = 1; a < wn; a++) for (int b = a; b > 0 && want[b] < want[b - 1]; b--) { int s = want[b]; want[b] = want[b - 1]; want[b - 1] = s; }
    int kept = 0;
    for (int k = 0; k < t->n; k++) {
        int keep = 0;
        for (int w = 0; w < wn; w++) if (want[w] == t->notes[k].note) keep = 1;
        if (keep) t->notes[kept++] = t->notes[k];
        else stop_note(&t->notes[k]);
    }
    t->n = kept;
    double delay = STRUM_MS[(int)set_[S_STRUM] & 3];
    int step = 0;
    for (int w = 0; w < wn && t->n < MAX_NOTES; w++) {
        int have = 0;
        for (int k = 0; k < t->n; k++) if (t->notes[k].note == want[w]) have = 1;
        if (have) continue;
        Note* v = &t->notes[t->n++];
        v->voice = next_voice++;
        v->note = want[w];
        v->started = 0;
        v->due = now + delay * step++;
        if (v->due <= now) start(t, v);
    }
    reshape(t);
}

static void add_trail(Touch* t) {
    float x, y;
    centre(t->target, &x, &y);
    if (t->tn == TRAIL) {
        for (int k = 0; k < (TRAIL - 1) * 2; k++) t->trail[k] = t->trail[k + 2];
        t->tn--;
    }
    t->trail[t->tn * 2] = x;
    t->trail[t->tn * 2 + 1] = y;
    t->tn++;
}

/* Still near the old target (within its margin): a finger on a border does not flicker. */
static int near_old(Target old, float x, float y) {
    float cx, cy;
    centre(old, &cx, &cy);
    return sqrtf_(dist2(x, y, cx, cy)) < (old.node ? dx * 0.33f : dx * 0.36f);
}

static void lift(Touch* t) {
    for (int k = 0; k < t->n; k++) stop_note(&t->notes[k]);
    t->n = 0;
    for (int f = 0; f < MAX_FADES; f++) if (fades[f].life <= 0) {
        for (int k = 0; k < t->tn * 2; k++) fades[f].pts[k] = t->trail[k];
        fades[f].n = t->tn;
        fades[f].life = 1;
        break;
    }
    t->used = 0;
    dirty = 1;
}

/* ---- drawing ---------------------------------------------------------------------------------- */
static const char* NAMES[12] = {"C", "C\xE2\x99\xAF", "D", "E\xE2\x99\xAD", "E", "F", "F\xE2\x99\xAF", "G", "A\xE2\x99\xAD", "A",
                                "B\xE2\x99\xAD", "B"};
static int in_key(int p) { return (0xAB5 >> mod12(p - key())) & 1; }   /* the major scale */

static const unsigned TOP = 0x1A1626FFu, MID = 0x0E1014FFu, BOTTOM = 0x0B1418FFu;
static const unsigned ACCENT = 0xFF8A65FFu, MINOR = 0x4DD0E1FFu, WHITE = 0xFFFFFFFFu, BLACK = 0x000000FFu;

static void chord_name(Target t, char* out) {
    const char* r = NAMES[chord_root(t)];
    int n = 0;
    while (r[n]) { out[n] = r[n]; n++; }
    const char* q = set_[S_CHORDS] == 1 ? (t.up ? "maj7" : "m7") : (t.up ? "" : "m");
    for (int k = 0; q[k]; k++) out[n++] = q[k];
    out[n] = 0;
}

static int held_target(Target t) {
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used && same(touches[k].target, t)) return 1;
    return 0;
}

static void trail_lines(const float* p, int n, float alpha) {
    for (int k = 1; k < n; k++) {
        float a = alpha * (float)k / (float)n;
        mesh_line(p[2 * k - 2], p[2 * k - 1], p[2 * k], p[2 * k + 1], eink ? mesh_alpha(BLACK, a) : mesh_alpha(WHITE, 0.6f * a), 2.5f);
    }
}

static int nodraw;              /* no one sees it (MESH_NODRAW): draw nothing */
static void draw(void) {
    if (nodraw) { dirty = 0; return; }
    mesh_begin();
    if (eink) mesh_clear(WHITE);
    else {
        mesh_vgradient(0, 0, W, H / 2, TOP, MID);
        mesh_vgradient(0, H / 2, W, H / 2, MID, BOTTOM);
    }
    int sounding[12] = {0};
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used)
        for (int v = 0; v < touches[k].n; v++) if (touches[k].notes[v].started) sounding[mod12(touches[k].notes[v].note)] = 1;
    int jr = (int)(H / 2 / dy) + 2, ir = (int)(W / dx) + jr + 2;
    unsigned label = eink ? BLACK : WHITE;
    /* Triangles: the key's chords lit, the held ones bright, chord names in the middle. */
    for (int j = -jr; j <= jr; j++) for (int i = -ir; i <= ir; i++) for (int up = 1; up >= 0; up--) {
        Target t = {0, i, j, up};
        float cx, cy;
        centre(t, &cx, &cy);
        if (cx < -dx || cx > W + dx || cy < -dy || cy > H + dy) continue;
        int is[3], js[3];
        corners(t, is, js);
        float xy[6] = {node_x(is[0], js[0]), node_y(js[0]), node_x(is[1], js[1]), node_y(js[1]), node_x(is[2], js[2]), node_y(js[2])};
        unsigned col = up ? ACCENT : MINOR;
        int held = held_target(t);
        int lit = set_[S_LITKEY] != 0 && in_key(pc(is[0], js[0])) && in_key(pc(is[1], js[1])) && in_key(pc(is[2], js[2]));
        if (held) mesh_poly(3, xy, eink ? mesh_alpha(BLACK, 0.35f) : mesh_alpha(col, 0.45f), 0, 0);
        else if (lit) mesh_poly(3, xy, eink ? mesh_alpha(BLACK, 0.06f) : mesh_alpha(col, 0.10f), 0, 0);
        else if (!eink) mesh_poly(3, xy, mesh_alpha(col, 0.03f), 0, 0);
        if (set_[S_LABELS] == 0 && dx > 30) {
            char name[12];
            chord_name(t, name);
            float size = dx * 0.17f;
            unsigned c = held ? (eink ? BLACK : WHITE) : mesh_alpha(label, lit ? 0.75f : 0.35f);
            mesh_text(cx, cy + size * 0.35f, size, c, MESH_TEXT_CENTER | (held ? MESH_TEXT_BOLD : 0), name);
        }
    }
    /* The lines of the net: fifths across, thirds on the slants. */
    unsigned line = eink ? mesh_alpha(BLACK, 0.5f) : mesh_alpha(WHITE, 0.16f);
    for (int j = -jr; j <= jr; j++) for (int i = -ir; i <= ir; i++) {
        float x = node_x(i, j), y = node_y(j);
        if (x < -dx * 2 || x > W + dx * 2) continue;
        mesh_line(x, y, node_x(i + 1, j), y, line, 1);
        mesh_line(x, y, node_x(i, j + 1), node_y(j + 1), line, 1);
        mesh_line(x, y, node_x(i - 1, j + 1), node_y(j + 1), line, 1);
    }
    /* Trails: where each finger went, fading after it lifts. */
    if (set_[S_TRAILS] != 0) {
        for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used) {
            Touch* t = &touches[k];
            float p[TRAIL * 2 + 2];
            for (int q = 0; q < t->tn * 2; q++) p[q] = t->trail[q];
            p[t->tn * 2] = t->x; p[t->tn * 2 + 1] = t->y;
            trail_lines(p, t->tn + 1, 1);
        }
        for (int f = 0; f < MAX_FADES; f++) if (fades[f].life > 0) trail_lines(fades[f].pts, fades[f].n, fades[f].life);
    }
    /* Nodes: the notes, glowing while they sound, anywhere on the net. */
    float r = dx * 0.17f;
    for (int j = -jr; j <= jr; j++) for (int i = -ir; i <= ir; i++) {
        float x = node_x(i, j), y = node_y(j);
        if (x < -r || x > W + r || y < -r || y > H + r) continue;
        int p = pc(i, j), on = sounding[p];
        float g = glow[p] > (on ? 0.7f : 0) ? glow[p] : (on ? 0.7f : 0);
        if (!eink && g > 0) mesh_glow(x, y, r * 3, mesh_alpha(ACCENT, 0.6f * g), mesh_alpha(ACCENT, 0));
        Target nt = {1, i, j, 0};
        int held = held_target(nt);
        unsigned fill = eink ? ((on || held) ? BLACK : WHITE) : (on || held) ? ACCENT : p == key() ? MID : BOTTOM;
        unsigned stroke = eink ? BLACK : mesh_alpha(WHITE, in_key(p) ? 0.55f : 0.2f);
        mesh_circle(x, y, r, fill, stroke, p == key() ? 1.6f : 0.8f);
        if (set_[S_LABELS] != 2) {
            float size = r * 0.95f;
            unsigned c = eink ? ((on || held) ? WHITE : BLACK) : (on || held) ? BLACK : mesh_alpha(WHITE, in_key(p) ? 0.95f : 0.5f);
            mesh_text(x, y + size * 0.35f, size, c, MESH_TEXT_CENTER | (p == key() ? MESH_TEXT_BOLD : 0), NAMES[p]);
        }
    }
    /* Each finger: a ring where it is. */
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used)
        mesh_circle(touches[k].x, touches[k].y, dx * 0.08f, 0, eink ? BLACK : mesh_alpha(WHITE, 0.85f), 1.6f);
    mesh_end();
    dirty = 0;
}

/* ---- the interface ------------------------------------------------------------------------------- */
MESH_EXPORT(init) void init(float width, float height, int flags, double t) {
    W = width; H = height; eink = flags & MESH_EINK; nodraw = flags & MESH_NODRAW; now = t;
    layout();
    draw();
}

MESH_EXPORT(instrument) void instrument(int part, float center_note) {
    if (part == 0) { center = center_note; dirty = 1; }
}

MESH_EXPORT(setting) void setting(int index, double value) {
    if (index < 0 || index >= S_COUNT) return;
    set_[index] = value;
    if (index == S_SIZE) layout();
    dirty = 1;
}

MESH_EXPORT(touch) void touch(int id, int phase, float x, float y, float size, float force, double t) {
    (void)size; (void)force;
    now = t;
    if (phase == MESH_DOWN) {
        Touch* t0 = find(id);
        if (t0) lift(t0);
        for (int k = 0; k < MAX_TOUCHES; k++) if (!touches[k].used) {
            Touch* u = &touches[k];
            u->used = 1; u->id = id; u->x = u->ax = x; u->y = u->ay = y; u->n = 0; u->tn = 0;
            u->target = hit(x, y);
            add_trail(u);
            voice(u);
            break;
        }
    } else if (phase == MESH_MOVE) {
        Touch* u = find(id);
        if (!u) return;
        u->x = x; u->y = y;
        Target h = hit(x, y);
        if (!same(h, u->target) && !near_old(u->target, x, y)) {
            u->target = h; u->ax = x; u->ay = y;
            if (set_[S_TRAILS] != 0) add_trail(u);
            voice(u);
        } else reshape(u);
    } else {
        Touch* u = find(id);
        if (u) lift(u);
    }
    dirty = 1;
}

MESH_EXPORT(tick) void tick(double t) {
    float dt = (float)((t - now) / 1000.0);
    if (dt < 0) dt = 0;
    now = t;
    /* strummed notes whose time has come */
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used)
        for (int v = 0; v < touches[k].n; v++)
            if (!touches[k].notes[v].started && touches[k].notes[v].due <= now) start(&touches[k], &touches[k].notes[v]);
    if (!eink) {
        for (int p = 0; p < 12; p++) if (glow[p] > 0) { glow[p] -= dt * 1.2f; if (glow[p] < 0) glow[p] = 0; dirty = 1; }
        for (int f = 0; f < MAX_FADES; f++) if (fades[f].life > 0) { fades[f].life -= dt * 0.8f; dirty = 1; }
    } else {
        for (int p = 0; p < 12; p++) glow[p] = 0;
        for (int f = 0; f < MAX_FADES; f++) fades[f].life = 0;
    }
    if (dirty) draw();
}

MESH_EXPORT(stop) void stop(void) {
    for (int k = 0; k < MAX_TOUCHES; k++) if (touches[k].used) lift(&touches[k]);
}
