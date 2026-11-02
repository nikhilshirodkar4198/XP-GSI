#include "data/StandOverrides.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <regex>

#include <nlohmann/json.hpp>

namespace StandOverrides {

namespace {

constexpr double kPi = 3.14159265358979323846;

double distM(double lat1, double lon1, double lat2, double lon2) {
    const double dn = (lat1 - lat2) * 110574.0;
    const double de = (lon1 - lon2) * 111320.0 * std::cos(lat2 * kPi / 180.0);
    return std::sqrt(dn * dn + de * de);
}

// What a rule sets on the stands it selects.
struct Effect {
    std::vector<std::string>     airlines;
    std::map<std::string, float> weights;   // from an {"ASA": 0.66, …} list
    std::vector<std::string>     types;
    std::string                  brand;
    bool                         none = false;   // "op": "none": no static here
    bool                         cargo = false;  // "op": "cargo": a cargo stand (cargo paint only)
};

Effect effectOf(const nlohmann::json& r) {
    Effect e;
    if (auto it = r.find("airlines"); it != r.end()) {
        if (it->is_array()) {
            for (const auto& s : *it)
                if (s.is_string()) e.airlines.push_back(s.get<std::string>());
        } else if (it->is_object()) {
            for (const auto& [code, w] : it->items())
                if (w.is_number() && w.get<float>() > 0.0f) {
                    e.airlines.push_back(code);
                    e.weights[code] = w.get<float>();
                }
        }
    }
    if (auto it = r.find("types"); it != r.end() && it->is_array())
        for (const auto& s : *it)
            if (s.is_string()) e.types.push_back(s.get<std::string>());
    e.brand = r.value("brand", std::string{});
    e.none  = r.value("op", std::string{}) == "none";
    e.cargo = r.value("op", std::string{}) == "cargo";
    return e;
}

void applyTo(const Effect& e, Stand& s) {
    if (e.none || e.cargo) {
        s.op = e.none ? OpType::None : OpType::Cargo;
        s.airlinesOverridden = true;   // StandEvidence leaves it alone
        return;
    }
    if (!e.airlines.empty()) {
        s.airlines           = e.airlines;
        s.airlineWeights     = e.weights;
        s.airlinesOverridden = true;
    }
    if (!e.types.empty())  s.allowedTypes = e.types;
    if (!e.brand.empty())  s.brand = e.brand;
}

} // namespace

std::size_t apply(const std::filesystem::path& file, const std::string& icao, std::vector<Stand>& stands) {
    std::ifstream f(file);
    if (!f.is_open()) return 0;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return 0;
    auto ap = j.find("airports");
    if (ap == j.end() || !ap->is_object()) return 0;
    auto rules = ap->find(icao);
    if (rules == ap->end() || !rules->is_array()) return 0;

    std::vector<bool> changed(stands.size(), false);

    // 1. Areas ("anchors": the positions of today's gates in it): each stand
    //    takes the area of its nearest anchor over all areas, if within that
    //    area's "max_m" — whatever the scenery names its stands and however
    //    old its layout (X-Plane 11's pre-2019 KSFO Terminal 1).
    struct Area { Effect effect; std::vector<std::pair<double, double>> anchors; double maxM = kDefaultAnchorM; };
    std::vector<Area> areas;
    for (const auto& r : *rules) {
        if (!r.is_object()) continue;
        auto it = r.find("anchors");
        if (it == r.end() || !it->is_array()) continue;
        Area a;
        a.effect = effectOf(r);
        a.maxM   = r.value("max_m", kDefaultAnchorM);
        for (const auto& p : *it)
            if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number())
                a.anchors.emplace_back(p[0].get<double>(), p[1].get<double>());
        if (!a.anchors.empty()) areas.push_back(std::move(a));
    }
    for (std::size_t i = 0; i < stands.size(); ++i) {
        const Area* best = nullptr;
        double bestD = std::numeric_limits<double>::max();
        for (const auto& a : areas)
            for (const auto& [la, lo] : a.anchors) {
                const double d = distM(stands[i].lat, stands[i].lon, la, lo);
                if (d <= a.maxM && d < bestD) { bestD = d; best = &a; }
            }
        if (best) {
            applyTo(best->effect, stands[i]);
            changed[i] = true;
        }
    }

    // 2. Single rules, in order (they refine an area): by name ("stands", a
    //    regex) and / or by position ("near": [lat, lon, radius m]).
    for (const auto& r : *rules) {
        if (!r.is_object() || r.contains("anchors")) continue;
        bool byName = false, byPos = false;
        std::regex re;
        if (auto it = r.find("stands"); it != r.end() && it->is_string()) {
            try {
                re = std::regex(it->get<std::string>(), std::regex::icase);
                byName = true;
            } catch (const std::regex_error&) {
            }
        }
        double nLat = 0, nLon = 0, nRadius = 0;
        if (auto it = r.find("near"); it != r.end() && it->is_array() && it->size() == 3 &&
            (*it)[0].is_number() && (*it)[1].is_number() && (*it)[2].is_number()) {
            nLat = (*it)[0].get<double>();
            nLon = (*it)[1].get<double>();
            nRadius = (*it)[2].get<double>();
            byPos = nRadius > 0;
        }
        if (!byName && !byPos) continue;
        const Effect e = effectOf(r);
        for (std::size_t i = 0; i < stands.size(); ++i) {
            Stand& s = stands[i];
            const bool hit = (byName && std::regex_search(s.id, re)) ||
                             (byPos && distM(s.lat, s.lon, nLat, nLon) <= nRadius);
            if (!hit) continue;
            applyTo(e, s);
            changed[i] = true;
        }
    }
    std::size_t n = 0;
    for (bool c : changed) n += c;
    return n;
}

} // namespace StandOverrides
