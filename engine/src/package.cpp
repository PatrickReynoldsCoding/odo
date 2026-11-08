#include "mpe/package.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include "miniz.h"

namespace mpe {

// ---- JSON ---------------------------------------------------------------------------------

static const Json kNull;

const Json& Json::operator[](const char* key) const {
    if (type != Object) return kNull;
    for (const auto& kv : o) if (kv.first == key) return kv.second;
    return kNull;
}

const Json& Json::operator[](size_t i) const { return type == Array && i < a.size() ? a[i] : kNull; }

namespace {
struct Parser {
    const char* p;
    const char* end;
    std::string err;

    void ws() { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
    bool fail(const char* what) { if (err.empty()) err = what; return false; }

    static void utf8(std::string& out, unsigned c) {
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F));
               out += char(0x80 | (c & 0x3F)); }
    }

    bool string(std::string& out) {
        if (p >= end || *p != '"') return fail("expected a string");
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) return fail("bad escape");
                char c = *p++;
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (end - p < 4) return fail("bad \\u");
                        unsigned c1 = unsigned(std::strtoul(std::string(p, 4).c_str(), nullptr, 16)); p += 4;
                        if (c1 >= 0xD800 && c1 < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            unsigned c2 = unsigned(std::strtoul(std::string(p + 2, 4).c_str(), nullptr, 16)); p += 6;
                            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
                        }
                        utf8(out, c1);
                        break;
                    }
                    default: out += c;
                }
            } else out += *p++;
        }
        if (p >= end) return fail("unterminated string");
        p++;
        return true;
    }

    bool value(Json& v) {
        ws();
        if (p >= end) return fail("unexpected end");
        char c = *p;
        if (c == '{') {
            v.type = Json::Object; p++; ws();
            if (p < end && *p == '}') { p++; return true; }
            while (true) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p >= end || *p != ':') return fail("expected ':'");
                p++;
                Json item;
                if (!value(item)) return false;
                v.o.emplace_back(std::move(k), std::move(item));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Json::Array; p++; ws();
            if (p < end && *p == ']') { p++; return true; }
            while (true) {
                Json item;
                if (!value(item)) return false;
                v.a.push_back(std::move(item));
                ws();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Json::String; return string(v.s); }
        if (end - p >= 4 && !std::strncmp(p, "true", 4)) { v.type = Json::Bool; v.b = true; p += 4; return true; }
        if (end - p >= 5 && !std::strncmp(p, "false", 5)) { v.type = Json::Bool; p += 5; return true; }
        if (end - p >= 4 && !std::strncmp(p, "null", 4)) { p += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            std::string num;
            while (p < end && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) num += *p++;
            v.type = Json::Number;
            v.n = std::strtod(num.c_str(), nullptr);
            return true;
        }
        return fail("unexpected character");
    }
};
}  // namespace

Json Json::parse(const std::string& text, std::string* error) {
    Parser ps{text.data(), text.data() + text.size(), {}};
    Json v;
    if (!ps.value(v)) {
        if (error) *error = "JSON: " + ps.err;
        return Json();
    }
    return v;
}

// ---- packages -----------------------------------------------------------------------------

const std::string& Package::file(const std::string& name) const {
    static const std::string none;
    auto it = files.find(name);
    return it == files.end() ? none : it->second;
}

std::unique_ptr<Package> Package::fromBytes(const void* data, size_t size, std::string* error) {
    auto fail = [&](const std::string& why) -> std::unique_ptr<Package> { if (error) *error = why; return nullptr; };
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) return fail("not a zip");
    auto pkg = std::make_unique<Package>();
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { ok = false; break; }
        if (st.m_is_directory) continue;
        size_t len = 0;
        void* bytes = mz_zip_reader_extract_to_heap(&zip, i, &len, 0);
        if (!bytes) { ok = false; break; }
        pkg->files[st.m_filename] = std::string(static_cast<const char*>(bytes), len);
        mz_free(bytes);
    }
    mz_zip_reader_end(&zip);
    if (!ok) return fail("a file in the zip could not be read");
    const char* manifest = pkg->has("instrument.json") ? "instrument.json" : pkg->has("controller.json") ? "controller.json" : nullptr;
    if (!manifest) return fail("no instrument.json or controller.json");
    std::string jerr;
    pkg->manifest = Json::parse(pkg->file(manifest), &jerr);
    if (pkg->manifest.type != Json::Object) return fail(std::string(manifest) + ": " + (jerr.empty() ? "not an object" : jerr));
    if (pkg->id().empty()) return fail(std::string(manifest) + ": no id");
    return pkg;
}

std::unique_ptr<Package> Package::read(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return nullptr; }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string bytes = ss.str();
    auto pkg = fromBytes(bytes.data(), bytes.size(), error);
    if (pkg) pkg->path = path;
    return pkg;
}

bool sustains(const std::vector<std::string>& tags, const std::string& alias) {
    if (alias == "synths/pluck-synth" || alias == "synths/fm-keys") return false;
    for (const std::string& t : tags)
        for (const char* f : {"percussion", "plucked", "struck", "drums", "keys"}) if (t == f) return false;
    return true;
}

}  // namespace mpe
