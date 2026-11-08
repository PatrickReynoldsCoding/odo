// MESH packages: instruments (.odoi) and controllers (.odoc), as docs/formats/INSTRUMENT_FORMAT.md and
// docs/formats/CONTROLLER_FORMAT.md describe. A package is a zip with a JSON manifest.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mpe {

// A JSON value: just enough for manifests.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;   // in order

    const Json& operator[](const char* key) const;  // a missing key: a Null value
    const Json& operator[](size_t i) const;
    size_t size() const { return type == Array ? a.size() : type == Object ? o.size() : 0; }
    std::string str(const std::string& def = "") const { return type == String ? s : def; }
    double num(double def = 0) const { return type == Number ? n : type == Bool ? (b ? 1 : 0) : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : type == Number ? n != 0 : def; }
    static Json parse(const std::string& text, std::string* error = nullptr);
};

// A package's files, read whole.
struct Package {
    std::string path;                               // where it was read from, if a file
    std::map<std::string, std::string> files;       // name in the zip -> its bytes
    Json manifest;                                  // instrument.json or controller.json

    bool has(const std::string& name) const { return files.count(name) != 0; }
    const std::string& file(const std::string& name) const;   // "" if absent
    std::string id() const { return manifest["id"].str(); }
    std::string version() const { return manifest["version"].str(); }
    std::string name() const { return manifest["name"].str(id()); }
    bool isInstrument() const { return has("instrument.json"); }
    bool isController() const { return has("controller.json"); }

    // Read a package from a file or from memory; null and *error if it is not one.
    static std::unique_ptr<Package> read(const std::string& path, std::string* error = nullptr);
    static std::unique_ptr<Package> fromBytes(const void* data, size_t size, std::string* error = nullptr);
};

// Whether a sound sustains: a held note keeps sounding (bowed, blown, sung, an organ, most synths),
// or fades on its own however long it is held (struck, plucked: keys, guitars, bells, drums). By
// its tags (an instrument.json's "tags"; a built-in instrument's group is its first) and its first
// alias ("synths/pluck-synth"). Both apps' sound pickers filter by it.
bool sustains(const std::vector<std::string>& tags, const std::string& alias);

}  // namespace mpe
