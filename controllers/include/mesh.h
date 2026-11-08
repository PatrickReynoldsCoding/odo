/*
 * Writing a MESH controller in C: the interface of docs/formats/CONTROLLER_FORMAT.md, for clang's wasm32
 * target, with no C library. Build with controllers/build.sh.
 *
 * A controller exports:
 *   void init(float width, float height, int flags, double t)  surface in dp; flags & MESH_EINK
 *   void instrument(int part, float center_note)              the middle of part's instrument
 *   void touch(int id, int phase, float x, float y, float size, float force, double t)
 *   void tick(double t)                                       every frame
 *   void setting(int index, double value)                     its manifest's settings, by position
 *   void stop(void)                                           let go of every note
 * Times are in ms, on the MESH's clock. Notes go out as soon as they are called; drawing waits
 * for the next frame.
 */
#ifndef MESH_H
#define MESH_H

#define MESH_EXPORT(name) __attribute__((export_name(#name)))
#define MESH_IMPORT(name) __attribute__((import_module("mesh"), import_name(#name)))

enum { MESH_EINK = 1 };
enum { MESH_DOWN = 0, MESH_MOVE = 1, MESH_UP = 2, MESH_CANCEL = 3 };

/* pitch: semitones (MIDI note numbers, with fractions for bends); pressure, slide, velocity 0..1. */
MESH_IMPORT(note_on) void mesh_note_on(int voice, int part, float pitch, float pressure, float slide, float velocity, double t);
MESH_IMPORT(note_change) void mesh_note_change(int voice, float pitch, float pressure, float slide, double t);
MESH_IMPORT(note_off) void mesh_note_off(int voice, double t);
/* A held note's own volume, 0..1 (1 when it starts): a fade, apart from pressure. */
MESH_IMPORT(note_volume) void mesh_note_volume(int voice, float volume, double t);
MESH_IMPORT(draw) void mesh_draw(const void* list, unsigned len);
/* Change one of its own settings (by its place in the manifest), as if set on the rail: the MESH
   keeps it and shows it. For an editor or a button on the surface. */
MESH_IMPORT(set_setting) void mesh_set_setting(int index, double value);

/* ---- the draw list: 32-bit words, colours 0xRRGGBBAA (alpha 0: none) ------------------------ */
enum {
    MESH_OP_CLEAR = 1,      /* colour */
    MESH_OP_RECT = 2,       /* x y w h radius fill stroke stroke_width */
    MESH_OP_CIRCLE = 3,     /* cx cy r fill stroke stroke_width */
    MESH_OP_LINE = 4,       /* x1 y1 x2 y2 colour width */
    MESH_OP_POLY = 5,       /* n fill stroke stroke_width, then n points x y */
    MESH_OP_TEXT = 6,       /* x y size colour flags bytes, then the UTF-8 padded to 4 */
    MESH_OP_VGRADIENT = 7,  /* x y w h top bottom: a rectangle shading from top to bottom */
    MESH_OP_GLOW = 8,       /* cx cy r inner outer: a disc shading from its centre outwards */
};
enum { MESH_TEXT_LEFT = 0, MESH_TEXT_CENTER = 1, MESH_TEXT_RIGHT = 2, MESH_TEXT_BOLD = 4 };

#ifndef MESH_DRAW_WORDS
#define MESH_DRAW_WORDS (64 * 1024)
#endif
static unsigned mesh_dl[MESH_DRAW_WORDS];
static unsigned mesh_dn;

static inline void mesh_w(unsigned v) { if (mesh_dn < MESH_DRAW_WORDS) mesh_dl[mesh_dn++] = v; }
static inline void mesh_f(float v) { union { float f; unsigned u; } c; c.f = v; mesh_w(c.u); }
static inline void mesh_begin(void) { mesh_dn = 0; }
static inline void mesh_end(void) { mesh_draw(mesh_dl, mesh_dn * 4); }

static inline void mesh_clear(unsigned c) { mesh_w(MESH_OP_CLEAR); mesh_w(c); }
static inline void mesh_rect(float x, float y, float w, float h, float r, unsigned fill, unsigned stroke, float sw) {
    mesh_w(MESH_OP_RECT); mesh_f(x); mesh_f(y); mesh_f(w); mesh_f(h); mesh_f(r); mesh_w(fill); mesh_w(stroke); mesh_f(sw);
}
static inline void mesh_circle(float x, float y, float r, unsigned fill, unsigned stroke, float sw) {
    mesh_w(MESH_OP_CIRCLE); mesh_f(x); mesh_f(y); mesh_f(r); mesh_w(fill); mesh_w(stroke); mesh_f(sw);
}
static inline void mesh_line(float x1, float y1, float x2, float y2, unsigned c, float w) {
    mesh_w(MESH_OP_LINE); mesh_f(x1); mesh_f(y1); mesh_f(x2); mesh_f(y2); mesh_w(c); mesh_f(w);
}
static inline void mesh_poly(int n, const float* xy, unsigned fill, unsigned stroke, float sw) {
    mesh_w(MESH_OP_POLY); mesh_w((unsigned)n); mesh_w(fill); mesh_w(stroke); mesh_f(sw);
    for (int i = 0; i < 2 * n; i++) mesh_f(xy[i]);
}
static inline void mesh_text(float x, float y, float size, unsigned c, int flags, const char* s) {
    unsigned n = 0;
    while (s[n]) n++;
    mesh_w(MESH_OP_TEXT); mesh_f(x); mesh_f(y); mesh_f(size); mesh_w(c); mesh_w((unsigned)flags); mesh_w(n);
    for (unsigned i = 0; i < n; i += 4) {
        unsigned v = 0;
        for (unsigned k = 0; k < 4 && i + k < n; k++) v |= (unsigned)(unsigned char)s[i + k] << (8 * k);
        mesh_w(v);
    }
}
static inline void mesh_vgradient(float x, float y, float w, float h, unsigned top, unsigned bottom) {
    mesh_w(MESH_OP_VGRADIENT); mesh_f(x); mesh_f(y); mesh_f(w); mesh_f(h); mesh_w(top); mesh_w(bottom);
}
static inline void mesh_glow(float x, float y, float r, unsigned inner, unsigned outer) {
    mesh_w(MESH_OP_GLOW); mesh_f(x); mesh_f(y); mesh_f(r); mesh_w(inner); mesh_w(outer);
}

/* A colour with its alpha replaced: a = 0..1. */
static inline unsigned mesh_alpha(unsigned c, float a) {
    int v = (int)(a * 255.0f + 0.5f);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (c & 0xFFFFFF00u) | (unsigned)v;
}

#endif
