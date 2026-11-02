// Terminal zones: stands and parking evidence by terminal, so an airline is
// only placed at a terminal it uses.
#include <doctest/doctest.h>

#include "csl/ModelSelector.h"
#include "engine/TerminalZones.h"

#include <string>

namespace {

// At 51.47° N, 0.001° of longitude is about 69 m.
constexpr double kLat = 51.4700;

void line(AirportTable& t, int zone, double lon0, double lon1) {
    for (double lon = lon0; lon <= lon1 + 1e-9; lon += 0.0005) t.anchors.push_back({kLat, lon, zone});
}

ParkCluster cluster(double lon, const std::string& airline, float n, double lat = kLat + 0.0003) {
    ParkCluster c;
    c.lat = lat; c.lon = lon; c.n = n;
    c.airlines[airline] = n;
    c.types[airline]["B738"] = n;
    return c;
}

// T1 (−0.500 … −0.486), T2 (−0.470 … −0.460), T3 (−0.440 … −0.438, nothing
// parked there). BAW parks at T1, DAL at T2, EZY only somewhere unzoned.
AirportTable heathrowLike() {
    AirportTable t;
    t.icao = "TEST";
    t.samples = 1000;
    t.airlines = {{"BAW", 0.4f}, {"DAL", 0.4f}, {"EZY", 0.2f}};
    for (const char* a : {"BAW", "DAL", "EZY"}) t.types[a]["B738"] = 1.0f;
    t.zones = {{"Terminal 1", "T1"}, {"Terminal 2", "T2"}, {"Terminal 3", "T3"}};
    line(t, 0, -0.500, -0.486);
    line(t, 1, -0.470, -0.460);
    line(t, 2, -0.440, -0.438);
    t.clusters = {cluster(-0.4990, "BAW", 20), cluster(-0.4650, "DAL", 20), cluster(-0.4655, "AAL", 1),
                  cluster(-0.4700, "EZY", 4, kLat + 0.03)};   // 3 km north: no zone
    return t;
}

Stand standAt(double lat, double lon) {
    Stand s;
    s.widthCode = 'C';
    s.lat = lat; s.lon = lon;
    return s;
}

ModelSelector::Extras withZones(const TerminalZones::Zones& z) {
    ModelSelector::Extras e;
    e.zones = &z;
    return e;
}

CslDb db() {
    CslDb d;
    for (const char* a : {"BAW", "DAL", "EZY", "AAL"}) {
        CslVariant v;
        v.objPaths = {std::string("/") + a + ".obj"};
        v.airlineCode = a;
        d["B738"].push_back(v);
    }
    return d;
}

} // namespace

TEST_SUITE("TerminalZones") {

TEST_CASE("OpenStreetMap zones: positions, clusters, mixes, based-elsewhere") {
    const AirportTable t = heathrowLike();
    const auto z = TerminalZones::Zones::build(t, {});
    REQUIRE_FALSE(z.empty());
    CHECK(z.fromOsm());
    CHECK(z.zoneCount() == 3);
    CHECK(z.zoneOf(kLat + 0.0005, -0.4870) == 0);
    CHECK(z.zoneOf(kLat, -0.4600) == 1);
    CHECK(z.zoneOf(kLat + 0.01, -0.4800) == -1);   // 1.1 km from any terminal
    CHECK(z.clusterZone(0) == 0);
    CHECK(z.clusterZone(1) == 1);
    CHECK(z.clusterZone(3) == -1);
    REQUIRE(z.mixFor(0));
    CHECK(z.mixFor(0)->airlines.count("DAL") == 0);
    CHECK(z.mixFor(2) == nullptr);                  // nothing parked at T3
    CHECK(z.basedElsewhere("DAL", 0));
    CHECK_FALSE(z.basedElsewhere("DAL", 1));
    CHECK_FALSE(z.basedElsewhere("AAL", 0));        // too few stays to tell
    CHECK_FALSE(z.basedElsewhere("EZY", 0));        // never seen in a terminal
}

TEST_CASE("a stand far from any parked aircraft takes its terminal's airlines only") {
    const AirportTable t = heathrowLike();
    const auto z = TerminalZones::Zones::build(t, {});
    const auto ex = withZones(z);
    const CslDb d = db();
    // East end of T1: ~830 m from where BAW parks (no nearby evidence).
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        auto c = ModelSelector::selectWithTable(standAt(kLat + 0.0005, -0.4870), d, t, seed * 7919u,
                                                nullptr, &ex);
        CHECK(c.airline == "BAW");
        CHECK(c.fromZone);
        CHECK_FALSE(c.fromArea);
    }
}

TEST_CASE("a terminal without evidence gets the airport mix minus airlines based elsewhere") {
    const AirportTable t = heathrowLike();
    const auto z = TerminalZones::Zones::build(t, {});
    const auto ex = withZones(z);
    const CslDb d = db();
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        auto c = ModelSelector::selectWithTable(standAt(kLat + 0.0005, -0.4390), d, t, seed * 104729u,
                                                nullptr, &ex);
        CHECK(c.airline == "EZY");
        CHECK_FALSE(c.fromZone);
    }
    // Without zones the same stand can get anyone.
    bool sawOther = false;
    for (std::uint32_t seed = 1; seed < 200 && !sawOther; ++seed)
        sawOther = ModelSelector::selectWithTable(standAt(kLat + 0.0005, -0.4390), d, t, seed * 104729u)
                       .airline != "EZY";
    CHECK(sawOther);
}

TEST_CASE("nearby evidence from the next terminal does not count") {
    AirportTable t;
    t.samples  = 1000;
    t.airlines = {{"BAW", 0.5f}, {"DAL", 0.5f}};
    t.types["BAW"]["B738"] = 1.0f;
    t.types["DAL"]["B738"] = 1.0f;
    t.zones = {{"Terminal 1", "T1"}, {"Terminal 2", "T2"}};
    line(t, 0, -0.500, -0.486);
    line(t, 1, -0.482, -0.470);
    t.clusters = {cluster(-0.4990, "BAW", 20), cluster(-0.4835, "DAL", 20)};
    const auto z = TerminalZones::Zones::build(t, {});
    // 173 m from the DAL cluster (in T2), 900 m from BAW's.
    const Stand s = standAt(kLat + 0.0003, -0.4860);
    CHECK(ModelSelector::areaMix(s, t).airlines.count("DAL") == 1);
    CHECK(ModelSelector::areaMix(s, t, &z).airlines.count("DAL") == 0);
    const CslDb d = db();
    const auto ex = withZones(z);
    for (std::uint32_t seed = 1; seed < 100; ++seed)
        CHECK(ModelSelector::selectWithTable(s, d, t, seed * 31u, nullptr, &ex).airline == "BAW");
}

TEST_CASE("without OpenStreetMap terminals, neighbouring stands form the zones") {
    AirportTable t;
    t.samples  = 400;
    t.airlines = {{"BAW", 0.5f}, {"DAL", 0.5f}};
    t.clusters = {cluster(-0.5000, "BAW", 10, kLat), cluster(-0.4800, "DAL", 10, kLat)};
    std::vector<Stand> stands;
    for (int k = 0; k < 5; ++k) stands.push_back(standAt(kLat, -0.5000 + k * 0.0008));  // ~55 m apart
    for (int k = 0; k < 5; ++k) stands.push_back(standAt(kLat, -0.4800 + k * 0.0008));  // 1.4 km east
    const auto z = TerminalZones::Zones::build(t, stands);
    REQUIRE_FALSE(z.empty());
    CHECK_FALSE(z.fromOsm());
    CHECK(z.zoneCount() == 2);
    const int west = z.zoneOf(kLat, -0.4968);
    CHECK(west >= 0);
    CHECK(z.zoneOf(kLat, -0.4800) != west);
    CHECK(z.basedElsewhere("DAL", west));
    // A single group (or no parking data) gives no zones.
    stands.resize(5);
    CHECK(TerminalZones::Zones::build(t, stands).empty());
    t.clusters.clear();
    CHECK(TerminalZones::Zones::build(t, {}).empty());
}

} // TEST_SUITE
