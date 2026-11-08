// The table of instruments: those compiled in (listed by the generated builtins.cpp), then any
// added while running (instrument packages compiled by libfaust).
#include <algorithm>
#include <cctype>
#include <deque>
#include <mutex>
#include "mpe/engine.h"

namespace mpe {
void builtinInstruments(std::vector<InstrumentInfo>& out);   // generated (CMakeLists.txt)

namespace {
std::mutex& lock() { static std::mutex m; return m; }
// A deque: adding never moves an entry, so a reference handed out stays good.
std::deque<InstrumentInfo>& table() {
    static std::deque<InstrumentInfo>* t = [] {
        auto* d = new std::deque<InstrumentInfo>();
        std::vector<InstrumentInfo> v;
        builtinInstruments(v);
        for (auto& i : v) d->push_back(std::move(i));
        return d;
    }();
    return *t;
}
}  // namespace

int instrumentCount() {
    std::lock_guard<std::mutex> g(lock());
    return int(table().size());
}

const InstrumentInfo& instrumentAt(int index) {
    std::lock_guard<std::mutex> g(lock());
    return table().at(size_t(index));
}

int addInstrument(InstrumentInfo info) {
    std::lock_guard<std::mutex> g(lock());
    auto& t = table();
    // The same id again (a package loaded twice): the newer one replaces it in finds, both stay valid.
    t.push_back(std::move(info));
    return int(t.size()) - 1;
}

int compareVersions(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        bool hx = i < a.size(), hy = j < b.size();
        while (i < a.size() && a[i] != '.') { x = x * 10 + (std::isdigit((unsigned char)a[i]) ? a[i] - '0' : 0); i++; }
        while (j < b.size() && b[j] != '.') { y = y * 10 + (std::isdigit((unsigned char)b[j]) ? b[j] - '0' : 0); j++; }
        if (hx != hy) return hx ? 1 : -1;
        if (x != y) return x < y ? -1 : 1;
        i++; j++;
    }
    return 0;
}

namespace {
bool named(const InstrumentInfo& in, const std::string& id) {
    if (in.id == id) return true;
    for (const auto& a : in.aliases) if (a == id) return true;
    return false;
}
// Under the lock. The newest version first; the same version: the latest added first.
std::vector<int> versionsOf(const std::deque<InstrumentInfo>& t, const std::string& id, const std::string& version) {
    std::vector<int> out;
    for (int i = int(t.size()) - 1; i >= 0; i--) {
        const InstrumentInfo& in = t[size_t(i)];
        if (!named(in, id) || (!version.empty() && in.version != version)) continue;
        bool same = false;
        for (int k : out) same = same || t[size_t(k)].version == in.version;
        if (!same) out.push_back(i);
    }
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return compareVersions(t[size_t(a)].version, t[size_t(b)].version) > 0; });
    return out;
}
}  // namespace

int findInstrument(const char* id) {
    if (!id) return -1;
    std::string s = id, version;
    if (size_t at = s.find('@'); at != std::string::npos) { version = s.substr(at + 1); s.resize(at); }
    std::lock_guard<std::mutex> g(lock());
    std::vector<int> v = versionsOf(table(), s, version);
    return v.empty() ? -1 : v[0];
}

std::vector<int> instrumentVersions(int index) {
    std::lock_guard<std::mutex> g(lock());
    auto& t = table();
    if (index < 0 || size_t(index) >= t.size()) return {};
    std::vector<int> v = versionsOf(t, t[size_t(index)].id, "");
    // A package loaded twice: the copy asked about stands for its version.
    for (int& k : v) if (t[size_t(k)].version == t[size_t(index)].version) k = index;
    return v;
}

bool isNewestVersion(int index) {
    std::vector<int> v = instrumentVersions(index);
    return !v.empty() && v[0] == index;
}

}  // namespace mpe
