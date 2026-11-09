// Scala tunings (.scl files, https://www.huygens-fokker.org/scala/scl_format.html): read one, and
// give it to a controller that takes one. Both apps use this, so a file reads the same in each.
//
// A controller takes a Scala tuning with a setting of type "scala" (docs/formats/CONTROLLER_FORMAT.md,
// "Scala tunings"): its value is how many notes the tuning has before it repeats (0: none yet), and
// hidden number settings named after it, <id>1 .. <id>max, hold each note's cents above the first,
// the last of them the interval it repeats at (usually 1200, the octave). So a tuning is kept in a
// preset, and goes from one app to the other, as any other settings are.
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "mpe/package.h"

namespace mpe::scala {

struct Scale {
    std::string description;       // its first line: what it is
    std::vector<double> cents;     // the notes above the first, in cents; the last is what it repeats at
};

// Read a .scl file's text. False and *error (a sentence to show) if it isn't one.
bool parse(const std::string& text, Scale* out, std::string* error = nullptr);

// The most notes a "scala" setting takes (its "max", up to 1024).
int capacity(const Json& setting);

// The settings that give the controller this tuning, for the "scala" setting at `index`: each
// note's cents (unused places 0), then the count. Empty and *error if it has too many notes or the
// controller is missing a place for them.
std::vector<std::pair<size_t, double>> settings(const Json& manifest, size_t index, const Scale& scale,
                                                std::string* error = nullptr);

// The tuning the settings hold now, as the rail shows it: "None", or "12 notes, to 1200 ¢" (the
// interval it repeats at: "to 2/1 (1200 ¢)" when it is an octave).
std::string describe(const Json& manifest, size_t index, const std::vector<double>& values);

}  // namespace mpe::scala
