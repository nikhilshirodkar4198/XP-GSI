#pragma once
#include <cmath>

// Geographic helpers — Phase 1 stub.
// Phase 2 will add bearings, local-frame conversion, and SpatialGrid.

// Haversine great-circle distance in kilometres between two WGS-84 points.
inline double haversineKm(double lat1Deg, double lon1Deg,
                          double lat2Deg, double lon2Deg) {
    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    constexpr double kEarthKm = 6371.0;
    double dlat = (lat2Deg - lat1Deg) * kDeg2Rad;
    double dlon = (lon2Deg - lon1Deg) * kDeg2Rad;
    double a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5)
             + std::cos(lat1Deg * kDeg2Rad) * std::cos(lat2Deg * kDeg2Rad)
             * std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
    return kEarthKm * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}
