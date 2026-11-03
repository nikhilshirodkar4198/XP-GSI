#include <doctest/doctest.h>

#include "csl/CslDb.h"
#include "csl/CslScanner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

// FIXTURES_DIR is injected at compile time from CMake.
static const std::filesystem::path kCslRoot =
    std::filesystem::path(FIXTURES_DIR) / "csl";
static const std::filesystem::path kXcslA20N =
    kCslRoot / "Custom Data" / "CSL" / "X-CSL" / "A20N";
static const std::filesystem::path kXcslShared =
    kCslRoot / "Custom Data" / "CSL" / "X-CSL" / "SHARED";

// ── parseFile ─────────────────────────────────────────────────────────────────

TEST_CASE("parseFile: ICAO directive populates catalogue") {
    CslDb db;
    auto xsb = kCslRoot / "Custom Scenery" / "pkg_a" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    CHECK(db.count("A320") == 1);
    CHECK(db.count("A321") == 1);
    CHECK(db.count("A306") == 1);
}

TEST_CASE("parseFile: __PackageName: prefix resolves to the package folder") {
    CslDb db;
    auto xsb = kCslRoot / "Custom Scenery" / "pkg_a" / "xsb_aircraft.txt";
    auto pkgDir = xsb.parent_path();
    CslScanner::parseFile(xsb, db);

    REQUIRE(db.count("A320") == 1);
    const auto& v = db.at("A320");
    REQUIRE(v.size() == 1);
    REQUIRE(v[0].objPaths.size() == 1);
    // EXPORT_NAME __Bluebell_Airbus → pkgDir; "__Bluebell_Airbus:A320/A320.obj"
    CHECK(v[0].objPaths[0] == pkgDir / "A320" / "A320.obj");
}

TEST_CASE("parseFile: AIRLINE directive maps to ICAO type") {
    CslDb db;
    auto xsb = kCslRoot / "Resources" / "plugins" / "pkg_b" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    CHECK(db.count("B738") == 1);
}

TEST_CASE("parseFile: AIRLINE directive captures airline code") {
    CslDb db;
    auto xsb = kCslRoot / "Resources" / "plugins" / "pkg_b" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    REQUIRE(db.count("B738") == 1);
    CHECK(db.at("B738")[0].airlineCode == "DAL");
}

TEST_CASE("parseFile: ICAO directive leaves airline code empty") {
    CslDb db;
    auto xsb = kCslRoot / "Custom Scenery" / "pkg_a" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    REQUIRE(db.count("A320") == 1);
    CHECK(db.at("A320")[0].airlineCode.empty());
}

TEST_CASE("parseFile: MATCHES with wildcard '-' leaves airline code empty") {
    CslDb db;
    auto xsb = kCslRoot / "Resources" / "plugins" / "pkg_b" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    REQUIRE(db.count("B763") == 1);
    CHECK(db.at("B763")[0].airlineCode.empty()); // MATCHES B763 - -  →  "-" stripped
}

TEST_CASE("parseFile: MATCHES directive maps to ICAO type") {
    CslDb db;
    auto xsb = kCslRoot / "Resources" / "plugins" / "pkg_b" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    CHECK(db.count("B763") == 1);
}

TEST_CASE("parseFile: path whose first element is not a known package is dropped") {
    // "B744/B744.obj": XPMP2 reads "B744" as the package name; no such
    // EXPORT_NAME exists, so the line (and the model) is dropped even though
    // the file exists next to xsb_aircraft.txt.
    CslDb db;
    auto xsb = kCslRoot / "Resources" / "plugins" / "pkg_b" / "xsb_aircraft.txt";
    REQUIRE(std::filesystem::exists(xsb.parent_path() / "B744" / "B744.obj"));
    CslScanner::parseFile(xsb, db);

    CHECK(db.count("B744") == 0);
}

TEST_CASE("parseFile: sizeCode defaults to question-mark") {
    CslDb db;
    auto xsb = kCslRoot / "Custom Scenery" / "pkg_a" / "xsb_aircraft.txt";
    CslScanner::parseFile(xsb, db);

    REQUIRE(db.count("A320") == 1);
    CHECK(db.at("A320")[0].sizeCode == '?');
}

TEST_CASE("parseFile: does not crash on non-existent file") {
    CslDb db;
    CslScanner::parseFile("no_such_file.txt", db);
    CHECK(db.empty());
}

// ── X-CSL style packages ──────────────────────────────────────────────────────

TEST_CASE("parseFile: X-CSL keeps every OBJ8 line, colon and backslash forms") {
    CslDb db;
    CslScanner::parseFile(kXcslA20N / "xsb_aircraft.txt", db);

    REQUIRE(db.count("A20N") == 1);
    const auto& models = db.at("A20N");
    REQUIRE(!models.empty());

    const auto& aaf = models[0];          // file order: A20N_A20NAAF first
    CHECK(aaf.airlineCode == "AAF");
    REQUIRE(aaf.objPaths.size() == 2);
    CHECK(aaf.objPaths[0] == kXcslA20N / "A20NCFMfan.obj");   // "A20N:A20NCFMfan.obj"
    CHECK(aaf.objPaths[1] == kXcslA20N / "A20NCFM_AAF.obj");  // "A20N\A20NCFM_AAF.obj"
    CHECK(aaf.vertOfsM == doctest::Approx(0.0f));
}

TEST_CASE("parseFile: unknown package and missing file are skipped") {
    CslDb db;
    CslScanner::parseFile(kXcslA20N / "xsb_aircraft.txt", db);

    // A20N_BROKEN has no resolvable object → not catalogued. Remaining:
    // AAF, SHARED (A20N part only — SharedParts is unknown to a single-file
    // parse), NOVO.
    REQUIRE(db.count("A20N") == 1);
    const auto& models = db.at("A20N");
    REQUIRE(models.size() == 5);
    CHECK(models[1].objPaths.size() == 1);
    for (const auto& m : models)
        for (const auto& p : m.objPaths)
            CHECK(p.filename() != "missing.obj");
}

TEST_CASE("parseFile: VERT_OFFSET read, absent offset derived from the OBJ") {
    CslDb db;
    CslScanner::parseFile(kXcslA20N / "xsb_aircraft.txt", db);

    REQUIRE(db.count("A20N") == 1);
    const auto& models = db.at("A20N");
    REQUIRE(models.size() == 5);
    CHECK(models[1].vertOfsM == doctest::Approx(1.5f));   // VERT_OFFSET 1.5
    CHECK(models[2].vertOfsM == doctest::Approx(2.5f));   // lowpoint.obj min Y = -2.5
}

TEST_CASE("LIVERY code parsed; numbered codes are special schemes") {
    CslDb db;
    CslScanner::parseFile(kXcslA20N / "xsb_aircraft.txt", db);
    const auto& models = db.at("A20N");
    REQUIRE(models.size() == 5);
    CHECK(models[0].livery.empty());          // AIRLINE A20N AAF: the standard livery
    CHECK_FALSE(models[0].special);
    CHECK(models[3].livery == "2");           // LIVERY A20N AAF 2
    CHECK(models[3].airlineCode == "AAF");
    CHECK(models[3].special);
    CHECK(models[4].livery == "WL");          // LIVERY A20N AAF WL: winglet airframe
    CHECK_FALSE(models[4].special);
}

TEST_CASE("livery overrides flip mislabelled models (X-CSL A20N BAW: Better World vs standard)") {
    CslDb db;
    CslVariant bw, std2, other;
    bw.objPaths   = {"/xp/Custom Data/CSL/X-CSL/A20N/A20NCFMfan.obj",
                     "/xp/Custom Data/CSL/X-CSL/A20N/A20NCFM_BAW.obj"};
    bw.airlineCode = "BAW";
    std2.objPaths = {"D:\\XP\\Custom Data\\CSL\\X-CSL\\A20N\\A20NCFM_BAW2.obj"};
    std2.airlineCode = "BAW"; std2.livery = "2"; std2.special = true;
    other.objPaths = {"/xp/Custom Data/CSL/X-CSL/A20N/A20NCFM_XBAW.obj"};   // suffix must match a whole name
    db["A20N"] = {bw, std2, other};

    const auto file = std::filesystem::temp_directory_path() / "xpgsi_livery_overrides_test.json";
    { std::ofstream(file) << R"({"schema": 2, "entries": {
                                    "X-CSL/A20N/A20NCFM_BAW.obj":  {"status": "current", "special": true},
                                    "x-csl/a20n/a20ncfm_baw2.obj": {"status": "current", "special": false}}})"; }
    const auto st = CslScanner::applyLiveryOverrides(db, file);
    CHECK(st.changed == 2);
    CHECK(st.matched == 2);
    CHECK(db["A20N"][0].special);
    CHECK_FALSE(db["A20N"][1].special);
    CHECK_FALSE(db["A20N"][2].special);
    CHECK(CslScanner::applyLiveryOverrides(db, "no_such_file.json").matched == 0);
    std::filesystem::remove(file);
}

TEST_CASE("shipped csl_livery_overrides.json is valid schema 2") {
    std::ifstream f(std::filesystem::path(FIXTURES_DIR) / ".." / ".." / "data" / "csl_livery_overrides.json");
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    REQUIRE_FALSE(j.is_discarded());
    CHECK(j.value("schema", 0) == 2);
    REQUIRE(j.contains("entries"));
    for (const auto& item : j.at("entries").items()) {
        // No structured binding: CHECK_MESSAGE captures `path` in a lambda (C++20).
        const std::string path = item.key();
        const std::string s = item.value().value("status", std::string{});
        CHECK_MESSAGE((s == "current" || s == "legacy" || s == "outdated" || s == "defunct"), path);
    }
}

TEST_CASE("isSpecialLiveryCode") {
    for (const char* s : {"2", "3", "WL2", "S3", "S5", "10"}) CHECK(isSpecialLiveryCode(s));
    for (const char* s : {"", "WL", "S", "CRG", "BCF", "OLD"}) CHECK_FALSE(isSpecialLiveryCode(s));
}

TEST_CASE("vertOfsFromObj: XPMP2 rule — VT/VLINE lines of 20+ chars, -minY") {
    // lowpoint.obj: VT Y -2.5 / 3.0 / 0.5, VLINE Y -1.0, and a short
    // "VT 0 -9 0" line that XPMP2's length check skips.
    CHECK(CslScanner::vertOfsFromObj(kXcslA20N / "lowpoint.obj") == doctest::Approx(2.5f));
    CHECK(CslScanner::vertOfsFromObj(kXcslA20N / "no_such.obj") == doctest::Approx(0.0f));
}

TEST_CASE("resolvePackagePath: XPMP2 package-path rules") {
    CslScanner::PackageMap pkgs{{"A20N", kXcslA20N}};
    CHECK(CslScanner::resolvePackagePath("A20N:A20NCFMfan.obj", pkgs) == kXcslA20N / "A20NCFMfan.obj");
    CHECK(CslScanner::resolvePackagePath("A20N\\A20NCFMfan.obj", pkgs) == kXcslA20N / "A20NCFMfan.obj");
    CHECK(CslScanner::resolvePackagePath("A20N/A20NCFMfan.obj", pkgs) == kXcslA20N / "A20NCFMfan.obj");
    CHECK(CslScanner::resolvePackagePath("A20NCFMfan.obj", pkgs).empty());       // no package prefix
    CHECK(CslScanner::resolvePackagePath(":A20NCFMfan.obj", pkgs).empty());      // separator first
    CHECK(CslScanner::resolvePackagePath("A20N:", pkgs).empty());                // separator last
    CHECK(CslScanner::resolvePackagePath("a20n:A20NCFMfan.obj", pkgs).empty());  // names are exact
    CHECK(CslScanner::resolvePackagePath("A20N:missing.obj", pkgs).empty());     // file must exist
}

// ── scan ──────────────────────────────────────────────────────────────────────

TEST_CASE("searchRoots: all of Custom Data, so xPilot's Custom Data/CSL and a library one level up") {
    auto roots = CslScanner::searchRoots("X");
    CHECK(std::find(roots.begin(), roots.end(),
                    std::filesystem::path("X") / "Custom Data") != roots.end());
}

// The first XP12 test (2026-10-28): X-CSL copied to Custom Data/X-CSL, not
// Custom Data/CSL/X-CSL as xPilot puts it in XP11 — XP-GSI found 0 models and
// placed flat placeholder stubs. Custom Data/CIFP is navdata and is not entered.
TEST_CASE("scan: a library directly in Custom Data is found; Custom Data/CIFP is skipped") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "xpgsi_csl_xp12_layout";
    std::error_code ec;
    fs::remove_all(root, ec);
    auto write = [](const fs::path& p, const std::string& text) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << text;
    };
    const fs::path pkg = root / "Custom Data" / "X-CSL" / "B738";
    write(pkg / "xsb_aircraft.txt",
          "EXPORT_NAME B738\n\nOBJ8_AIRCRAFT B738_DAL\nOBJ8 SOLID YES B738:B738_DAL.obj\n"
          "VERT_OFFSET 0.0\nAIRLINE B738 DAL\n");
    write(pkg / "B738_DAL.obj", "I\n800\nOBJ\n");
    const fs::path cifp = root / "Custom Data" / "CIFP" / "NAV";
    write(cifp / "xsb_aircraft.txt",
          "EXPORT_NAME NAV\n\nOBJ8_AIRCRAFT A320_X\nOBJ8 SOLID YES NAV:a.obj\nICAO A320\n");
    write(cifp / "a.obj", "I\n800\nOBJ\n");

    CslDb db = CslScanner::scan(root);
    REQUIRE(db.count("B738") == 1);
    CHECK(db.at("B738")[0].airlineCode == "DAL");
    CHECK(db.at("B738")[0].objPaths[0] == pkg / "B738_DAL.obj");
    CHECK(db.count("A320") == 0);

    fs::remove_all(root, ec);
}

TEST_CASE("scan: finds Custom Data/CSL, Custom Scenery and Resources/plugins packages") {
    CslDb db = CslScanner::scan(kCslRoot);

    // pkg_a (Custom Scenery) contributes A320, A321, A306
    CHECK(db.count("A320") == 1);
    CHECK(db.count("A321") == 1);
    CHECK(db.count("A306") == 1);
    // pkg_b (Resources/plugins) contributes B738, B763 (B744 has no valid package)
    CHECK(db.count("B738") == 1);
    CHECK(db.count("B763") == 1);
    CHECK(db.count("B744") == 0);
    // X-CSL (Custom Data/CSL) contributes A20N
    CHECK(db.count("A20N") == 1);
}

TEST_CASE("scan: cross-package object resolves through the global package map") {
    CslDb db = CslScanner::scan(kCslRoot);

    REQUIRE(db.count("A20N") == 1);
    const auto& models = db.at("A20N");
    REQUIRE(models.size() == 5);
    const auto& shared = models[1];       // A20N_SHARED
    REQUIRE(shared.objPaths.size() == 2);
    CHECK(shared.objPaths[0] == kXcslA20N / "A20NCFM_AAF.obj");
    CHECK(shared.objPaths[1] == kXcslShared / "wheels.obj");
}

TEST_CASE("scan: total variant count matches fixture content") {
    CslDb db = CslScanner::scan(kCslRoot);
    size_t total = 0;
    for (const auto& [icao, entry] : db) total += entry.size();
    CHECK(total == 10); // 3 from pkg_a + 2 from pkg_b + 5 from X-CSL A20N
}

TEST_CASE("scan: returns empty db for non-existent root") {
    CslDb db = CslScanner::scan("/no/such/directory");
    CHECK(db.empty());
}
