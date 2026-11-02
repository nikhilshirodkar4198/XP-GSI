#include "engine/ConflictEngine.h"
#include "geo/Geo.h"

namespace ConflictEngine {

std::vector<std::size_t> findConflicts(
    const std::vector<LatLon>& stands,
    const std::vector<LatLon>& livePlanes,
    float                      radiusM)
{
    std::vector<std::size_t> out;
    if (livePlanes.empty()) return out;

    const double radiusKm = static_cast<double>(radiusM) / 1000.0;

    for (std::size_t i = 0; i < stands.size(); ++i) {
        for (const auto& plane : livePlanes) {
            if (haversineKm(stands[i].lat, stands[i].lon,
                            plane.lat,     plane.lon) <= radiusKm) {
                out.push_back(i);
                break; // one conflict per stand is enough
            }
        }
    }
    return out;
}

} // namespace ConflictEngine
