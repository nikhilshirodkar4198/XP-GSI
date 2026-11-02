#include "Fingerprint.h"
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdint>

namespace {

// FNV-1a 64-bit hash — www.isthe.com/chongo/tech/comp/fnv/
// Deterministic across platforms; not cryptographic.
uint64_t fnv1a64(const char* data, size_t len) {
    static constexpr uint64_t kOffset = 14695981039346656037ULL;
    static constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t h = kOffset;
    for (size_t i = 0; i < len; ++i) {
        h ^= static_cast<uint8_t>(data[i]);
        h *= kPrime;
    }
    return h;
}

} // namespace

namespace Fingerprint {

std::string compute(const std::vector<Stand>& stands) {
    // Token format per stand: "<id>|<lat_5dp>|<lon_5dp>|<hdg_5deg>"
    // Precision:
    //   lat/lon: 5 decimal places ≈ 1.1 m at the equator
    //   heading: nearest multiple of 5° so minor rounding noise is absorbed
    std::vector<std::string> tokens;
    tokens.reserve(stands.size());

    for (const auto& s : stands) {
        double lat5 = std::round(s.lat * 1e5) / 1e5;
        double lon5 = std::round(s.lon * 1e5) / 1e5;
        int    hdg5 = static_cast<int>(std::lround(s.headingDeg / 5.0f) * 5) % 360;
        if (hdg5 < 0) hdg5 += 360;

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s|%.5f|%.5f|%d",
                      s.id.c_str(), lat5, lon5, hdg5);
        tokens.emplace_back(buf);
    }

    std::sort(tokens.begin(), tokens.end());

    // Newline-delimited so token boundaries are unambiguous.
    std::string combined;
    combined.reserve(tokens.size() * 36);
    for (const auto& t : tokens) { combined += t; combined += '\n'; }

    uint64_t h = fnv1a64(combined.data(), combined.size());

    char result[32];
    std::snprintf(result, sizeof(result), "fnv1a:%016" PRIx64, h);
    return result;
}

} // namespace Fingerprint
