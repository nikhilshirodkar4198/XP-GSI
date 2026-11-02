#include <doctest/doctest.h>
#include "engine/ConflictEngine.h"

namespace CE = ConflictEngine;
using LL = CE::LatLon;

// KJFK threshold coords used as a stable reference point.
static constexpr double kBaseLat = 40.6413;
static constexpr double kBaseLon = -73.7781;

// Returns a LatLon offset by approx dLat, dLon in degrees.
static LL offset(double dLat, double dLon) {
    return {kBaseLat + dLat, kBaseLon + dLon};
}

// ~111 km per degree latitude; 1 m ≈ 0.000009 deg lat.
static constexpr double kDeg1m = 1.0 / 111111.0;

TEST_SUITE("ConflictEngine") {

TEST_CASE("no planes → no conflicts") {
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    CHECK(CE::findConflicts(stands, {}).empty());
}

TEST_CASE("no stands → no conflicts") {
    std::vector<LL> planes = { {kBaseLat, kBaseLon} };
    CHECK(CE::findConflicts({}, planes).empty());
}

TEST_CASE("plane exactly on stand → conflict") {
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    std::vector<LL> planes = { {kBaseLat, kBaseLon} };
    auto res = CE::findConflicts(stands, planes);
    REQUIRE(res.size() == 1);
    CHECK(res[0] == 0);
}

TEST_CASE("plane within radius → conflict") {
    // Place plane 30 m north of stand (well within 60 m default)
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    std::vector<LL> planes = { offset(30 * kDeg1m, 0.0) };
    auto res = CE::findConflicts(stands, planes, 60.0f);
    REQUIRE(res.size() == 1);
    CHECK(res[0] == 0);
}

TEST_CASE("plane outside radius → no conflict") {
    // Place plane 100 m north of stand (outside 60 m radius)
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    std::vector<LL> planes = { offset(100 * kDeg1m, 0.0) };
    CHECK(CE::findConflicts(stands, planes, 60.0f).empty());
}

TEST_CASE("multiple stands: only the close one is conflicted") {
    std::vector<LL> stands = {
        {kBaseLat,              kBaseLon},         // stand 0 — close
        {kBaseLat + 0.01,       kBaseLon},         // stand 1 — ~1.1 km away
    };
    std::vector<LL> planes = { offset(10 * kDeg1m, 0.0) }; // 10 m from stand 0
    auto res = CE::findConflicts(stands, planes, 60.0f);
    REQUIRE(res.size() == 1);
    CHECK(res[0] == 0);
}

TEST_CASE("one plane conflicts multiple stands") {
    // Two stands 10 m apart, plane between them
    std::vector<LL> stands = {
        offset( 10 * kDeg1m, 0.0),   // stand 0
        offset(-10 * kDeg1m, 0.0),   // stand 1
    };
    std::vector<LL> planes = { {kBaseLat, kBaseLon} };
    auto res = CE::findConflicts(stands, planes, 60.0f);
    CHECK(res.size() == 2);
}

TEST_CASE("custom radius respected") {
    // Plane is 40 m away — inside 50 m radius, outside 30 m radius
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    std::vector<LL> planes = { offset(40 * kDeg1m, 0.0) };
    CHECK(!CE::findConflicts(stands, planes, 30.0f).empty() == false); // outside 30
    CHECK(!CE::findConflicts(stands, planes, 50.0f).empty());          // inside 50
}

TEST_CASE("each stand reported at most once even with multiple planes") {
    std::vector<LL> stands = { {kBaseLat, kBaseLon} };
    std::vector<LL> planes = {
        offset(5  * kDeg1m, 0.0),
        offset(10 * kDeg1m, 0.0),
    };
    auto res = CE::findConflicts(stands, planes, 60.0f);
    CHECK(res.size() == 1);  // same stand not reported twice
}

} // TEST_SUITE
