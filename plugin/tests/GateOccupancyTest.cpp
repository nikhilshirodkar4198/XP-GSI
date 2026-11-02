#include <doctest/doctest.h>

#include "engine/GateOccupancy.h"

#include <cmath>

using GateOccupancy::Traffic;

// A stand facing north (heading 0) at KSFO-ish coordinates.
static Stand standNorth(char code = 'C') {
    Stand s;
    s.id = "A1";
    s.lat = 37.6150;
    s.lon = -122.3900;
    s.headingDeg = 0.0f;
    s.widthCode = code;
    return s;
}

// An aircraft `behind` metres south and `side` metres east of the stand point.
static Traffic at(const Stand& s, double behind, double side, float hdg, float speed, bool gnd = true) {
    const double mPerDegLat = 111320.0;
    const double mPerDegLon = 111320.0 * std::cos(s.lat * 3.14159265358979323846 / 180.0);
    Traffic t;
    t.lat = s.lat - behind / mPerDegLat;
    t.lon = s.lon + side / mPerDegLon;
    t.headingDeg = hdg;
    t.speedMs = speed;
    t.onGround = gnd;
    return t;
}

TEST_SUITE("GateOccupancy") {

TEST_CASE("parked on the stand: occupies") {
    const Stand s = standNorth();
    CHECK(GateOccupancy::occupies(s, at(s, 15, 0, 0, 0)));
    CHECK(GateOccupancy::occupies(s, at(s, 15, 8, 358, 0)));     // a little off, heading wraps
}

TEST_CASE("taxiing past, airborne, misaligned or far away: does not occupy") {
    const Stand s = standNorth();
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, 15, 0, 0, 0, false)));   // airborne
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, 15, 0, 0, 8)));          // too fast (taxi)
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, 30, 0, 90, 2)));         // across the stand (taxilane)
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, 15, 25, 0, 0)));         // next stand over
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, 80, 0, 0, 0)));          // far behind
    CHECK_FALSE(GateOccupancy::occupies(s, at(s, -20, 0, 0, 0)));         // in front of the nose
}

TEST_CASE("box depth follows the stand size") {
    CHECK_FALSE(GateOccupancy::occupies(standNorth('A'), at(standNorth('A'), 35, 0, 0, 0)));
    CHECK(GateOccupancy::occupies(standNorth('E'), at(standNorth('E'), 35, 0, 0, 0)));
}

TEST_CASE("Tracker: parked claims at once and never releases") {
    const std::vector<Stand> stands{standNorth()};
    GateOccupancy::Tracker tr;
    tr.reset(1);
    auto got = tr.update(stands, {at(stands[0], 15, 0, 0, 0)}, 10.0);
    REQUIRE(got.size() == 1);
    CHECK(tr.claimed(0));
    // Pushback, then gone: the stand stays claimed and is not reported again.
    CHECK(tr.update(stands, {at(stands[0], 30, 0, 2, 1.0f)}, 11.0).empty());
    CHECK(tr.update(stands, {}, 60.0).empty());
    CHECK(tr.claimed(0));
}

TEST_CASE("Tracker: moving slowly in the box claims after the dwell time") {
    const std::vector<Stand> stands{standNorth()};
    GateOccupancy::Tracker tr;
    tr.reset(1);
    const Traffic slow = at(stands[0], 25, 0, 0, 2.0f);   // pulling in
    CHECK(tr.update(stands, {slow}, 0.0).empty());
    CHECK(tr.update(stands, {slow}, 0.5).empty());
    CHECK(tr.update(stands, {slow}, 1.0).size() == 1);    // kDwellSec 1 s (was 2)

    // Passing through briefly does not claim.
    GateOccupancy::Tracker tr2;
    tr2.reset(1);
    CHECK(tr2.update(stands, {slow}, 0.0).empty());
    CHECK(tr2.update(stands, {}, 1.0).empty());
    CHECK(tr2.update(stands, {slow}, 2.0).empty());
    CHECK_FALSE(tr2.claimed(0));
}

// A narrowbody static as the planner places it: nose gear on the stand point.
static StandLayout::Shape narrowbody() {
    StandLayout::Shape s;
    s.fMin = -36.0; s.fMax = 4.0; s.halfWidth = 2.0;
    s.hasWing = true; s.wingFMin = -22.0; s.wingFMax = -14.0; s.wingRMin = -18.0; s.wingRMax = 18.0;
    return s;
}

TEST_CASE("touches: a widebody on the next stand reaches the static's wing; a narrowbody does not") {
    const Stand s = standNorth();
    // Next stand 40 m east, aircraft parked facing north, reference point ~20 m behind it.
    Traffic wide = at(s, 20, 40, 0, 0);
    wide.sizeCode = 'E';                      // 62 m span: wingtip 9 m from our centreline
    CHECK(GateOccupancy::touches(s, narrowbody(), wide));
    Traffic narrow = wide;
    narrow.sizeCode = 'C';                    // 36 m span: 22 m, clear of our 18 m wing
    CHECK_FALSE(GateOccupancy::touches(s, narrowbody(), narrow));
    Traffic airborne = wide;
    airborne.onGround = false;
    CHECK_FALSE(GateOccupancy::touches(s, narrowbody(), airborne));
}

TEST_CASE("Tracker: a touching aircraft claims the stand when shapes are set") {
    const std::vector<Stand> stands{standNorth()};
    Traffic wide = at(stands[0], 20, 40, 0, 0);
    wide.sizeCode = 'E';
    GateOccupancy::Tracker tr;
    tr.reset(1);
    CHECK(tr.update(stands, {wide}, 0.0).empty());       // no shapes: only occupancy counts
    tr.reset(1);
    tr.setShapes({narrowbody()});
    CHECK(tr.update(stands, {wide}, 0.0).size() == 1);
}

TEST_CASE("gateGroup: alternative positions share their gate") {
    using GateOccupancy::gateGroup;
    CHECK(gateGroup("F22")  == "F22");
    CHECK(gateGroup("F22A") == "F22");
    CHECK(gateGroup("f22b") == "F22");
    CHECK(gateGroup("G57G") == "G57");
    CHECK(gateGroup("201B") == "201");
    CHECK(gateGroup("305 L") == "305");
    CHECK(gateGroup("GA5")  == "GA5");
    CHECK(gateGroup("B21")  != gateGroup("B22"));
    CHECK(gateGroup("Qantas Maintenance 1") == "QANTAS MAINTENANCE 1");
    CHECK(gateGroup("Remote") == "REMOTE");
}

TEST_CASE("Tracker: an aircraft on 201B takes 201A and 201 too, not 202 or a far 201") {
    Stand b = standNorth();  b.id = "201B";
    Stand a = b;             a.id = "201A"; a.lon += 0.0003;   // ~26 m east
    Stand c = b;             c.id = "201";  c.lon += 0.00015;
    Stand d = b;             d.id = "202";  d.lon += 0.0006;
    Stand far = b;           far.id = "201"; far.lat += 0.01;   // ~1 km: another 201
    const std::vector<Stand> stands{b, a, c, d, far};
    GateOccupancy::Tracker tr;
    tr.reset(stands.size());
    auto got = tr.update(stands, {at(b, 15, 0, 0, 0)}, 0.0);
    CHECK(got.size() == 3);
    CHECK(tr.claimed(0));
    CHECK(tr.claimed(1));
    CHECK(tr.claimed(2));
    CHECK_FALSE(tr.claimed(3));
    CHECK_FALSE(tr.claimed(4));
}

} // TEST_SUITE
