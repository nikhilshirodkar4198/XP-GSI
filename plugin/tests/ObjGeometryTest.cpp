#include <doctest/doctest.h>

#include "csl/ObjGeometry.h"

#include <filesystem>

// FIXTURES_DIR is injected at compile time from CMake.
static const std::filesystem::path kGeom =
    std::filesystem::path(FIXTURES_DIR) / "geometry";

TEST_SUITE("ObjGeometry") {

TEST_CASE("extents span every VT vertex; -Z is the nose") {
    auto e = ObjGeometry::measure({kGeom / "gear_anim.obj"});
    REQUIRE(e.valid);
    CHECK(e.minZ == doctest::Approx(-10.0f));
    CHECK(e.maxZ == doctest::Approx(10.0f));
    CHECK(e.minY == doctest::Approx(-0.5f));   // antenna below the gear: not gear
}

TEST_CASE("nose gear = centreline gear_ratio geometry, contact patch centre") {
    auto e = ObjGeometry::measure({kGeom / "gear_anim.obj"});
    REQUIRE(e.noseGearFound);
    // Contact vertices z -7.2 and -6.8 at y 0; strut top (y 1.5) is above
    // the contact band; main gear (|x| > 1) and the non-gear antenna ignored.
    CHECK(e.noseGearZ == doctest::Approx(-7.0f));
    CHECK(e.noseGearY == doctest::Approx(0.0f));
    CHECK(ObjGeometry::noseGearPlausible(e));
    CHECK(ObjGeometry::noseGearForwardM(e) == doctest::Approx(7.0f));
}

TEST_CASE("no gear animation: fall back to a fraction of the length") {
    auto e = ObjGeometry::measure({kGeom / "no_gear.obj"});
    REQUIRE(e.valid);
    CHECK_FALSE(e.noseGearFound);
    // minZ + 0.13 * 20 = -7.4  →  7.4 m forward of the origin
    CHECK(ObjGeometry::noseGearForwardM(e) == doctest::Approx(7.4f));
}

TEST_CASE("implausible gear position (mid-fuselage) is rejected") {
    auto e = ObjGeometry::measure({kGeom / "gear_mid.obj"});
    REQUIRE(e.noseGearFound);
    CHECK_FALSE(ObjGeometry::noseGearPlausible(e));
    CHECK(ObjGeometry::noseGearForwardM(e) == doctest::Approx(7.4f));
}

TEST_CASE("several parts are measured together") {
    // Fuselage-only part + gear part: gear from one, extents from both.
    auto e = ObjGeometry::measure({kGeom / "no_gear.obj", kGeom / "gear_anim.obj"});
    REQUIRE(e.noseGearFound);
    CHECK(ObjGeometry::noseGearForwardM(e) == doctest::Approx(7.0f));
}

TEST_CASE("unreadable files give an invalid result and zero offset") {
    auto e = ObjGeometry::measure({kGeom / "no_such.obj"});
    CHECK_FALSE(e.valid);
    CHECK(ObjGeometry::noseGearForwardM(e) == doctest::Approx(0.0f));
}

} // TEST_SUITE
