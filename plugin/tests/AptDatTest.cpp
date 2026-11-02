#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include "scenery/AptDat.h"
#include "scenery/Fingerprint.h"
#include "engine/StandState.h"

// Resolve the fixtures directory relative to this source file at compile time.
// CMake injects FIXTURES_DIR as an absolute path via a compile definition.
#ifndef FIXTURES_DIR
#  error "FIXTURES_DIR must be set by CMake"
#endif

static std::filesystem::path fixturesDir() {
    return std::filesystem::path(FIXTURES_DIR);
}

// ── Helper: build a minimal AptDat::IndexEntry pointing into a fixture file ──

// Scans a fixture file to find the byte offset of a specific ICAO, mimicking
// what buildIndex does for real apt.dat files.
static AptDat::IndexEntry entryForIcao(const std::filesystem::path& file,
                                       const std::string& icao) {
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in.is_open());

    std::string line;
    while (true) {
        std::streamoff pos = in.tellg();
        if (!std::getline(in, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Check for airport header with matching ICAO
        if (line.rfind("1 ",  0) == 0 || line.rfind("16 ", 0) == 0
                                       || line.rfind("17 ", 0) == 0) {
            std::istringstream ss(line);
            std::string tok;
            for (int i = 0; i < 5; ++i) ss >> tok; // skip to field[4]
            if (tok == icao) {
                return AptDat::IndexEntry{file, pos, 0.0, 0.0, "fixture"};
            }
        }
    }
    FAIL("ICAO not found in fixture: " << icao);
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("AptDat") {

TEST_CASE("minimal.dat — parse 4 ramp starts with no 1301") {
    auto entry = entryForIcao(fixturesDir() / "minimal.dat", "KXYZ");
    auto a = AptDat::parseAirport(entry);

    CHECK(a.icao == "KXYZ");
    CHECK(a.name == "Minimal Test Airport");
    CHECK(a.stands.size() == 4);

    // Gate A1 — heavy|jets → inferred size E
    const auto& a1 = a.stands[0];
    CHECK(a1.id == "Gate A1");
    CHECK(a1.type == StandType::Gate);
    CHECK(a1.widthCode == 'E'); // heavy present → inferred E
    CHECK(a1.op == OpType::Airline); // gate + unknown → airline
    CHECK((a1.allowedClasses & EquipClass::Heavy) != 0);
    CHECK((a1.allowedClasses & EquipClass::Jets)  != 0);
    CHECK(a1.airlines.empty()); // no 1301
    CHECK(a1.headingDeg == doctest::Approx(180.0f).epsilon(0.01));

    // Gate A2 — jets only → inferred size C
    const auto& a2 = a.stands[1];
    CHECK(a2.widthCode == 'C');
    CHECK(a2.op == OpType::Airline);
    CHECK((a2.allowedClasses & EquipClass::Jets)   != 0);
    CHECK((a2.allowedClasses & EquipClass::Heavy)  == 0);

    // Ramp 1 — tie_down, props → size A, op Unknown (not a gate)
    const auto& r1 = a.stands[2];
    CHECK(r1.type == StandType::TieDown);
    CHECK(r1.widthCode == 'A');
    CHECK(r1.op == OpType::Unknown);

    // Helipad H1 — misc, helos → size A
    const auto& h1 = a.stands[3];
    CHECK(h1.type == StandType::Misc);
    CHECK(h1.widthCode == 'A');
}

TEST_CASE("with_1301.dat — 1301 metadata applied to each stand") {
    auto entry = entryForIcao(fixturesDir() / "with_1301.dat", "KABC");
    auto a = AptDat::parseAirport(entry);

    CHECK(a.icao == "KABC");
    REQUIRE(a.stands.size() == 5);

    // Gate B1 — "1301 E airline dal aal" (codes upper-cased)
    const auto& b1 = a.stands[0];
    CHECK(b1.widthCode == 'E');
    CHECK(b1.op == OpType::Airline);
    REQUIRE(b1.airlines.size() == 2);
    CHECK(b1.airlines[0] == "DAL");
    CHECK(b1.airlines[1] == "AAL");

    // Gate B2 — "1301 C airline ual swa"
    const auto& b2 = a.stands[1];
    CHECK(b2.widthCode == 'C');
    CHECK(b2.op == OpType::Airline);
    REQUIRE(b2.airlines.size() == 2);
    CHECK(b2.airlines[0] == "UAL");
    CHECK(b2.airlines[1] == "SWA");

    // Cargo Bay — "1301 B cargo fdx"
    const auto& cb = a.stands[2];
    CHECK(cb.widthCode == 'B');
    CHECK(cb.op == OpType::Cargo);
    REQUIRE(cb.airlines.size() == 1);
    CHECK(cb.airlines[0] == "FDX");

    // Exec Ramp — "1301 A general_aviation" (no airline list)
    const auto& er = a.stands[3];
    CHECK(er.widthCode == 'A');
    CHECK(er.op == OpType::GA);
    CHECK(er.airlines.empty());

    // 305L — "1301 C airline" on a heavy|jets stand: the 1301 size wins (C),
    // not the equipment-class inference (E). Regression: Aerosoft EGLL MARS
    // stands came out as E and got widebodies.
    const auto& m = a.stands[4];
    CHECK(m.widthCode == 'C');
    CHECK(m.op == OpType::Airline);
    CHECK(m.airlines.empty());
}

TEST_CASE("1301 airline codes are space-separated and upper-cased") {
    // Real-file layout (XP11 Global Airports, 141 772 rows checked):
    //   1301 <size> <op_type> [airline airline ...], codes lowercase.
    auto entry = entryForIcao(fixturesDir() / "multi_airport.dat", "KSEC");
    auto a = AptDat::parseAirport(entry);
    REQUIRE(a.stands.size() == 2);
    REQUIRE(a.stands[0].airlines.size() == 2);
    CHECK(a.stands[0].airlines[0] == "UAL");
    CHECK(a.stands[0].airlines[1] == "AAL");
}

TEST_CASE("no_ramps.dat — airport with zero ramp starts") {
    auto entry = entryForIcao(fixturesDir() / "no_ramps.dat", "KNRP");
    auto a = AptDat::parseAirport(entry);

    CHECK(a.icao == "KNRP");
    CHECK(a.stands.empty());
    // Fingerprint of an empty stand list should still be a valid string
    CHECK(a.fingerprint.rfind("fnv1a:", 0) == 0);
}

TEST_CASE("multi_airport.dat — only the requested ICAO is parsed") {
    auto entryFst = entryForIcao(fixturesDir() / "multi_airport.dat", "KFST");
    auto entryWnd = entryForIcao(fixturesDir() / "multi_airport.dat", "KSEC");

    auto fst = AptDat::parseAirport(entryFst);
    auto sec = AptDat::parseAirport(entryWnd);

    CHECK(fst.icao == "KFST");
    CHECK(fst.stands.size() == 1);
    CHECK(fst.stands[0].id == "Gate 1");

    CHECK(sec.icao == "KSEC");
    CHECK(sec.stands.size() == 2);
    CHECK(sec.stands[0].id == "Gate A");
    CHECK(sec.stands[0].widthCode == 'E'); // heavy|jets, 1301 says E
    CHECK(sec.stands[1].id == "Gate B");
    CHECK(sec.stands[1].widthCode == 'C'); // jets only, no 1301 → inferred C
}

// buildIndex cache (cache/apt_index.json): a plugin folder copied from XP11 to
// XP12 carries its cache along; it must be rebuilt for the new install, and a
// scenery pack added since the cache was written must be indexed.
TEST_CASE("buildIndex: the cache belongs to one X-Plane install and its current packs") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "xpgsi_aptcache";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    const fs::path cache = tmp / "plugin_cache";   // shared, like a copied plugin folder
    auto put = [](const fs::path& src, const fs::path& dst) {
        fs::create_directories(dst.parent_path());
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    };
    auto ini = [](const fs::path& root, const std::string& lines) {
        fs::create_directories(root / "Custom Scenery");
        std::ofstream(root / "Custom Scenery" / "scenery_packs.ini") << "I\n1000 Version\nSCENERY\n\n" << lines;
    };

    // XP11 layout: Global Airports is a Custom Scenery pack.
    const fs::path xp11 = tmp / "X-Plane 11";
    put(fixturesDir() / "minimal.dat", AptDat::globalAptPath(xp11, 11550));
    ini(xp11, "SCENERY_PACK Custom Scenery/Global Airports/\n");
    auto i11 = AptDat::buildIndex(xp11, 11550, cache);
    CHECK(i11.count("KXYZ") == 1);

    // XP12 layout (Global Scenery, *GLOBAL_AIRPORTS*), same cache folder.
    const fs::path xp12 = tmp / "X-Plane 12";
    put(fixturesDir() / "multi_airport.dat", AptDat::globalAptPath(xp12, 12000));
    ini(xp12, "SCENERY_PACK *GLOBAL_AIRPORTS*\n");
    auto i12 = AptDat::buildIndex(xp12, 12000, cache);
    CHECK(i12.count("KXYZ") == 0);
    CHECK(i12.count("KFST") == 1);
    for (const auto& [icao, e] : i12)
        CHECK(e.file.generic_string().find("X-Plane 11") == std::string::npos);

    // A pack added after the cache was written is indexed, not skipped.
    put(fixturesDir() / "no_ramps.dat", xp12 / "Custom Scenery" / "KNRP Pack" / "Earth nav data" / "apt.dat");
    ini(xp12, "SCENERY_PACK Custom Scenery/KNRP Pack/\nSCENERY_PACK *GLOBAL_AIRPORTS*\n");
    auto added = AptDat::buildIndex(xp12, 12000, cache);
    CHECK(added.count("KNRP") == 1);
    CHECK(added.count("KFST") == 1);

    // Nothing changed: served from the cache, same content.
    auto again = AptDat::buildIndex(xp12, 12000, cache);
    CHECK(again.size() == added.size());

    fs::remove_all(tmp, ec);
}

} // TEST_SUITE("AptDat")

// ─────────────────────────────────────────────────────────────────────────────
TEST_SUITE("Fingerprint") {

TEST_CASE("empty stand list produces a valid fingerprint") {
    std::vector<Stand> empty;
    auto fp = Fingerprint::compute(empty);
    CHECK(fp.rfind("fnv1a:", 0) == 0);
    CHECK(fp.size() == 22); // "fnv1a:" (6) + 16 hex digits
}

TEST_CASE("same stands produce the same fingerprint") {
    Stand s;
    s.id = "Gate A1"; s.lat = 40.000100; s.lon = -73.000100; s.headingDeg = 180.0f;
    std::vector<Stand> v1{s}, v2{s};
    CHECK(Fingerprint::compute(v1) == Fingerprint::compute(v2));
}

TEST_CASE("different positions produce different fingerprints") {
    Stand a, b;
    a.id = "G1"; a.lat = 40.000100; a.lon = -73.000100; a.headingDeg = 90.f;
    b.id = "G1"; b.lat = 40.100100; b.lon = -73.000100; b.headingDeg = 90.f;
    CHECK(Fingerprint::compute({a}) != Fingerprint::compute({b}));
}

TEST_CASE("stand order does not affect fingerprint (sorted)") {
    Stand a, b;
    a.id = "G1"; a.lat = 40.0; a.lon = -73.0; a.headingDeg = 0.f;
    b.id = "G2"; b.lat = 41.0; b.lon = -74.0; b.headingDeg = 0.f;
    CHECK(Fingerprint::compute({a, b}) == Fingerprint::compute({b, a}));
}

TEST_CASE("minor lat/lon noise within ~1 m does not change fingerprint") {
    // 5 decimal places ≈ 1.1 m; noise below that should be absorbed by rounding
    Stand a, b;
    a.id = "G1"; a.lat = 40.000001; a.lon = -73.000001; a.headingDeg = 90.f;
    b.id = "G1"; b.lat = 40.000004; b.lon = -73.000004; b.headingDeg = 90.f; // < 1 m difference
    CHECK(Fingerprint::compute({a}) == Fingerprint::compute({b}));
}

TEST_CASE("heading noise within 5° does not change fingerprint") {
    Stand a, b;
    a.id = "G1"; a.lat = 40.0; a.lon = -73.0; a.headingDeg = 91.0f;
    b.id = "G1"; b.lat = 40.0; b.lon = -73.0; b.headingDeg = 92.0f;
    // Both round to 90° → same fingerprint
    CHECK(Fingerprint::compute({a}) == Fingerprint::compute({b}));
}

} // TEST_SUITE("Fingerprint")

TEST_SUITE("demoteNonPassenger") {

static Stand mk(const char* id, double lat, double lon, StandType t, OpType op) {
    Stand s; s.id = id; s.lat = lat; s.lon = lon; s.type = t; s.op = op; return s;
}

TEST_CASE("FKKD: storage tie-downs among the military ones are not passenger stands") {
    // Rows from X-Plane 12 Global Airports FKKD (Douala).
    std::vector<Stand> st{
        mk("B3", 4.010995, 9.717333, StandType::Gate, OpType::Airline),
        mk("C1", 4.012044, 9.717287, StandType::Gate, OpType::Airline),
        mk("Mil 3", 4.017716, 9.708188, StandType::TieDown, OpType::Military),
        mk("Storage 2", 4.016781, 9.708312, StandType::TieDown, OpType::Airline),
        mk("Storage 1", 4.017214, 9.708216, StandType::TieDown, OpType::Airline),
    };
    CHECK(AptDat::demoteNonPassenger(st) == 2);
    CHECK(st[0].op == OpType::Airline);
    CHECK(st[1].op == OpType::Airline);
    CHECK(st[3].op == OpType::None);
    CHECK(st[4].op == OpType::None);
}

TEST_CASE("names: storage / maintenance / GA / cargo; ordinary remote stands kept") {
    std::vector<Stand> st{
        mk("Remote 51", 0, 0, StandType::TieDown, OpType::Airline),
        mk("Maintenance Apron 2", 0, 0.001, StandType::Gate, OpType::Airline),
        mk("GA 3", 0, 0.002, StandType::TieDown, OpType::Airline),
        mk("Cargo 7", 0, 0.003, StandType::Gate, OpType::Airline),
        mk("Gate 12", 0, 0.004, StandType::Gate, OpType::Airline),
        mk("Gate 14A", 0, 0.005, StandType::Gate, OpType::Airline),
    };
    AptDat::demoteNonPassenger(st);
    CHECK(st[0].op == OpType::Airline);   // near the gates: a real remote stand
    CHECK(st[1].op == OpType::None);
    CHECK(st[2].op == OpType::None);
    CHECK(st[3].op == OpType::Cargo);
    CHECK(st[4].op == OpType::Airline);
    CHECK(st[5].op == OpType::Airline);
}

} // TEST_SUITE("demoteNonPassenger")
