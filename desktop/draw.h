// Drawing a controller's draw list (docs/formats/CONTROLLER_FORMAT.md) with SDL3, and plain text for the rail.
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace draw {

struct Color { float r, g, b, a; };
Color rgba(uint32_t c);   // 0xRRGGBBAA

// Everything in the current viewport's coordinates (points).
void fillRect(SDL_Renderer* r, float x, float y, float w, float h, Color c, float radius = 0);
void strokeRect(SDL_Renderer* r, float x, float y, float w, float h, Color c, float width, float radius = 0);
void line(SDL_Renderer* r, float x1, float y1, float x2, float y2, Color c, float width);
void circle(SDL_Renderer* r, float cx, float cy, float rad, Color fill, Color stroke, float width);
// Text in Roboto (ui/fonts, as on Android), `size` points to the em, from the baseline at y.
// align: 0 left from x, 1 centred on x, 2 right to x.
void text(SDL_Renderer* r, float x, float y, float size, Color c, const std::string& utf8, int align = 0, bool bold = false);
// How wide text() draws it, in points.
float textWidth(float size, const std::string& utf8, bool bold = false);
// The text as it is if it is no wider than `room` points, else as much of it as fits, then "…".
std::string fit(const std::string& utf8, float size, float room, bool bold = false);

// A controller's draw list, onto a surface of w x h points at the current viewport.
void list(SDL_Renderer* r, const std::vector<uint8_t>& bytes, float w, float h);

}  // namespace draw
