#include "data/AirportData.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace AirportData {

static std::vector<OsmStand> readOsmStands(const nlohmann::json& arr);

std::optional<AirportTable> load(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f.is_open()) return std::nullopt;

    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1)
        return std::nullopt;

    AirportTable t;
    t.icao    = j.value("icao", std::string{});
    t.samples = j.value("samples", 0);
    if (auto it = j.find("generated"); it != j.end() && it->is_string())
        t.generated = it->get<std::string>();
    t.airportType = j.value("airport_type", std::string{});
    if (auto it = j.find("occupancy"); it != j.end() && it->is_number())
        t.occupancy = it->get<float>();
    if (auto it = j.find("daily_arrivals"); it != j.end() && it->is_number())
        t.dailyArrivals = it->get<float>();
    if (auto it = j.find("on_ground"); it != j.end() && it->is_number())
        t.onGround = it->get<float>();

    if (auto it = j.find("airlines"); it != j.end() && it->is_object())
        for (const auto& [code, w] : it->items())
            if (w.is_number() && w.get<float>() > 0.0f) t.airlines[code] = w.get<float>();

    if (auto it = j.find("types"); it != j.end() && it->is_object())
        for (const auto& [code, byType] : it->items()) {
            if (!byType.is_object()) continue;
            for (const auto& [type, w] : byType.items())
                if (w.is_number() && w.get<float>() > 0.0f) t.types[code][type] = w.get<float>();
        }

    if (auto it = j.find("clusters"); it != j.end() && it->is_array())
        for (const auto& c : *it) {
            if (!c.is_object() || !c.contains("lat") || !c.contains("lon")) continue;
            ParkCluster pc;
            pc.lat = c.value("lat", 0.0);
            pc.lon = c.value("lon", 0.0);
            pc.n   = c.value("n", 0.0f);
            if (auto a = c.find("airlines"); a != c.end() && a->is_object())
                for (const auto& [code, n] : a->items())
                    if (n.is_number() && n.get<float>() > 0.0f) pc.airlines[code] = n.get<float>();
            if (auto ty = c.find("types"); ty != c.end() && ty->is_object())
                for (const auto& [code, byType] : ty->items()) {
                    if (!byType.is_object()) continue;
                    for (const auto& [type, n] : byType.items())
                        if (n.is_number() && n.get<float>() > 0.0f) pc.types[code][type] = n.get<float>();
                }
            if (!pc.airlines.empty()) t.clusters.push_back(std::move(pc));
        }

    if (auto it = j.find("terminals"); it != j.end() && it->is_object()) {
        if (auto z = it->find("zones"); z != it->end() && z->is_array())
            for (const auto& e : *z)
                if (e.is_object())
                    t.zones.push_back({e.value("name", std::string{}), e.value("group", std::string{})});
        if (auto a = it->find("anchors"); a != it->end() && a->is_array())
            for (const auto& e : *a)
                if (e.is_array() && e.size() == 3 && e[0].is_number() && e[1].is_number() &&
                    e[2].is_number_integer()) {
                    const int zone = e[2].get<int>();
                    if (zone >= 0 && zone < static_cast<int>(t.zones.size()))
                        t.anchors.push_back({e[0].get<double>(), e[1].get<double>(), zone});
                }
        if (t.anchors.empty()) t.zones.clear();
    }

    // "nonpax_cells": [[lat, lon, nonpax, pax], ...]
    if (auto it = j.find("nonpax_cells"); it != j.end() && it->is_array())
        for (const auto& e : *it)
            if (e.is_array() && e.size() >= 2 && e[0].is_number() && e[1].is_number())
                t.nonPaxCells.emplace_back(e[0].get<double>(), e[1].get<double>());
    // "park_cells": [[lat, lon, B, C, D, E, F, nonpax], ...]
    if (auto it = j.find("park_cells"); it != j.end() && it->is_array())
        for (const auto& e : *it) {
            if (!e.is_array() || e.size() < 8) continue;
            bool ok = true;
            for (const auto& v : e) ok = ok && v.is_number();
            if (!ok) continue;
            ParkCell c;
            c.lat = e[0].get<double>();
            c.lon = e[1].get<double>();
            for (int k = 0; k < 5; ++k) c.pax[k] = e[2 + k].get<int>();
            c.nonPax = e[7].get<int>();
            t.parkCells.push_back(c);
        }
    // "cargo": {"airlines": {...}, "types": {...}, "daily": 6.0}
    if (auto it = j.find("cargo"); it != j.end() && it->is_object()) {
        if (auto a = it->find("airlines"); a != it->end() && a->is_object())
            for (const auto& [code, w] : a->items())
                if (w.is_number() && w.get<float>() > 0.0f) t.cargoAirlines[code] = w.get<float>();
        if (auto ty = it->find("types"); ty != it->end() && ty->is_object())
            for (const auto& [code, byType] : ty->items())
                if (byType.is_object())
                    for (const auto& [type, w] : byType.items())
                        if (w.is_number() && w.get<float>() > 0.0f) t.cargoTypes[code][type] = w.get<float>();
        t.cargoDaily = it->value("daily", 0.0f);
    }
    if (auto it = j.find("osm_stands"); it != j.end()) t.osmStands = readOsmStands(*it);
    return t;
}

// [[lat, lon, heading, "C", "Bay 12", 0], ...]
static std::vector<OsmStand> readOsmStands(const nlohmann::json& arr) {
    std::vector<OsmStand> out;
    if (!arr.is_array()) return out;
    for (const auto& e : arr) {
        if (!e.is_array() || e.size() < 6 || !e[0].is_number() || !e[1].is_number() || !e[2].is_number())
            continue;
        OsmStand s;
        s.lat        = e[0].get<double>();
        s.lon        = e[1].get<double>();
        s.headingDeg = e[2].get<float>();
        if (e[3].is_string() && !e[3].get<std::string>().empty()) s.size = e[3].get<std::string>()[0];
        if (e[4].is_string()) s.ref = e[4].get<std::string>();
        s.remote = e[5].is_number() && e[5].get<int>() != 0;
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<OsmStand> loadOsmStands(const std::filesystem::path& file, const std::string& icao) {
    std::ifstream f(file);
    if (!f.is_open() || icao.empty()) return {};
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return {};
    auto a = j.find("airports");
    if (a == j.end() || !a->is_object()) return {};
    auto it = a->find(icao);
    return it == a->end() ? std::vector<OsmStand>{} : readOsmStands(*it);
}

bool nearNonPax(const AirportTable& t, double lat, double lon) {
    const double cosLat = std::cos(lat * 3.14159265358979323846 / 180.0);
    for (const auto& [cl, co] : t.nonPaxCells) {
        const double n = (cl - lat) * 110574.0, e = (co - lon) * 111320.0 * cosLat;
        if (n * n + e * e <= kNonPaxRadiusM * kNonPaxRadiusM) return true;
    }
    return false;
}

// The airport's entry in a {"schema":1,"airports":{...}} file; a null json if none.
static nlohmann::json terminalEntry(const std::filesystem::path& file, const std::string& icao) {
    std::ifstream f(file);
    if (!f.is_open()) return {};
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return {};
    const auto ap = j.find("airports");
    if (ap == j.end() || !ap->is_object()) return {};
    const auto e = ap->find(icao);
    if (e == ap->end() || !e->is_object()) return {};
    return std::move(*e);
}

std::size_t applyTerminalAssignments(AirportTable& t, const std::filesystem::path& file, const std::string& icao,
                                     const std::filesystem::path& airlinesFile) {
    if (!t.clusters.empty()) return 0;
    const nlohmann::json e = terminalEntry(file, icao);
    if (!e.is_object()) return 0;
    // The zones' airlines, inline or (shipped data) in the separate
    // Wikipedia-derived file, kept apart for its license.
    const nlohmann::json alSrc =
        (!e.contains("airlines") && !airlinesFile.empty()) ? terminalEntry(airlinesFile, icao) : e;
    if (!alSrc.is_object()) return 0;
    const auto zs = e.find("zones"), as = e.find("anchors"), al = alSrc.find("airlines");
    if (zs == e.end() || as == e.end() || al == alSrc.end() || !zs->is_array() || !as->is_array() ||
        !al->is_array() || zs->size() != al->size())
        return 0;
    std::vector<TerminalZone>  zones;
    std::vector<ZoneAnchor>    anchors;
    std::vector<std::map<std::string, float>> mix;   // zone → airline → share
    for (const auto& z : *zs)
        zones.push_back({z.value("name", std::string{}), z.value("group", std::string{})});
    for (const auto& codes : *al) {
        std::map<std::string, float> m;
        float total = 0.0f;
        if (codes.is_array())
            for (const auto& c : codes)
                if (c.is_string()) {
                    auto it = t.airlines.find(c.get<std::string>());
                    if (it != t.airlines.end() && it->second > 0.0f) { m[it->first] = it->second; total += it->second; }
                }
        for (auto& [a, w] : m) w /= total;
        mix.push_back(std::move(m));
    }
    for (const auto& a : *as)
        if (a.is_array() && a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number_integer()) {
            const int zone = a[2].get<int>();
            if (zone >= 0 && zone < static_cast<int>(zones.size()))
                anchors.push_back({a[0].get<double>(), a[1].get<double>(), zone});
        }
    if (zones.size() < 2 || anchors.empty()) return 0;
    t.zones   = std::move(zones);
    t.anchors = anchors;
    for (const auto& a : anchors) {
        if (mix[a.zone].empty()) continue;
        ParkCluster c;
        c.lat = a.lat;
        c.lon = a.lon;
        c.n   = kAssignedStays;
        for (const auto& [airline, share] : mix[a.zone]) c.airlines[airline] = share * kAssignedStays;
        t.clusters.push_back(std::move(c));
    }
    return t.zones.size();
}

std::optional<AirportTable> loadFor(const std::filesystem::path& dataDir, const std::string& icao) {
    return load(dataDir / "airports" / (icao + ".json"));
}

} // namespace AirportData
