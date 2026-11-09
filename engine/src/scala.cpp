// Scala tunings (scala.h).
#include "mpe/scala.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mpe::scala {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

// A pitch line: its first word. With a full stop, cents; else a ratio a/b, or a whole number a (a/1).
bool pitch(const std::string& line, double* cents) {
    std::string w = trim(line);
    size_t end = 0;
    while (end < w.size() && !std::isspace(static_cast<unsigned char>(w[end]))) end++;
    w = w.substr(0, end);
    if (w.empty()) return false;
    char* stop = nullptr;
    if (w.find('.') != std::string::npos) {
        double c = std::strtod(w.c_str(), &stop);
        if (*stop || !std::isfinite(c)) return false;
        *cents = c;
        return true;
    }
    size_t slash = w.find('/');
    std::string a = w.substr(0, slash), b = slash == std::string::npos ? "1" : w.substr(slash + 1);
    for (const std::string* p : {&a, &b}) {
        if (p->empty()) return false;
        for (char c : *p) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    double n = std::strtod(a.c_str(), nullptr), d = std::strtod(b.c_str(), nullptr);
    if (!(n > 0) || !(d > 0) || !std::isfinite(n) || !std::isfinite(d)) return false;
    *cents = 1200.0 * std::log2(n / d);
    return true;
}

std::string fixed(double v, int places) {
    char b[48];
    std::snprintf(b, sizeof b, "%.*f", places, v);
    std::string t = b;
    if (t.find('.') != std::string::npos) {
        while (t.back() == '0') t.pop_back();
        if (t.back() == '.') t.pop_back();
    }
    return t == "-0" ? "0" : t;
}

long findSetting(const Json& manifest, const std::string& id) {
    const Json& ss = manifest["settings"];
    for (size_t i = 0; i < ss.size(); i++) if (ss[i]["id"].str() == id) return long(i);
    return -1;
}

}  // namespace

bool parse(const std::string& text, Scale* out, std::string* error) {
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    std::vector<std::string> lines;
    size_t at = 0;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        std::string l = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] != '!') lines.push_back(l);    // "!" starts a comment line
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    if (lines.size() < 2) return fail("This isn't a Scala file: it needs a description line and how many notes.");
    Scale s;
    s.description = trim(lines[0]);
    if (s.description.size() >= 3 && s.description.compare(0, 3, "\xEF\xBB\xBF") == 0) s.description.erase(0, 3);
    std::string count = trim(lines[1]);
    size_t sp = 0;
    while (sp < count.size() && std::isdigit(static_cast<unsigned char>(count[sp]))) sp++;
    if (sp == 0) return fail("This isn't a Scala file: its second line should say how many notes it has.");
    long n = std::strtol(count.substr(0, sp).c_str(), nullptr, 10);
    if (n < 1) return fail("This Scala file has no notes.");
    if (n > 1024) return fail("This Scala file has " + std::to_string(n) + " notes; odo takes up to 1024.");
    size_t i = 2;
    while (long(s.cents.size()) < n) {
        while (i < lines.size() && trim(lines[i]).empty()) i++;
        if (i >= lines.size())
            return fail("This Scala file says it has " + std::to_string(n) + " notes but lists " + std::to_string(s.cents.size()) + ".");
        double c;
        if (!pitch(lines[i], &c))
            return fail("Note " + std::to_string(s.cents.size() + 1) + " isn't cents (with a full stop) or a ratio: \"" + trim(lines[i]) + "\".");
        s.cents.push_back(c);
        i++;
    }
    if (!(s.cents.back() > 0)) return fail("Its last note, where it repeats, has to be above the first.");
    *out = std::move(s);
    return true;
}

int capacity(const Json& s) { return std::max(0, std::min(1024, int(s["max"].num(0)))); }

std::vector<std::pair<size_t, double>> settings(const Json& manifest, size_t index, const Scale& scale, std::string* error) {
    std::vector<std::pair<size_t, double>> out;
    const Json& s = manifest["settings"][index];
    std::string id = s["id"].str();
    int cap = capacity(s);
    if (int(scale.cents.size()) > cap) {
        if (error) *error = "This tuning has " + std::to_string(scale.cents.size()) + " notes; this controller takes up to " + std::to_string(cap) + ".";
        return out;
    }
    for (int k = 1; k <= cap; k++) {
        long at = findSetting(manifest, id + std::to_string(k));
        if (at < 0) {
            if (error) *error = "This controller has no place for its note " + std::to_string(k) + ".";
            out.clear();
            return out;
        }
        out.push_back({size_t(at), k <= int(scale.cents.size()) ? scale.cents[size_t(k - 1)] : 0.0});
    }
    out.push_back({index, double(scale.cents.size())});
    return out;
}

std::string describe(const Json& manifest, size_t index, const std::vector<double>& values) {
    const Json& s = manifest["settings"][index];
    double v = index < values.size() ? values[index] : s["default"].num(0);
    int n = int(std::lround(v));
    if (n < 1) return "None";
    long at = findSetting(manifest, s["id"].str() + std::to_string(n));
    double period = at >= 0 && size_t(at) < values.size() ? values[size_t(at)] : 0;
    std::string out = std::to_string(n) + (n == 1 ? " note" : " notes");
    if (period > 0) {
        if (std::fabs(period - 1200) < 1e-6) out += ", to 2/1 (1200 \xC2\xA2)";
        else out += ", to " + fixed(period, 3) + " \xC2\xA2";
    }
    return out;
}

}  // namespace mpe::scala
