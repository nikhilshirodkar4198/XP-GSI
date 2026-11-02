#include "engine/Density.h"

#include <algorithm>
#include <cmath>

#include "core/Settings.h"
#include "engine/Planner.h"

namespace Density {

SizeCategory categoryFor(bool majorHub, float daily) {
    if (majorHub) return SizeCategory::Hub;
    return daily >= kRegionalBelowArrivals ? SizeCategory::Standard : SizeCategory::Regional;
}

float dailyArrivals(float table, float wiki, float score) {
    const float known = std::max(std::isfinite(table) ? table : -1.0f, std::isfinite(wiki) ? wiki : -1.0f);
    if (known >= 0.0f) return known;
    return std::isfinite(score) && score > 0.0f ? score * kScoreToArrivals : -1.0f;
}

float groundHours(float wb) {
    wb = std::isfinite(wb) ? std::min(std::max(wb, 0.0f), 1.0f) : 0.0f;
    return 1.0f / ((1.0f - wb) / kNarrowGroundHours + wb / kWideGroundHours);
}

float parkedAtOnce(float daily, float wb) {
    if (!std::isfinite(daily) || daily < 0.0f) return -1.0f;
    return daily * groundHours(wb) / kActiveDayHours;
}

const char* name(SizeCategory c) {
    switch (c) {
        case SizeCategory::Hub:      return "major hub";
        case SizeCategory::Standard: return "standard";
        default:                     return "regional";
    }
}

float autoPassenger(float parked, double stands) {
    if (!std::isfinite(parked) || parked < 0.0f || !(stands > 0.0)) return kNoDataDensity;
    const float share = static_cast<float>(static_cast<double>(parked) / stands);
    return clamp(std::min(std::max(share, kAutoFloor), kAutoMax));
}

float autoCargo(float cargoDaily, std::size_t cargoStands) {
    if (cargoStands == 0) return kCargoMin;
    const float onGround = std::isfinite(cargoDaily) ? std::max(cargoDaily, 0.0f) * kCargoGroundHours / 24.0f : 0.0f;
    return clamp(std::min(std::max(onGround / static_cast<float>(cargoStands), kCargoMin), kCargoMax));
}

float clamp(float v) {
    if (!std::isfinite(v)) v = kNoDataDensity;
    v = std::min(std::max(v, kMin), kMax);
    return std::round(v * 100.0f) / 100.0f;
}

static float& slot(Settings::AirportDensity& d, Kind k) { return k == Kind::Cargo ? d.cargo : d.passenger; }

float get(const Settings& s, const std::string& icao, Kind k, float automatic) {
    auto it = s.densityByAirport.find(icao);
    if (it == s.densityByAirport.end()) return automatic;
    const float v = k == Kind::Cargo ? it->second.cargo : it->second.passenger;
    return v >= 0.0f ? v : automatic;
}

bool isCustom(const Settings& s, const std::string& icao, Kind k) {
    return get(s, icao, k, -1.0f) >= 0.0f;
}

void set(Settings& s, const std::string& icao, Kind k, float v) {
    if (icao.empty()) return;
    slot(s.densityByAirport[icao], k) = clamp(v);
}

void reset(Settings& s, const std::string& icao, Kind k) {
    auto it = s.densityByAirport.find(icao);
    if (it == s.densityByAirport.end()) return;
    slot(it->second, k) = -1.0f;
    if (it->second.passenger < 0.0f && it->second.cargo < 0.0f) s.densityByAirport.erase(it);
}

float fillShare(float s) {
    // The bent scale (75 % → 65 %) was reverted
    // (2026-10-26: "the 75 % density from before was more realistic").
    return std::min(std::max(s, 0.0f), 1.0f);
}

double weightedStands(std::size_t gates, std::size_t remote) {
    return static_cast<double>(gates) + Planner::remoteWeight(gates, remote) * static_cast<double>(remote);
}

std::size_t target(double eligible, std::size_t maxStands, float density, int aircraftLimit) {
    const double d = static_cast<double>(fillShare(density));
    double want = std::max(eligible, 0.0) * d;
    // Above kAutoMax the stands the weighting held back (remote ones) ramp
    // in, so 100 % fills every usable stand (ZGGG at 100 %
    // drew 208 aircraft on 286 stands, 120 of them remote).
    if (d > kAutoMax && static_cast<double>(maxStands) > eligible)
        want += (static_cast<double>(maxStands) - eligible) * (d - kAutoMax) / (1.0 - kAutoMax);
    auto n = static_cast<std::size_t>(std::llround(want));
    if (n == 0 && maxStands > 0 && density > 0.0f) n = 1;   // a two-gate airport still gets one
    if (aircraftLimit >= 0) n = std::min(n, static_cast<std::size_t>(aircraftLimit));
    return std::min(n, maxStands);
}

std::size_t target(std::size_t eligible, float density, int aircraftLimit) {
    return target(static_cast<double>(eligible), eligible, density, aircraftLimit);
}

} // namespace Density
