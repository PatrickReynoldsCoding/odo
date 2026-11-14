// Splits of the play area (mpe/split.h), as both apps share them: the dividers kept apart, each
// side's region, a touch near a divider, a drag, and each side's MIDI channels.
#include "check.h"
#include "mpe/split.h"

using namespace mpe::split;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    CHECK(kCount == 6 && kSplits[0].sides == 1 && kSplits[5].sides == 4, "six splits, Whole to Four");
    CHECK(evenly(0).empty() && evenly(1).size() == 1 && near(evenly(1)[0], 0.5), "Whole has no divider; side by side one, halfway");
    CHECK(evenly(5).size() == 2 && near(evenly(5)[0], 0.5) && near(evenly(5)[1], 0.5), "Four: one across, one down");
    CHECK(evenly(3).size() == 2 && near(evenly(3)[0], 1.0 / 3) && near(evenly(3)[1], 2.0 / 3), "three side by side: thirds");

    CHECK(dividers(1, {0.7}).size() == 1 && near(dividers(1, {0.7})[0], 0.7), "a divider where it was put");
    CHECK(near(dividers(1, {0.01})[0], kApart) && near(dividers(1, {0.99})[0], 1 - kApart), "kept from the edges");
    CHECK(dividers(1, {0.3, 0.4}).size() == 1 && near(dividers(1, {0.3, 0.4})[0], 0.5), "too many: evenly");
    std::vector<double> t = dividers(3, {0.5, 0.52});
    CHECK(t[1] - t[0] >= kApart - 1e-9, "two dividers kept apart (%.3f, %.3f)", t[0], t[1]);
    CHECK(near(dividers(1, {std::nan("")})[0], 0.5), "not a number: halfway");

    Region l = region(1, {0.6}, 0), r = region(1, {0.6}, 1);
    CHECK(near(l.x, 0) && near(l.w, 0.6) && near(r.x, 0.6) && near(r.w, 0.4) && near(l.h, 1), "side by side at 0.6");
    Region br = region(5, {0.4, 0.7}, 3);
    CHECK(near(br.x, 0.4) && near(br.w, 0.6) && near(br.y, 0.7) && near(br.h, 0.3), "Four: the bottom right");
    Region top = region(2, {0.25}, 0);
    CHECK(near(top.h, 0.25) && near(top.w, 1), "above and below: the top");

    CHECK(sideAt(1, {0.6}, 0.59, 0.5) == 0 && sideAt(1, {0.6}, 0.61, 0.5) == 1, "a touch on its side");
    CHECK(sideAt(5, {0.5, 0.5}, 0.2, 0.8) == 2, "Four: bottom left");

    // 1000 x 500 dp; side by side at 0.6: the divider at x 600.
    CHECK(dividerAt(1, {0.6}, 610, 250, 1000, 500, 14) == 0, "near the divider: it is dragged");
    CHECK(dividerAt(1, {0.6}, 630, 250, 1000, 500, 14) == -1, "farther: plays");
    CHECK(dividerAt(0, {}, 500, 250, 1000, 500, 14) == -1, "Whole has none");
    CHECK(dividerAt(5, {0.5, 0.5}, 300, 255, 1000, 500, 14) == 1, "Four: the one across the middle (down's index)");
    CHECK(dividerAt(5, {0.5, 0.5}, 505, 100, 1000, 500, 14) == 0, "Four: the one down the middle");

    std::vector<double> d = dragged(2, {0.5}, 0, 0.3, 0.8);
    CHECK(d.size() == 1 && near(d[0], 0.8), "above and below: dragged down to 0.8");
    d = dragged(1, {0.5}, 0, 0.99, 0.5);
    CHECK(near(d[0], 1 - kApart), "dragged to the edge: stops short");

    CHECK(channelsPer(1) == 15 && channelsPer(2) == 7 && channelsPer(4) == 3, "channels per side");
    CHECK(firstChannel(1, 2) == 8 && firstChannel(3, 4) == 10, "each side's first channel");
    return finish();
}
