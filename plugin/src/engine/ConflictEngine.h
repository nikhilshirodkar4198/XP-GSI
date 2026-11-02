#pragma once
// ConflictEngine: pure, XPLM-free conflict detection.
// Callers supply lat/lon positions; the engine uses haversine (geo/Geo.h) so
// it can be unit-tested without X-Plane.

#include <cstddef>
#include <vector>

namespace ConflictEngine {

// Conflict radius: any live aircraft within this distance clears the stand.
static constexpr float kConflictRadiusM = 60.0f;

// How long a stand stays blocked after the conflict is resolved.
static constexpr float kCooldownSec = 120.0f;

struct LatLon { double lat, lon; };

// Returns the indices (into `stands`) of every stand within radiusM of any
// plane in `livePlanes`.  Runs in O(stands × planes).
std::vector<std::size_t> findConflicts(
    const std::vector<LatLon>& stands,
    const std::vector<LatLon>& livePlanes,
    float                      radiusM = kConflictRadiusM);

} // namespace ConflictEngine
