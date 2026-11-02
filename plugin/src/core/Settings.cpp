// Settings: load / save plugin configuration from/to settings.json.

#include "core/Settings.h"
#include "core/Log.h"
#include "engine/Density.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace {

static StartMode startModeFromStr(const std::string& s) {
    if (s == "manual")           return StartMode::Manual;
    if (s == "vatsim_connected") return StartMode::VatsimConnected;
    return StartMode::OnSimStart;
}

} // namespace

const char* startModeToStr(StartMode m) {
    switch (m) {
        case StartMode::Manual:          return "manual";
        case StartMode::VatsimConnected: return "vatsim_connected";
        default:                          return "on_sim_start";
    }
}

void Settings::sanitize() {
    for (auto it = densityByAirport.begin(); it != densityByAirport.end();) {
        auto& d = it->second;
        d.passenger = (std::isfinite(d.passenger) && d.passenger >= 0.0f) ? Density::clamp(d.passenger) : -1.0f;
        d.cargo     = (std::isfinite(d.cargo)     && d.cargo     >= 0.0f) ? Density::clamp(d.cargo)     : -1.0f;
        it = (d.passenger < 0.0f && d.cargo < 0.0f) ? densityByAirport.erase(it) : std::next(it);
    }
    maxAircraft   = maxAircraft < 0 ? -1 : std::min(maxAircraft, kAircraftLimitMax);
    auto nonNeg = [](float& v, float fallback) { if (!std::isfinite(v) || v < 0.0f) v = fallback; };
    nonNeg(conflictRadiusM, 60.0f);
    nonNeg(cooldownSec,     0.0f);
    nonNeg(maxRenderDistKm, 15.0f);
    windowGeometrySaved = windowGeometrySaved &&
                          windowRight > windowLeft && windowTop > windowBottom;
    gateGeometrySaved   = gateGeometrySaved && gateRight > gateLeft && gateTop > gateBottom;
}

Settings Settings::load(const std::filesystem::path& path) {
    Settings s;

    std::ifstream f(path);
    if (!f.is_open()) {
        Log::info("Settings: '%s' not found — using defaults", path.u8string().c_str());
        return s;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        Log::warn("Settings: failed to parse '%s' (%s) — using defaults",
                  path.u8string().c_str(), e.what());
        return s;
    }
    if (!j.is_object()) {
        Log::warn("Settings: '%s' is not a JSON object — using defaults", path.u8string().c_str());
        return s;
    }

    auto getFloat = [&](const char* key, float& out) {
        if (j.contains(key) && j.at(key).is_number()) out = j.at(key).get<float>();
    };
    auto getBool = [&](const char* key, bool& out) {
        if (j.contains(key) && j.at(key).is_boolean()) out = j.at(key).get<bool>();
    };
    auto getInt = [&](const char* key, int& out) {
        if (j.contains(key) && j.at(key).is_number_integer()) out = j.at(key).get<int>();
    };

    if (j.contains("start_mode") && j.at("start_mode").is_string())
        s.startMode = startModeFromStr(j.at("start_mode").get<std::string>());
    // Per-airport densities (2026-10-27). The earlier per-category values
    // ("traffic_density_v3" hub / standard / regional) are not carried over:
    // every airport starts from its automatic density once.
    if (j.contains("density_by_airport") && j.at("density_by_airport").is_object())
        for (const auto& [icao, d] : j.at("density_by_airport").items()) {
            if (!d.is_object()) continue;
            Settings::AirportDensity a;
            if (d.contains("passenger") && d.at("passenger").is_number()) a.passenger = d.at("passenger").get<float>();
            if (d.contains("cargo") && d.at("cargo").is_number())         a.cargo     = d.at("cargo").get<float>();
            s.densityByAirport[icao] = a;
        }
    getInt  ("aircraft_limit",     s.maxAircraft);
    getFloat("conflict_radius_m",  s.conflictRadiusM);
    // New key (2026-10-29): the old "cooldown_sec" (always 120 when saved) is
    // ignored, so existing installs start with the restore off too.
    getFloat("restore_cooldown_sec", s.cooldownSec);
    getFloat("max_render_dist_km", s.maxRenderDistKm);
    getInt  ("fixed_seed",         s.fixedSeed);
    getBool ("enabled",            s.enabled);
    getBool ("cargo_enabled",      s.cargoEnabled);
    getBool ("debug_log",          s.debugLog);

    if (j.contains("window") && j.at("window").is_object()) {
        const auto& w = j.at("window");
        auto getWInt = [&](const char* key, int& out) {
            if (w.contains(key) && w.at(key).is_number_integer()) out = w.at(key).get<int>();
        };
        getWInt("left",   s.windowLeft);
        getWInt("top",    s.windowTop);
        getWInt("right",  s.windowRight);
        getWInt("bottom", s.windowBottom);
        s.windowGeometrySaved = true;   // sanitize() drops it if degenerate
    }
    if (j.contains("gate_window") && j.at("gate_window").is_object()) {
        const auto& w = j.at("gate_window");
        auto getWInt = [&](const char* key, int& out) {
            if (w.contains(key) && w.at(key).is_number_integer()) out = w.at(key).get<int>();
        };
        getWInt("left",   s.gateLeft);
        getWInt("top",    s.gateTop);
        getWInt("right",  s.gateRight);
        getWInt("bottom", s.gateBottom);
        s.gateGeometrySaved = true;
    }

    s.sanitize();
    Log::info("Settings: loaded — mode=%s  density set by hand at %zu airport(s)  limit=%d (-1 = none)  "
              "radius=%.0f m  cooldown=%.0f s  cull=%.0f km  enabled=%s",
              startModeToStr(s.startMode),
              s.densityByAirport.size(),
              s.maxAircraft,
              static_cast<double>(s.conflictRadiusM),
              static_cast<double>(s.cooldownSec),
              static_cast<double>(s.maxRenderDistKm),
              s.enabled ? "yes" : "no");
    return s;
}

bool Settings::save(const std::filesystem::path& path, const Settings& in) {
    Settings s = in;
    s.sanitize();

    nlohmann::json j;
    j["start_mode"]         = startModeToStr(s.startMode);
    nlohmann::json dens = nlohmann::json::object();
    for (const auto& [icao, d] : s.densityByAirport) {
        nlohmann::json e = nlohmann::json::object();
        if (d.passenger >= 0.0f) e["passenger"] = d.passenger;
        if (d.cargo     >= 0.0f) e["cargo"]     = d.cargo;
        dens[icao] = e;
    }
    j["density_by_airport"] = dens;
    j["aircraft_limit"]     = s.maxAircraft;   // -1 = unlimited
    j["conflict_radius_m"]  = s.conflictRadiusM;
    j["restore_cooldown_sec"] = s.cooldownSec;
    j["max_render_dist_km"] = s.maxRenderDistKm;
    j["fixed_seed"]         = s.fixedSeed;
    j["enabled"]            = s.enabled;
    j["cargo_enabled"]      = s.cargoEnabled;
    j["debug_log"]          = s.debugLog;

    if (s.windowGeometrySaved) {
        j["window"] = {
            {"left",   s.windowLeft},
            {"top",    s.windowTop},
            {"right",  s.windowRight},
            {"bottom", s.windowBottom}
        };
    }
    if (s.gateGeometrySaved) {
        j["gate_window"] = {
            {"left",   s.gateLeft},
            {"top",    s.gateTop},
            {"right",  s.gateRight},
            {"bottom", s.gateBottom}
        };
    }

    // Write to a temp file and rename, so a crash mid-write never leaves a
    // truncated settings.json behind.
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) {
            Log::warn("Settings: could not write '%s'", tmp.u8string().c_str());
            return false;
        }
        out << j.dump(2) << '\n';
        if (!out) {
            Log::warn("Settings: write failed for '%s'", tmp.u8string().c_str());
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        Log::warn("Settings: could not replace '%s' (%s)", path.u8string().c_str(), ec.message().c_str());
        return false;
    }
    Log::debug("Settings: saved to '%s'", path.u8string().c_str());
    return true;
}
