#include <doctest/doctest.h>

#include "engine/StandLayout.h"

using namespace StandLayout;

// An A320-like model as measured from X-CSL: nose tip z -16.5, tail +21.2,
// span ±17.0, outer wing (|x| ≥ 0.45 half-span) between z +1 and +6,
// nose gear 11.4 m ahead of the origin.
static ObjGeometry::Extents a320() {
    ObjGeometry::Extents e;
    e.valid = true;
    e.minX = -17.0f; e.maxX = 17.0f;
    e.minY = 0.0f;   e.maxY = 12.0f;
    e.minZ = -16.5f; e.maxZ = 21.2f;
    e.wingFound = true; e.wingMinZ = 1.0f; e.wingMaxZ = 6.0f;
    return e;
}
static constexpr double kNoseFwd = 11.4;

TEST_SUITE("StandLayout") {

TEST_CASE("shape: nose gear on the stand point, nose 5 m ahead of it") {
    auto s = shapeFor(a320(), kNoseFwd);
    CHECK(s.fMax == doctest::Approx(16.5 - 11.4));     // nose tip
    CHECK(s.fMin == doctest::Approx(-21.2 - 11.4));    // tail
    REQUIRE(s.hasWing);
    CHECK(s.wingRMin == doctest::Approx(-17.0));
    CHECK(s.wingRMax == doctest::Approx(17.0));
}

TEST_CASE("side-by-side gates: enough spacing fits, too little overlaps") {
    auto s = shapeFor(a320(), kNoseFwd);
    // Both nose-in facing north; gates 40 m apart east-west (34 m span + 6 m).
    CHECK_FALSE(overlaps(place(s, 0, 0, 0), place(s, 40, 0, 0)));
    // 30 m apart: wingtips overlap.
    CHECK(overlaps(place(s, 0, 0, 0), place(s, 30, 0, 0)));
    // 35 m: only 1 m between wingtips, less than the 1.5 m clearance.
    CHECK(overlaps(place(s, 0, 0, 0), place(s, 35, 0, 0)));
}

TEST_CASE("heading is honoured: a nose-to-tail row along the heading") {
    auto s = shapeFor(a320(), kNoseFwd);
    // Facing east (90°): each aircraft spans 37.7 m along east-west.
    CHECK_FALSE(overlaps(place(s, 0, 0, 90), place(s, 45, 0, 90)));
    CHECK(overlaps(place(s, 0, 0, 90), place(s, 35, 0, 90)));
    // The same 45 m spacing north-south is plenty when facing east (wings are
    // 34 m wide across that axis... 45 > 34 + 1.5).
    CHECK_FALSE(overlaps(place(s, 0, 0, 90), place(s, 0, 45, 90)));
}

TEST_CASE("a widebody on a narrowbody-spaced gate overlaps its neighbour") {
    ObjGeometry::Extents b777 = a320();
    b777.minX = -32.0f; b777.maxX = 32.0f; b777.minZ = -37.0f; b777.maxZ = 37.0f;
    b777.wingMinZ = 0.0f; b777.wingMaxZ = 9.0f;
    auto big = shapeFor(b777, 30.9), small = shapeFor(a320(), kNoseFwd);
    // Gates 38 m apart: right for two A320s (4 m between wingtips), but the
    // 777's wingtip (32 m out, far aft) reaches the A320's horizontal tail.
    CHECK_FALSE(overlaps(place(small, 0, 0, 0), place(small, 38, 0, 0)));
    CHECK(overlaps(place(big, 0, 0, 0), place(small, 38, 0, 0)));
}

TEST_CASE("gate key groups MARS positions") {
    CHECK(gateKey("305L") == "305");
    CHECK(gateKey("305R") == "305");
    CHECK(gateKey("305")  == "305");
    CHECK(gateKey("Stand 305 L") == "STAND 305");
    CHECK(gateKey("544C") == "544");
    CHECK(gateKey("B12")  == "B12");     // no suffix
    CHECK(gateKey("Gate C") == "GATE C"); // a letter after a space, no digit: kept
    CHECK(gateKey("A")    == "A");
}

TEST_CASE("checker: one aircraft per multi-position gate") {
    auto s = shapeFor(a320(), kNoseFwd);
    Checker c;
    // 305L and 305R are 40 m apart, so their A320s would not touch, but the
    // gate takes only one aircraft.
    auto l = place(s, 0, 0, 0), r = place(s, 40, 0, 0);
    REQUIRE(c.check(l, gateKey("305L"), 0, 0) == Checker::Verdict::Fits);
    c.add(l, gateKey("305L"), 0, 0);
    CHECK(c.check(r, gateKey("305R"), 40, 0) == Checker::Verdict::SameGate);
    CHECK(c.check(place(s, 20, 0, 0), gateKey("305"), 20, 0) == Checker::Verdict::SameGate);
    // A same-named stand far away (another apron) is a different gate.
    CHECK(c.check(place(s, 800, 0, 0), gateKey("305R"), 800, 0) == Checker::Verdict::Fits);
}

TEST_CASE("checker: overlap with an accepted static is rejected") {
    auto s = shapeFor(a320(), kNoseFwd);
    Checker c;
    c.add(place(s, 0, 0, 0), "1", 0, 0);
    CHECK(c.check(place(s, 30, 0, 0), "2", 30, 0) == Checker::Verdict::Overlap);
    CHECK(c.check(place(s, 40, 0, 0), "2", 40, 0) == Checker::Verdict::Fits);
}

TEST_CASE("local projection: ~111 km per degree of latitude") {
    double e = 0, n = 0;
    toLocal(51.48, -0.45, 51.47, -0.45, e, n);
    CHECK(n == doctest::Approx(1105.7).epsilon(0.01));
    CHECK(e == doctest::Approx(0.0));
    toLocal(51.47, -0.44, 51.47, -0.45, e, n);
    CHECK(e == doctest::Approx(694.0).epsilon(0.01));   // 0.01° × 111.32 km × cos 51.47°
}

} // TEST_SUITE
