// The rail's settings model: how a controller's settings (its manifest's "settings") are shown and
// edited on a MESH's rail. Both apps use it, so they show and step them the same way.
// CONTROLLER_FORMAT.md ("Settings on the rail") has the rules.
#pragma once
#include <string>
#include <vector>
#include "mpe/package.h"

namespace mpe::rail {

enum class Widget { Switch, Chips, Slider };

struct Row {
    size_t index;                     // the setting's position in manifest["settings"] (what set_setting/setting() use)
    std::string id, label, group;     // group: the setting's "group" ("" if none)
    Widget widget;
    std::vector<std::string> choices; // Chips: one label per choice/step, in order (for a number: its formatted values)
    int chosen;                       // Chips: which is selected (nearest for a number), else -1
    double min, max, step;            // Slider/number: step 0 = continuous. A choice: 0, options-1, 1; a switch: 0, 1, 1
    double value;                     // the current value
    std::string display;              // the value as shown: "On"/"Off", the choice's name, or the formatted number (with its unit)
};

struct Group { std::string name; std::vector<Row> rows; };   // name "" for settings with no group

// The rail: non-hidden settings, grouped. Groups appear in the order of their first setting; rows
// keep manifest order within a group. values[i] is setting i's value; a missing one is its default.
std::vector<Group> groups(const Json& manifest, const std::vector<double>& values);
// Flat convenience: every shown row, in display order (groups flattened).
std::vector<Row> rows(const Json& manifest, const std::vector<double>& values);

// Editing, shared so both apps step identically. `setting` is one entry of manifest["settings"].
double stepped(const Json& setting, double value, int dir);  // one step on (+1) or back (-1): switch flips; choice cycles (wraps); number moves by step (step 0: (max-min)/20), clamped, snapped to the step grid from min
double fromFraction(const Json& setting, double t);          // slider position 0..1 -> value, snapped to step
double toFraction(const Json& setting, double value);
double chipValue(const Json& setting, int k);                // the value of chip k
std::string display(const Json& setting, double value);
std::string label(const Json& setting);                      // the name, without a trailing unit like " (%)" when display() carries it

// Helpers.
Widget widget(const Json& setting);
double snap(const Json& setting, double value);              // clamped to its range and snapped to its step (a choice to an option)
// groups() as compact JSON, for a MESH that draws in another language (Android, across JNI):
// [{"name":"Layout","rows":[{"index":0,"id":"cols","label":"Squares across","widget":"chips",
//   "choices":["2",...],"chosen":1,"min":2,"max":8,"step":1,"value":3,"display":"3"}, ...]}, ...]
// widget is "switch", "chips" or "slider".
std::string toJson(const std::vector<Group>& groups);

}  // namespace mpe::rail
