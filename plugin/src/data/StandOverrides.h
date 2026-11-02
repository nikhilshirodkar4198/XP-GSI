#pragma once
// StandOverrides — hand-checked corrections for particular stands where the
// scenery's gate list and the data are not enough (data/stand_overrides.json):
//   { "schema": 1, "airports": { "KLAX": [
//       { "stands": "^52[A-Z]$",                 // regex on the stand name
//         "near": [33.9422, -118.3934, 120],     // and / or: within r m of a point
//         "airlines": ["SKW", "ENY"],            // replaces the 1301 list
//         "brand": "AAL",                        // regionals in this brand's paint
//         "types": ["CRJ2", "CRJ7", "E175", …],  // only these types
//         "note": "…" } ] } }
// Areas (hand-made terminal maps, KSFO 2026-10-28): a rule with "anchors"
// — [[lat, lon], …], the positions of today's gates in the area — covers
// every stand whose nearest anchor over all of the airport's areas is one of
// its own and within "max_m" (default kDefaultAnchorM). Stand names do not
// matter, so an older scenery layout (X-Plane 11's KSFO Terminal 1, gates
// 20–38 on piers since rebuilt) gets the airlines of the area its stands sit
// in. "airlines" may be weighted: {"ASA": 0.66, "HAL": 0.07}. "op": "none"
// leaves the stands empty (maintenance, GA and cargo ramps the scenery marks
// for airlines); "op": "cargo" makes them cargo stands (KSMF 51–60, user
// 2026-10-28). Areas apply first; single rules then refine them in order.
// Pure logic (file IO + JSON), no XPLM calls.

#include "engine/StandState.h"

#include <filesystem>
#include <string>
#include <vector>

namespace StandOverrides {

constexpr double kDefaultAnchorM = 120.0;

// Applies the airport's rules to its stands; returns how many stands changed.
// A missing or malformed file, or an invalid regex, changes nothing.
std::size_t apply(const std::filesystem::path& file, const std::string& icao, std::vector<Stand>& stands);

} // namespace StandOverrides
