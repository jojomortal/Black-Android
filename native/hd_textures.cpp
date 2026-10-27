#include "hd_textures.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/log.h"

namespace hd {
namespace {

struct Entry {
    uint32_t w, h, fmt;
    std::string file;
};

std::unordered_multimap<uint64_t, Entry> g_map;
size_t g_map_lines = 0;
bool g_loaded = false, g_on = false;

struct Stats {
    // distinct textures seen by format (index: 0 P8, 1 DXT1, 2 DXT3, 3 DXT5), and how many of them the map knows
    uint64_t seen[4] = {}, hit[4] = {};
    uint64_t skipped = 0;  // not eligible: linear, cube, 3D, depth, under 16 px, other formats
    std::set<uint64_t> keys_hit;  // distinct map keys seen
    std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t>> done;
    std::chrono::steady_clock::time_point logged = std::chrono::steady_clock::now();
    double hash_ms = 0;
} g_st;

int Slot(uint32_t fmt) { return fmt == 0x0B ? 0 : fmt == 0x0C ? 1 : fmt == 0x0E ? 2 : fmt == 0x0F ? 3 : -1; }

// the map's key: FNV-1a 64 over the top level's bytes as 64-bit words
uint64_t Fnv(const uint8_t* m, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull, word;
    for (size_t i = 0; i < n; i += 8) {
        std::memcpy(&word, m + i, 8);
        h = (h ^ word) * 0x100000001b3ull;
    }
    return h;
}

void Report() {
    uint64_t seen = 0, hit = 0;
    for (int i = 0; i < 4; i++) { seen += g_st.seen[i]; hit += g_st.hit[i]; }
    LOGI("hd: %llu textures keyed, %llu in the map (%.0f%%), %llu distinct map keys, %llu not eligible; "
         "P8 %llu/%llu DXT1 %llu/%llu DXT3 %llu/%llu DXT5 %llu/%llu; hashing %.1f ms (map: %zu lines)",
         (unsigned long long)seen, (unsigned long long)hit, seen ? 100.0 * hit / seen : 0.0,
         (unsigned long long)g_st.keys_hit.size(), (unsigned long long)g_st.skipped,
         (unsigned long long)g_st.hit[0], (unsigned long long)g_st.seen[0], (unsigned long long)g_st.hit[1],
         (unsigned long long)g_st.seen[1], (unsigned long long)g_st.hit[2], (unsigned long long)g_st.seen[2],
         (unsigned long long)g_st.hit[3], (unsigned long long)g_st.seen[3], g_st.hash_ms, g_map_lines);
}

}  // namespace

bool Enabled() {
    if (g_loaded) return g_on;
    g_loaded = true;
    const char* path = std::getenv("BLACK_HD_MAP");
    if (!path || !*path) return false;
    std::ifstream in(path);
    if (!in) {
        LOGE("hd: cannot open the map %s", path);
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key, file;
        Entry e{};
        std::string fmt;
        if (!(ls >> key >> e.w >> e.h >> fmt >> file)) continue;
        e.fmt = uint32_t(std::strtoul(fmt.c_str(), nullptr, 16));
        e.file = file;
        g_map.emplace(std::strtoull(key.c_str(), nullptr, 16), e);
        g_map_lines++;
    }
    g_on = g_map_lines > 0;
    LOGI("hd: map %s: %zu textures", path, g_map_lines);
    return g_on;
}

void Note(const gpu::TextureDesc& d, const gpu::FormatInfo& fi, const uint8_t* guest) {
    if (!Enabled()) return;
    int slot = Slot(d.format);
    if (slot < 0 || d.linear || d.cube || d.depth > 1 || fi.depth || d.width < 16 || d.height < 16 || !guest) {
        g_st.skipped++;
        return;
    }
    size_t top = slot == 0 ? size_t(d.width) * d.height
                           : size_t(d.width / 4) * (d.height / 4) * (slot == 1 ? 8 : 16);
    if (!top || (top & 7)) {
        g_st.skipped++;
        return;
    }
    auto t0 = std::chrono::steady_clock::now();
    uint64_t key = Fnv(guest, top);
    g_st.hash_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (g_st.done.insert({key, d.width, d.height, d.format}).second) {
        g_st.seen[slot]++;
        bool found = false;
        auto r = g_map.equal_range(key);
        for (auto it = r.first; it != r.second; ++it)
            if (it->second.w == d.width && it->second.h == d.height && it->second.fmt == d.format) found = true;
        if (found) {
            g_st.hit[slot]++;
            g_st.keys_hit.insert(key);
        } else if (std::getenv("BLACK_HD_MISSES")) {
            LOGI("hd: miss %016llx %ux%u fmt %02x levels %u", (unsigned long long)key, d.width, d.height, d.format,
                 d.levels);
        }
    }
    auto now = std::chrono::steady_clock::now();
    if (now - g_st.logged > std::chrono::seconds(5)) {
        g_st.logged = now;
        Report();
    }
}

}  // namespace hd
