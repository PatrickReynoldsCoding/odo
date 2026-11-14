// Splits of the play area into sides, each its own controller and sound(s), as both apps show them:
// their names, their sides' names, where the dividers between them are, and where each side's notes
// go. A side's controller is the whole play area and shows its own share of it, so a divider slides
// between them (as the touch pad's two pads); docs/formats/PRESET_FORMAT.md has `split` and `splitAt`.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace mpe::split {

struct Split { const char* name; int sides; int cols; const char* side[4]; };

// By index: a preset's `split`. The same words in both apps.
inline constexpr Split kSplits[] = {
    {"Whole", 1, 1, {"Whole"}},
    {"Side by side", 2, 2, {"Left", "Right"}},
    {"Above and below", 2, 1, {"Top", "Bottom"}},
    {"Three side by side", 3, 3, {"Left", "Middle", "Right"}},
    {"Three above each other", 3, 1, {"Top", "Middle", "Bottom"}},
    {"Four", 4, 2, {"Top left", "Top right", "Bottom left", "Bottom right"}},
};
inline constexpr int kCount = int(sizeof kSplits / sizeof kSplits[0]);
inline constexpr int kMaxSides = 4;
inline constexpr double kApart = 0.12;     // dividers are kept this far apart, and from the edges

inline int clampSplit(int n) { return std::clamp(n, 0, kCount - 1); }
inline int cols(int n) { return kSplits[clampSplit(n)].cols; }
inline int rows(int n) { const Split& s = kSplits[clampSplit(n)]; return (s.sides + s.cols - 1) / s.cols; }

// Split n's dividers evenly: its columns' (cols - 1, left to right), then its rows' (rows - 1, top down).
inline std::vector<double> evenly(int n) {
    std::vector<double> v;
    for (int c = 1; c < cols(n); c++) v.push_back(double(c) / cols(n));
    for (int r = 1; r < rows(n); r++) v.push_back(double(r) / rows(n));
    return v;
}

// The dividers of split n, from `at` if it has as many (else evenly), kept in order and apart.
inline std::vector<double> dividers(int n, const std::vector<double>& at) {
    std::vector<double> v = at.size() == evenly(n).size() ? at : evenly(n);
    int c = cols(n);
    for (size_t i = 0; i < v.size(); i++) {
        if (!std::isfinite(v[i])) v[i] = 0.5;
        int first = int(i) < c - 1 ? 0 : c - 1, last = int(i) < c - 1 ? c - 2 : int(v.size()) - 1;
        double lo = (int(i) > first ? v[i - 1] : 0) + kApart, hi = (int(i) < last ? v[i + 1] : 1) - kApart;
        v[i] = std::clamp(v[i], std::min(lo, hi), std::max(lo, hi));
    }
    return v;
}

// Where side k of split n shows, as fractions of the play area (0..1), with the dividers `at` (as
// dividers() keeps them).
struct Region { double x = 0, y = 0, w = 1, h = 1; };
inline Region region(int n, const std::vector<double>& at, int k) {
    std::vector<double> d = dividers(n, at);
    int c = cols(n), r = rows(n);
    auto edge = [&](int i, int count, bool across) {
        if (i <= 0) return 0.0;
        if (i >= count) return 1.0;
        return across ? d[size_t(i - 1)] : d[size_t(c - 1 + i - 1)];
    };
    int col = k % c, row = k / c;
    Region g;
    g.x = edge(col, c, true); g.w = edge(col + 1, c, true) - g.x;
    g.y = edge(row, r, false); g.h = edge(row + 1, r, false) - g.y;
    return g;
}

// The side of split n at (x, y), fractions of the play area; -1 if none.
inline int sideAt(int n, const std::vector<double>& at, double x, double y) {
    for (int k = 0; k < kSplits[clampSplit(n)].sides; k++) {
        Region g = region(n, at, k);
        if (x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h) return k;
    }
    return -1;
}

// The divider near (x, y) in a w x h play area (points or dp), within `grab` of it, as its index in
// the dividers; -1 if none. A divider is grabbed only beside a side it bounds.
inline int dividerAt(int n, const std::vector<double>& at, double x, double y, double w, double h, double grab) {
    if (kSplits[clampSplit(n)].sides < 2) return -1;
    int c = cols(n);
    for (int k = 0; k < kSplits[clampSplit(n)].sides; k++) {
        Region g = region(n, at, k);
        int col = k % c, row = k / c;
        double gx = g.x * w, gy = g.y * h, gw = g.w * w, gh = g.h * h;
        if (col > 0 && std::fabs(x - gx) < grab && y >= gy && y < gy + gh) return col - 1;
        if (row > 0 && std::fabs(y - gy) < grab && x >= gx && x < gx + gw) return c - 1 + row - 1;
    }
    return -1;
}

// Divider i of split n dragged to (x, y), fractions of the play area: the dividers then.
inline std::vector<double> dragged(int n, const std::vector<double>& at, int i, double x, double y) {
    std::vector<double> d = dividers(n, at);
    if (i < 0 || size_t(i) >= d.size()) return d;
    d[size_t(i)] = i < cols(n) - 1 ? x : y;
    return dividers(n, d);
}

// Side k of `count`'s MIDI channels: first .. first + per - 1 (of 1..15), so no two sides share one.
inline int channelsPer(int count) { return 15 / std::clamp(count, 1, kMaxSides); }
inline int firstChannel(int k, int count) { return 1 + k * channelsPer(count); }

}  // namespace mpe::split
