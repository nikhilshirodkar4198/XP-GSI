#include "engine/GateSuggest.h"

#include <algorithm>
#include <cstdio>

#include "engine/Reservations.h"

namespace GateSuggest {

const char* statusName(Status s) {
    switch (s) {
        case Status::Static:   return "static parked";
        case Status::Traffic:  return "live traffic";
        case Status::Reserved: return "reserved";
        default:               return "free";
    }
}

bool fits(char standSize, char userSize) {
    const bool known = standSize >= 'A' && standSize <= 'F' && userSize >= 'A' && userSize <= 'F';
    return !known || standSize >= userSize;
}

std::vector<std::size_t> rank(const std::string& query, const std::vector<Candidate>& stands,
                              char userSize) {
    const std::string q = Reservations::normalizeEntry(query);
    std::vector<std::size_t> prefix, contains;
    prefix.reserve(stands.size());
    for (std::size_t i = 0; i < stands.size(); ++i) {
        const std::string& n = stands[i].norm;
        if (q.empty() || n.compare(0, q.size(), q) == 0) prefix.push_back(i);
        else if (n.find(q) != std::string::npos)         contains.push_back(i);
    }
    auto order = [&](std::size_t a, std::size_t b) {
        const bool fa = fits(stands[a].size, userSize), fb = fits(stands[b].size, userSize);
        if (fa != fb) return fa;
        if (stands[a].distM != stands[b].distM) return stands[a].distM < stands[b].distM;
        return stands[a].norm < stands[b].norm;
    };
    std::sort(prefix.begin(), prefix.end(), order);
    std::sort(contains.begin(), contains.end(), order);
    prefix.insert(prefix.end(), contains.begin(), contains.end());
    return prefix;
}

std::string formatDistance(double m) {
    char buf[32];
    if (m < 1852.0) std::snprintf(buf, sizeof(buf), "%.0f m", m);
    else            std::snprintf(buf, sizeof(buf), "%.1f nm", m / 1852.0);
    return buf;
}

} // namespace GateSuggest
