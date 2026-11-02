#include "engine/StandEvidence.h"

#include <cmath>
#include <string>
#include <unordered_map>

#include "engine/StandLayout.h"

namespace StandEvidence {

std::vector<Evidence> collect(const std::vector<Stand>& stands, const std::vector<ParkCell>& cells) {
    std::vector<Evidence> out(stands.size());
    if (stands.empty()) return out;
    const double refLat = stands.front().lat, refLon = stands.front().lon;
    std::vector<std::pair<double, double>> pos(stands.size());
    for (std::size_t i = 0; i < stands.size(); ++i)
        StandLayout::toLocal(stands[i].lat, stands[i].lon, refLat, refLon, pos[i].first, pos[i].second);
    for (const auto& c : cells) {
        double e = 0, n = 0;
        StandLayout::toLocal(c.lat, c.lon, refLat, refLon, e, n);
        std::size_t best = stands.size();
        double bestD = kMatchM;
        for (std::size_t i = 0; i < stands.size(); ++i) {
            const double d = std::hypot(pos[i].first - e, pos[i].second - n);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best == stands.size()) continue;
        for (int k = 0; k < 5; ++k) out[best].pax[k] += c.pax[k];
        out[best].nonPax += c.nonPax;
    }
    return out;
}

char sizeFor(const Evidence& e, char current) {
    const char cur = (current >= 'A' && current <= 'F') ? current : 'C';
    const int total = e.paxTotal();
    if (total == 0) return cur;
    int bigger = 0;
    for (int k = 0; k < 5; ++k)
        if ('B' + k > cur) bigger += e.pax[k];
    if (bigger < kMinBigger || bigger < kMinBiggerShare * total) return cur;
    for (int k = 4; k >= 0; --k) {
        const char sz = static_cast<char>('B' + k);
        if (sz > cur && e.pax[k] >= 2 && e.pax[k] >= kMinSizeShare * total) return sz;
    }
    return cur;
}

bool passengerStand(const Evidence& e) {
    const int pax = e.paxTotal();
    return pax >= kMinPaxStays && pax >= kMinPaxShare * (pax + e.nonPax);
}

bool nonPassengerStand(const Evidence& e) {
    return e.nonPax >= kMinNonPaxStays && e.nonPax >= kNonPaxDominance * e.paxTotal();
}

static bool overridden(const Stand& s) {
    return s.airlinesOverridden || !s.allowedTypes.empty() || !s.brand.empty();
}

// Evidence per stand, a multi-position gate's positions summed (each gets its gate's total).
static std::vector<Evidence> grouped(const std::vector<Stand>& stands, const std::vector<Evidence>& ev,
                                     std::vector<std::string>& keys) {
    std::vector<std::string> names;
    for (const auto& s : stands) names.push_back(s.id);
    keys = StandLayout::gateKeys(names);
    std::unordered_map<std::string, Evidence> group;
    for (std::size_t i = 0; i < stands.size(); ++i) {
        auto& g = group[keys[i]];
        for (int k = 0; k < 5; ++k) g.pax[k] += ev[i].pax[k];
        g.nonPax += ev[i].nonPax;
    }
    std::vector<Evidence> out(stands.size());
    for (std::size_t i = 0; i < stands.size(); ++i) out[i] = group[keys[i]];
    return out;
}

std::vector<int> paxStaysPerStand(const std::vector<Stand>& stands, const std::vector<ParkCell>& cells) {
    std::vector<int> out(stands.size(), 0);
    if (cells.empty() || stands.empty()) return out;
    std::vector<std::string> keys;
    const auto g = grouped(stands, collect(stands, cells), keys);
    for (std::size_t i = 0; i < stands.size(); ++i) out[i] = g[i].paxTotal();
    return out;
}

Result apply(std::vector<Stand>& stands, const std::vector<ParkCell>& cells) {
    Result r;
    if (cells.empty() || stands.empty()) return r;
    // A multi-position gate's evidence counts together.
    std::vector<std::string> keys;
    const auto ev = grouped(stands, collect(stands, cells), keys);
    std::unordered_map<std::string, std::size_t> largest;   // gate → its largest position
    for (std::size_t i = 0; i < stands.size(); ++i) {
        auto it = largest.find(keys[i]);
        if (it == largest.end() || stands[i].widthCode > stands[it->second].widthCode) largest[keys[i]] = i;
    }

    for (std::size_t i = 0; i < stands.size(); ++i) {
        Stand& s = stands[i];
        if (overridden(s)) continue;
        const bool usable = s.type == StandType::Gate || s.type == StandType::TieDown;
        if (usable && s.op != OpType::Airline && s.op != OpType::Military && passengerStand(ev[i])) {
            s.op = OpType::Airline;
            ++r.toAirline;
        } else if (usable && s.op == OpType::Airline && nonPassengerStand(ev[i])) {
            s.op = OpType::None;
            ++r.notAirline;
        }
    }
    for (const auto& [key, i] : largest) {
        Stand& s = stands[i];
        if (s.op != OpType::Airline || overridden(s)) continue;
        const char want = sizeFor(ev[i], s.widthCode);
        if (want != s.widthCode && want > s.widthCode) {
            s.widthCode = want;
            ++r.upsized;
        }
    }
    return r;
}

} // namespace StandEvidence
