#include "draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
#include "stb_truetype.h"

namespace draw {

Color rgba(uint32_t c) {
    return {float((c >> 24) & 255) / 255.f, float((c >> 16) & 255) / 255.f, float((c >> 8) & 255) / 255.f, float(c & 255) / 255.f};
}

static SDL_FColor fc(Color c) { return {c.r, c.g, c.b, c.a}; }

static void tris(SDL_Renderer* r, const std::vector<SDL_Vertex>& v) {
    if (!v.empty()) SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), nullptr, 0);
}

static int segments(float rad) { return std::max(8, std::min(64, int(rad * 0.8f))); }

// A convex outline as a fan of triangles around its first point.
static void fan(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, Color c) {
    if (pts.size() < 3 || c.a <= 0) return;
    std::vector<SDL_Vertex> v;
    for (size_t i = 1; i + 1 < pts.size(); i++)
        for (SDL_FPoint p : {pts[0], pts[i], pts[i + 1]}) v.push_back({p, fc(c), {0, 0}});
    tris(r, v);
}

static void polyline(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, bool closed, Color c, float width) {
    if (c.a <= 0 || width <= 0) return;
    size_t n = pts.size();
    for (size_t i = 0; i + (closed ? 0 : 1) < n; i++) {
        SDL_FPoint a = pts[i], b = pts[(i + 1) % n];
        line(r, a.x, a.y, b.x, b.y, c, width);
    }
}

static std::vector<SDL_FPoint> roundedRect(float x, float y, float w, float h, float rad) {
    std::vector<SDL_FPoint> p;
    rad = std::min(rad, std::min(w, h) / 2);
    if (rad <= 0.5f) return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    const float cx[4] = {x + w - rad, x + w - rad, x + rad, x + rad}, cy[4] = {y + rad, y + h - rad, y + h - rad, y + rad};
    const int seg = 6;
    for (int k = 0; k < 4; k++)
        for (int s = 0; s <= seg; s++) {
            float a = float(M_PI) * (-0.5f + 0.5f * float(k) + 0.5f * float(s) / seg);
            p.push_back({cx[k] + rad * std::cos(a), cy[k] + rad * std::sin(a)});
        }
    return p;
}

void fillRect(SDL_Renderer* r, float x, float y, float w, float h, Color c, float radius) { fan(r, roundedRect(x, y, w, h, radius), c); }
void strokeRect(SDL_Renderer* r, float x, float y, float w, float h, Color c, float width, float radius) {
    polyline(r, roundedRect(x, y, w, h, radius), true, c, width);
}

void line(SDL_Renderer* r, float x1, float y1, float x2, float y2, Color c, float width) {
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f || c.a <= 0) return;
    float nx = -dy / len * width / 2, ny = dx / len * width / 2;
    SDL_FPoint a{x1 + nx, y1 + ny}, b{x2 + nx, y2 + ny}, cc{x2 - nx, y2 - ny}, d{x1 - nx, y1 - ny};
    std::vector<SDL_Vertex> v;
    for (SDL_FPoint p : {a, b, cc, a, cc, d}) v.push_back({p, fc(c), {0, 0}});
    tris(r, v);
}

static std::vector<SDL_FPoint> ring(float cx, float cy, float rad) {
    std::vector<SDL_FPoint> p;
    int n = segments(rad);
    for (int k = 0; k < n; k++) {
        float a = 2 * float(M_PI) * float(k) / float(n);
        p.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a)});
    }
    return p;
}

void circle(SDL_Renderer* r, float cx, float cy, float rad, Color fill, Color stroke, float width) {
    auto p = ring(cx, cy, rad);
    fan(r, p, fill);
    polyline(r, p, true, stroke, width);
}

static void glow(SDL_Renderer* r, float cx, float cy, float rad, Color inner, Color outer) {
    auto p = ring(cx, cy, rad);
    std::vector<SDL_Vertex> v;
    for (size_t i = 0; i < p.size(); i++) {
        v.push_back({{cx, cy}, fc(inner), {0, 0}});
        v.push_back({p[i], fc(outer), {0, 0}});
        v.push_back({p[(i + 1) % p.size()], fc(outer), {0, 0}});
    }
    tris(r, v);
}

static void vgradient(SDL_Renderer* r, float x, float y, float w, float h, Color top, Color bottom) {
    SDL_Vertex a{{x, y}, fc(top), {0, 0}}, b{{x + w, y}, fc(top), {0, 0}}, c{{x + w, y + h}, fc(bottom), {0, 0}},
        d{{x, y + h}, fc(bottom), {0, 0}};
    tris(r, {a, b, c, a, c, d});
}

// ---- text ----------------------------------------------------------------------------------------
// Roboto, the same files odo on Android draws with (ui/fonts, carried in the binary), with Noto Sans
// Symbols for the few signs Roboto lacks (sharps and flats, arrows), as Android falls back to it.
// Glyphs are rasterised by stb_truetype at the size they show on screen (points x render scale) into
// an atlas texture per weight and size, and drawn as textured quads.

}  // namespace draw

namespace embedded {
extern const unsigned char Roboto_Regular[], Roboto_Bold[], NotoSansSymbols_Regular_Subset[], NotoSansSymbols_Bold_Subset[];
}

namespace draw {

namespace {

struct Face {
    stbtt_fontinfo info{};
    bool ok = false;
};

// [bold]: Roboto, then the symbols.
struct Fonts {
    Face roboto[2], symbols[2];
    Fonts() {
        auto load = [](Face& f, const unsigned char* data) { f.ok = stbtt_InitFont(&f.info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0; };
        load(roboto[0], embedded::Roboto_Regular);
        load(roboto[1], embedded::Roboto_Bold);
        load(symbols[0], embedded::NotoSansSymbols_Regular_Subset);
        load(symbols[1], embedded::NotoSansSymbols_Bold_Subset);
    }
};

Fonts& fonts() {
    static Fonts f;
    return f;
}

// A character's glyph: the face it comes from and its index there.
struct Mapped {
    const Face* face;
    int glyph;
};

Mapped mapChar(uint32_t cp, bool bold) {
    Fonts& f = fonts();
    const Face& main = f.roboto[bold ? 1 : 0];
    int g = main.ok ? stbtt_FindGlyphIndex(&main.info, int(cp)) : 0;
    if (g) return {&main, g};
    const Face& sym = f.symbols[bold ? 1 : 0];
    if (sym.ok && (g = stbtt_FindGlyphIndex(&sym.info, int(cp)))) return {&sym, g};
    return {&main, 0};   // Roboto's empty box
}

// The next character of a UTF-8 string; a broken sequence reads as U+FFFD.
uint32_t nextChar(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : -1;
    if (more < 0 || c > 0xF4) return 0xFFFD;
    uint32_t cp = c & (0x3Fu >> more);
    for (int k = 0; k < more; k++) {
        if (i >= s.size() || ((unsigned char)s[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    }
    return cp;
}

// A string as glyphs, each with how far the pen moves after it.
struct Run {
    Mapped m;
    float advance;   // in pixels at `px`, with the kerning to the next glyph
};

std::vector<Run> shape(const std::string& s, bool bold, float px) {
    std::vector<Run> out;
    for (size_t i = 0; i < s.size();) {
        Mapped m = mapChar(nextChar(s, i), bold);
        if (!m.face->ok) continue;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&m.face->info, m.glyph, &adv, &lsb);
        float scale = stbtt_ScaleForMappingEmToPixels(&m.face->info, px);
        if (!out.empty() && out.back().m.face == m.face)
            out.back().advance += float(stbtt_GetGlyphKernAdvance(&m.face->info, out.back().m.glyph, m.glyph)) * scale;
        out.push_back({m, float(adv) * scale});
    }
    return out;
}

float width(const std::vector<Run>& runs) {
    float w = 0;
    for (const Run& r : runs) w += r.advance;
    return w;
}

// The glyphs of one weight at one size, packed in rows into a texture.
struct Atlas {
    struct Glyph {
        int x = 0, y = 0, w = 0, h = 0;   // in the texture
        int xoff = 0, yoff = 0;          // from the pen on the baseline to the bitmap's top left
    };
    SDL_Texture* tex = nullptr;
    int dim = 0, rowX = 0, rowY = 0, rowH = 0;
    uint64_t used = 0;
    std::unordered_map<uint64_t, Glyph> glyphs;   // by face and glyph index
};

struct Cache {
    SDL_Renderer* renderer = nullptr;
    std::map<std::pair<int, bool>, Atlas> atlases;   // by size in half pixels, and weight
    uint64_t clock = 0;
};

Cache& cache() {
    static Cache c;
    return c;
}

constexpr size_t kMaxAtlases = 24;

Atlas* atlasFor(SDL_Renderer* r, int halfPx, bool bold) {
    Cache& c = cache();
    if (c.renderer != r) {              // a new renderer: the old one took its textures with it
        c.atlases.clear();
        c.renderer = r;
    }
    auto key = std::make_pair(halfPx, bold);
    auto it = c.atlases.find(key);
    if (it == c.atlases.end()) {
        if (c.atlases.size() >= kMaxAtlases) {   // too many sizes: let the longest unused go
            auto old = c.atlases.begin();
            for (auto j = c.atlases.begin(); j != c.atlases.end(); ++j)
                if (j->second.used < old->second.used) old = j;
            if (old->second.tex) SDL_DestroyTexture(old->second.tex);
            c.atlases.erase(old);
        }
        Atlas a;
        float px = float(halfPx) / 2;
        a.dim = 128;
        while (a.dim < 2048 && float(a.dim) < px * 18) a.dim *= 2;   // room for a few hundred glyphs
        a.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, a.dim, a.dim);
        if (!a.tex) return nullptr;
        SDL_SetTextureBlendMode(a.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(a.tex, SDL_SCALEMODE_LINEAR);
        it = c.atlases.emplace(key, std::move(a)).first;
    }
    it->second.used = ++c.clock;
    return &it->second;
}

// The glyph's place in the atlas, rasterising it the first time. False when the atlas is full: then
// it has been emptied, and glyphs looked up before are gone.
bool glyphIn(Atlas& a, const Mapped& m, float px, Atlas::Glyph& out) {
    uint64_t key = (uint64_t(reinterpret_cast<uintptr_t>(m.face)) << 20) ^ uint64_t(m.glyph);
    auto it = a.glyphs.find(key);
    if (it != a.glyphs.end()) { out = it->second; return true; }
    float scale = stbtt_ScaleForMappingEmToPixels(&m.face->info, px);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&m.face->info, m.glyph, scale, scale, &x0, &y0, &x1, &y1);
    Atlas::Glyph g;
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.xoff = x0;
    g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
        const int pad = 1;
        if (g.w + pad > a.dim || g.h + pad > a.dim) { a.glyphs[key] = Atlas::Glyph{}; out = {}; return true; }
        if (a.rowX + g.w + pad > a.dim) { a.rowX = 0; a.rowY += a.rowH; a.rowH = 0; }
        if (a.rowY + g.h + pad > a.dim) {            // full: start it again
            a.glyphs.clear();
            a.rowX = a.rowY = a.rowH = 0;
            return false;
        }
        std::vector<unsigned char> alpha(size_t(g.w) * size_t(g.h));
        stbtt_MakeGlyphBitmap(&m.face->info, alpha.data(), g.w, g.h, g.w, scale, scale, m.glyph);
        std::vector<uint32_t> rgba(alpha.size());
        for (size_t k = 0; k < alpha.size(); k++) {
            const unsigned char px4[4] = {255, 255, 255, alpha[k]};
            std::memcpy(&rgba[k], px4, 4);
        }
        g.x = a.rowX;
        g.y = a.rowY;
        SDL_Rect dst{g.x, g.y, g.w, g.h};
        SDL_UpdateTexture(a.tex, &dst, rgba.data(), g.w * 4);
        a.rowX += g.w + pad;
        a.rowH = std::max(a.rowH, g.h + pad);
    }
    a.glyphs[key] = g;
    out = g;
    return true;
}

}  // namespace

float textWidth(float size, const std::string& utf8, bool bold) {
    return size > 0 ? width(shape(utf8, bold, size)) : 0;
}

std::string fit(const std::string& utf8, float size, float room, bool bold) {
    if (textWidth(size, utf8, bold) <= room) return utf8;
    const std::string dots = "\xE2\x80\xA6";   // …
    // Whole characters only: cut at the start of one, and drop spaces left at the end.
    std::vector<size_t> starts;
    for (size_t i = 0; i < utf8.size();) { starts.push_back(i); nextChar(utf8, i); }
    size_t lo = 0, hi = starts.size() - 1;   // the longest prefix (in characters) that fits with the dots
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (textWidth(size, utf8.substr(0, starts[mid]) + dots, bold) <= room) lo = mid;
        else hi = mid - 1;
    }
    std::string head = lo < starts.size() ? utf8.substr(0, starts[lo]) : utf8;
    while (!head.empty() && head.back() == ' ') head.pop_back();
    return head + dots;
}

void text(SDL_Renderer* r, float x, float y, float size, Color c, const std::string& utf8, int align, bool bold) {
    if (utf8.empty() || c.a <= 0 || size <= 0) return;
    float sx = 1, sy = 1;
    SDL_GetRenderScale(r, &sx, &sy);
    if (sx <= 0) sx = 1;
    // The glyphs are made at the size they show in pixels (to the half pixel, so a few sizes share them).
    int halfPx = std::max(2, int(std::lround(size * sx * 2)));
    float px = float(halfPx) / 2;
    std::vector<Run> runs = shape(utf8, bold, px);
    float w = width(runs) / sx;
    if (align == 1) x -= w / 2;
    else if (align == 2) x -= w;
    Atlas* a = atlasFor(r, halfPx, bold);
    if (!a) return;
    // On whole pixels of the window (the viewport's place counts), so the glyphs stay sharp.
    SDL_Rect vp{0, 0, 0, 0};
    if (SDL_RenderViewportSet(r)) SDL_GetRenderViewport(r, &vp);
    float penX = std::round((x + float(vp.x)) * sx), baseY = std::round((y + float(vp.y)) * sx);
    std::vector<Atlas::Glyph> glyphs(runs.size());
    for (int attempt = 0; attempt < 2; attempt++) {   // a second go if the atlas filled up on the way
        bool ok = true;
        for (size_t k = 0; k < runs.size() && ok; k++) ok = glyphIn(*a, runs[k].m, px, glyphs[k]);
        if (ok) break;
        if (attempt == 1) return;
    }
    const float inv = 1.f / float(a->dim);
    SDL_FColor col = fc(c);
    std::vector<SDL_Vertex> v;
    v.reserve(runs.size() * 6);
    float pen = 0;
    for (size_t k = 0; k < runs.size(); k++) {
        const Atlas::Glyph& g = glyphs[k];
        if (g.w > 0 && g.h > 0) {
            float gx = penX + std::round(pen) + float(g.xoff), gy = baseY + float(g.yoff);
            float x0 = gx / sx - float(vp.x), y0 = gy / sx - float(vp.y);
            float x1 = x0 + float(g.w) / sx, y1 = y0 + float(g.h) / sx;
            float u0 = float(g.x) * inv, v0 = float(g.y) * inv, u1 = float(g.x + g.w) * inv, v1 = float(g.y + g.h) * inv;
            SDL_Vertex p0{{x0, y0}, col, {u0, v0}}, p1{{x1, y0}, col, {u1, v0}}, p2{{x1, y1}, col, {u1, v1}}, p3{{x0, y1}, col, {u0, v1}};
            for (const SDL_Vertex& q : {p0, p1, p2, p0, p2, p3}) v.push_back(q);
        }
        pen += runs[k].advance;
    }
    if (!v.empty()) SDL_RenderGeometry(r, a->tex, v.data(), int(v.size()), nullptr, 0);
}

// ---- a controller's draw list ------------------------------------------------------------------

namespace {
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok() const { return p + 4 <= end; }
    uint32_t u() { uint32_t v = 0; if (p + 4 <= end) std::memcpy(&v, p, 4); p += 4; return v; }
    float f() { uint32_t v = u(); float x; std::memcpy(&x, &v, 4); return x; }
};
}  // namespace

void list(SDL_Renderer* r, const std::vector<uint8_t>& bytes, float w, float h) {
    Reader in{bytes.data(), bytes.data() + bytes.size()};
    while (in.ok()) {
        switch (in.u()) {
            case 1: fillRect(r, 0, 0, w, h, rgba(in.u())); break;
            case 2: {
                float x = in.f(), y = in.f(), ww = in.f(), hh = in.f(), rad = in.f();
                Color fill = rgba(in.u()), stroke = rgba(in.u());
                float sw = in.f();
                fillRect(r, x, y, ww, hh, fill, rad);
                if (sw > 0) strokeRect(r, x, y, ww, hh, stroke, sw, rad);
                break;
            }
            case 3: {
                float x = in.f(), y = in.f(), rad = in.f();
                Color fill = rgba(in.u()), stroke = rgba(in.u());
                circle(r, x, y, rad, fill, stroke, in.f());
                break;
            }
            case 4: {
                float x1 = in.f(), y1 = in.f(), x2 = in.f(), y2 = in.f();
                Color c = rgba(in.u());
                line(r, x1, y1, x2, y2, c, in.f());
                break;
            }
            case 5: {
                uint32_t n = in.u();
                Color fill = rgba(in.u()), stroke = rgba(in.u());
                float sw = in.f();
                if (n > 4096) return;
                std::vector<SDL_FPoint> pts(n);
                for (auto& p : pts) { p.x = in.f(); p.y = in.f(); }
                fan(r, pts, fill);
                if (sw > 0) polyline(r, pts, true, stroke, sw);
                break;
            }
            case 6: {
                float x = in.f(), y = in.f(), size = in.f();
                Color c = rgba(in.u());
                uint32_t flags = in.u(), n = in.u();
                if (n > 4096 || in.p + ((n + 3) & ~3u) > in.end) return;
                std::string s(reinterpret_cast<const char*>(in.p), n);
                in.p += (n + 3) & ~3u;
                text(r, x, y, size, c, s, int(flags & 3), (flags & 4) != 0);
                break;
            }
            case 7: {
                float x = in.f(), y = in.f(), ww = in.f(), hh = in.f();
                Color top = rgba(in.u());
                vgradient(r, x, y, ww, hh, top, rgba(in.u()));
                break;
            }
            case 8: {
                float x = in.f(), y = in.f(), rad = in.f();
                Color inner = rgba(in.u());
                glow(r, x, y, rad, inner, rgba(in.u()));
                break;
            }
            default: return;   // an operation this MESH doesn't know: stop rather than misread the rest
        }
    }
}

}  // namespace draw
