#pragma once
// Density — the share of usable stands filled with statics.
//
// As realistic as possible, tailored to each airport, for both cargo and
// passenger traffic. One
// model, from how airports really fill up by day:
//
//   aircraft parked by day = airline arrivals a day × average ground time
//                            / kActiveDayHours
//
//   arrivals a day   the larger of the adsb.lol table's daily_arrivals and
//                    Wikipedia's arrivals_est (infobox movements / passengers;
//                    the first misses stays where receiver coverage is poor);
//                    the destination score × kScoreToArrivals only without
//                    either
//   ground time      narrowbody turns kNarrowGroundHours, widebodies
//                    kWideGroundHours (long-haul aircraft sit most of the day
//                    between banks), by the airport's widebody share of
//                    ground hours (harmonic mean: the table's mixes are
//                    hours-weighted)
//   active day       arrivals bunch into ~15 daylight-to-late-evening hours
//
// passenger density = parked / weighted stands, within [kAutoFloor, kAutoMax]
// (a full apron leaves room for the user and live traffic). EGLL ≈ 640
// arrivals × 3.1 h / 15 ≈ 130 parked; KPDX ≈ 260 × 1.9 h / 15 ≈ 33; a
// 10-arrival field ≈ 1. No traffic data at all: kNoDataDensity.
//
// cargo density = cargo arrivals a day × kCargoGroundHours / 24 over the
// cargo stands (freighters sit through the day between night sorts), within
// [kCargoMin, kCargoMax].
//
// A slider change is kept per airport (Settings densityByAirport); reset
// returns it to automatic. target = round(stands × density), at least one
// when there is a usable stand and the density is above 0, capped by the
// aircraft limit when one is set.
//
// Pure logic, no XPLM calls.

#include <cstddef>
#include <string>

struct Settings;

// Traffic category, for labels only ("major hub", "standard", "regional").
enum class SizeCategory { Hub, Standard, Regional };

namespace Density {

enum class Kind { Passenger, Cargo };

constexpr float kMin  = 0.00f;
constexpr float kMax  = 1.00f;
constexpr float kStep = 0.01f;
constexpr float kRegionalBelowArrivals = 10.0f;   // airline arrivals a day

constexpr float kNarrowGroundHours = 1.8f;   // turn plus the gaps between waves
constexpr float kWideGroundHours   = 5.0f;   // long-haul: arrive in one bank, leave in another
constexpr float kActiveDayHours    = 15.0f;
constexpr float kScoreToArrivals   = 0.6f;   // Wikipedia destination score → arrivals a day
constexpr float kNoDataDensity     = 0.25f;
constexpr float kAutoFloor         = 0.05f;
constexpr float kAutoMax           = 0.85f;
constexpr float kCargoGroundHours  = 10.0f;
constexpr float kCargoMin          = 0.05f;
constexpr float kCargoMax          = 0.90f;

// A major hub → Hub; else dailyArrivals ≥ kRegionalBelowArrivals → Standard;
// else (including unknown, < 0) → Regional.
SizeCategory categoryFor(bool majorHub, float dailyArrivals);
const char* name(SizeCategory c);

// Airline arrivals a day (< 0 for each input = absent; < 0 when nothing is).
float dailyArrivals(float tableArrivals, float wikiArrivals, float wikiDestinationScore = -1.0f);

// Average hours on the ground per arrival for a widebody share of ground
// hours (0–1).
float groundHours(float widebodyHoursShare);

// Airline aircraft parked at a daytime moment; < 0 when arrivals are unknown.
float parkedAtOnce(float dailyArrivals, float widebodyHoursShare);

// Automatic densities (whole percent). autoPassenger: parked over the stands
// that count (weightedStands); kNoDataDensity when parked < 0.
float autoPassenger(float parkedAtOnce, double weightedStands);
float autoCargo(float cargoArrivalsPerDay, std::size_t cargoStands);

// Clamp to [kMin, kMax] and snap to whole percent.
float clamp(float v);

// The airport's density: the user's setting for it, else `automatic`.
float get(const Settings& s, const std::string& icao, Kind k, float automatic);
bool  isCustom(const Settings& s, const std::string& icao, Kind k);
void  set(Settings& s, const std::string& icao, Kind k, float v);   // clamps
void  reset(Settings& s, const std::string& icao, Kind k);          // back to automatic

// The share of stands filled for a slider value: the value itself.
float fillShare(float slider);

// Stands that count toward the target: gates in full, remote (tie-down)
// stands at Planner::remoteWeight each — they are filled more sparsely, so
// counting them in full would only pack the gates (RJTT: 103 gates, 139
// remote stands).
double weightedStands(std::size_t gates, std::size_t remote);

// round(eligible × fillShare(density)), at least 1 when eligible > 0 and
// density > 0, capped by maxStands (the usable stands) and, when
// aircraftLimit ≥ 0, by aircraftLimit (< 0 = unlimited). Above kAutoMax the
// stands not counted in `eligible` (remote ones at their weight) are added
// linearly, so 100 % is every usable stand.
std::size_t target(double eligible, std::size_t maxStands, float density, int aircraftLimit);
// Same, every stand counting in full.
std::size_t target(std::size_t eligible, float density, int aircraftLimit);

} // namespace Density
