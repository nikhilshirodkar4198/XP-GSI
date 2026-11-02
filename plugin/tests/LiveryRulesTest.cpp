// Livery and layout fixes from the KFAT / KPIT / RJTT test flights
// (2026-10-26): retired paint, regionals in partner paint, look-alike types,
// sparser remote stands.
#include <doctest/doctest.h>

#include "csl/CslScanner.h"
#include "csl/ModelSelector.h"
#include "csl/RelatedTypes.h"
#include "data/RegionalBrands.h"
#include "engine/Planner.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

CslVariant liv(const std::string& obj, const std::string& airline, const std::string& brand = {},
               bool retired = false) {
    CslVariant v;
    v.objPaths    = {obj};
    v.airlineCode = airline;
    v.brand       = brand;
    v.retired     = retired;
    return v;
}

Stand gate() {
    Stand s;
    s.widthCode = 'C';
    s.lat = 36.776; s.lon = -119.718;
    return s;
}

RegionalBrands skywest() {
    RegionalBrands b;
    b.addOperator("SKW", {"UAL", "DAL", "AAL", "ASA"});
    b.addArticle("brand:United Express", "UAL");
    b.addArticle("brand:Delta Connection", "DAL");
    return b;
}

} // namespace

TEST_SUITE("LiveryRules") {

TEST_CASE("overrides file: statuses, years, brands, defunct airlines, OLD tags") {
    const auto file = std::filesystem::temp_directory_path() / "xpgsi_overrides_test.json";
    std::ofstream(file) << R"({"schema":2, "defunct_airlines":["AWE"], "old_livery_tags":["OLD"],
        "entries": {
          "X-CSL/CRJ2/CRJ2_JIA.obj":  {"status":"defunct"},
          "X-CSL/CRJ2/CRJ2_SKW2.obj": {"status":"current", "brand":"DAL", "special":false},
          "X-CSL/B733/B733_SWA.obj":  {"status":"outdated", "year":2001},
          "X-CSL/B738/B738_UAL.obj":  {"status":"legacy", "year":2010}}})";
    CslDb db;
    db["CRJ2"].push_back(liv("/csl/X-CSL/CRJ2/CRJ2_JIA.obj", "JIA"));
    db["CRJ2"].push_back(liv("/csl/X-CSL/CRJ2/CRJ2_AWE.obj", "AWE"));
    auto skw2 = liv("C:\\csl\\X-CSL\\CRJ2\\CRJ2_SKW2.obj", "SKW");
    skw2.special = true;                                   // X-CSL numbered livery
    db["CRJ2"].push_back(skw2);
    auto old = liv("/csl/BB/CRJ2/CRJ2_XYZ_OLD.obj", "XYZ");
    old.livery = "OLD";                                    // another library's own OLD tag
    db["CRJ2"].push_back(old);
    db["B733"].push_back(liv("/csl/X-CSL/B733/B733_SWA.obj", "SWA"));
    db["B738"].push_back(liv("/csl/X-CSL/B738/B738_UAL.obj", "UAL"));
    const auto st = CslScanner::applyLiveryOverrides(db, file);
    CHECK(st.matched == 4);
    const auto& e = db.at("CRJ2");
    CHECK(e[0].retired);
    CHECK(e[1].retired);                                   // defunct airline code
    CHECK(e[2].brand == "DAL");
    CHECK_FALSE(e[2].special);
    CHECK(e[3].era == CslVariant::Era::Outdated);
    CHECK(db.at("B733")[0].era == CslVariant::Era::Outdated);
    CHECK(db.at("B733")[0].schemeYear == 2001);
    CHECK(db.at("B738")[0].era == CslVariant::Era::Legacy);
    std::filesystem::remove(file);
}

TEST_CASE("an entry whose texture size differs (another library version) is skipped") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "xpgsi_fp" / "X-CSL" / "B738";
    fs::create_directories(dir);
    std::ofstream(dir / "B738_SWA.obj") << "I\n800\nOBJ\n\nTEXTURE SWA.png\nPOINT_COUNTS 0 0 0 0\n";
    std::ofstream(dir / "SWA.png") << std::string(1234, 'x');
    const auto file = fs::temp_directory_path() / "xpgsi_fp_overrides.json";
    auto write = [&](int bytes) {
        std::ofstream(file) << R"({"schema":2, "entries": {"X-CSL/B738/B738_SWA.obj":
            {"status":"outdated", "year":2001, "tex_bytes":)" << bytes << "}}}";
    };
    CHECK(CslScanner::objTexture(dir / "B738_SWA.obj") == dir / "SWA.png");
    for (const int bytes : {1234, 999}) {
        write(bytes);
        CslDb db;
        db["B738"].push_back(liv((dir / "B738_SWA.obj").string(), "SWA"));
        const auto st = CslScanner::applyLiveryOverrides(db, file);
        if (bytes == 1234) {
            CHECK(st.matched == 1);
            CHECK(db.at("B738")[0].era == CslVariant::Era::Outdated);
        } else {                                           // repainted file: left as current
            CHECK(st.stale == 1);
            CHECK(db.at("B738")[0].era == CslVariant::Era::Current);
        }
    }
    fs::remove_all(fs::temp_directory_path() / "xpgsi_fp");
    fs::remove(file);
}

TEST_CASE("retired paint is never chosen, from the table or by size alone") {
    CslDb db;
    db["CRJ2"].push_back(liv("/jia.obj", "JIA", {}, /*retired=*/true));
    db["CRJ2"].push_back(liv("/gen.obj", ""));
    AirportTable t;
    t.airlines = {{"JIA", 1.0f}};
    t.types["JIA"]["CRJ2"] = 1.0f;
    for (std::uint32_t seed = 1; seed < 100; ++seed) {
        CHECK(ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u).model->objPaths[0] == "/gen.obj");
        CHECK(ModelSelector::selectForStand(gate(), db, seed)->objPaths[0] == "/gen.obj");
    }
}

TEST_CASE("SkyWest is drawn in partner paint, by brand presence, never its own") {
    CslDb db;
    db["CRJ2"].push_back(liv("/skw_own.obj", "SKW"));           // its own (untagged) scheme
    db["CRJ2"].push_back(liv("/skw_dl.obj", "SKW", "DAL"));
    db["CRJ2"].push_back(liv("/asq_ua.obj", "ASQ", "UAL"));     // another regional's United Express
    db["E175"].push_back(liv("/rpa_ua.obj", "RPA", "UAL"));
    AirportTable t;
    t.airlines = {{"SKW", 1.0f}};
    t.types["SKW"] = {{"E75L", 0.6f}, {"CRJ9", 0.4f}};         // adsb.lol designators
    RelatedTypes rel;
    rel.addAlias("E75L", "E175");
    rel.addGroup({"CRJ1", "CRJ2", "CRJ7", "CRJ9", "CRJX"});
    const RegionalBrands b = skywest();
    ModelSelector::Extras ex;
    ex.related = &rel;
    ex.brands  = &b;
    WikiEntry w;
    w.airlines = {{"brand:United Express", 1.0f}};              // a United Express-only airport
    ex.brandPresence = b.presence(&w, t);
    std::set<std::string> seen;
    for (std::uint32_t seed = 1; seed < 300; ++seed) {
        auto c = ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u, nullptr, &ex);
        REQUIRE(c.model);
        CHECK(c.reason == ModelSelector::Choice::Reason::PartnerBrand);
        seen.insert(c.model->objPaths[0].generic_string());
    }
    CHECK(seen == std::set<std::string>{"/asq_ua.obj", "/rpa_ua.obj"});   // United Express only

    // Delta Connection airport: SkyWest's own Delta Connection CRJ.
    w.airlines = {{"brand:Delta Connection", 1.0f}};
    ex.brandPresence = b.presence(&w, t);
    seen.clear();
    for (std::uint32_t seed = 1; seed < 300; ++seed)
        seen.insert(ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u, nullptr, &ex)
                        .model->objPaths[0].generic_string());
    CHECK(seen == std::set<std::string>{"/skw_dl.obj"});
}

TEST_CASE("nothing says which partner: any partner paint, still not its own") {
    CslDb db;
    db["CRJ2"].push_back(liv("/skw_own.obj", "SKW"));
    db["CRJ2"].push_back(liv("/skw_dl.obj", "SKW", "DAL"));
    db["CRJ2"].push_back(liv("/skw_aa.obj", "SKW", "AAL"));
    AirportTable t;
    t.airlines = {{"SKW", 1.0f}};
    t.types["SKW"]["CRJ2"] = 1.0f;
    const RegionalBrands b = skywest();
    ModelSelector::Extras ex;
    ex.brands = &b;
    std::set<std::string> seen;
    for (std::uint32_t seed = 1; seed < 300; ++seed)
        seen.insert(ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u, nullptr, &ex)
                        .model->objPaths[0].generic_string());
    CHECK(seen == std::set<std::string>{"/skw_dl.obj", "/skw_aa.obj"});
}

TEST_CASE("a mainline static may use a regional's paint of its brand") {
    CslDb db;
    db["E175"].push_back(liv("/cpz_dl.obj", "CPZ", "DAL"));
    AirportTable t;
    t.airlines = {{"DAL", 1.0f}};
    t.types["DAL"]["E175"] = 1.0f;
    auto c = ModelSelector::selectWithTable(gate(), db, t, 9);
    CHECK(c.reason == ModelSelector::Choice::Reason::ExactLivery);
}

TEST_CASE("newer paint first: current over legacy, out-of-use only as a last resort, newest first") {
    auto era = [](CslVariant v, CslVariant::Era e, int year = 0) { v.era = e; v.schemeYear = year; return v; };
    AirportTable t;
    t.airlines = {{"SWA", 1.0f}};
    t.types["SWA"] = {{"B737", 0.7f}, {"B738", 0.3f}};
    CslDb db;
    // Southwest: B737 only in Canyon Blue (out of use), B738 in Heart.
    db["B737"].push_back(era(liv("/b737_canyon.obj", "SWA"), CslVariant::Era::Outdated, 2001));
    db["B738"].push_back(liv("/b738_heart.obj", "SWA"));
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        auto c = ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u);
        CHECK(c.model->objPaths[0] == "/b738_heart.obj");
    }
    // Nothing newer anywhere: the newest out-of-use scheme (Canyon Blue 2001
    // over Desert Gold 1971).
    db.erase("B738");
    db["B737"].push_back(era(liv("/b737_gold.obj", "SWA"), CslVariant::Era::Outdated, 1971));
    for (std::uint32_t seed = 1; seed < 100; ++seed)
        CHECK(ModelSelector::selectWithTable(gate(), db, t, seed * 2654435761u).model->objPaths[0] ==
              "/b737_canyon.obj");
    // Current and legacy both flying: mostly current, sometimes legacy.
    CslDb ua;
    ua["B738"].push_back(liv("/ua_new.obj", "UAL"));
    ua["B738"].push_back(era(liv("/ua_globe.obj", "UAL"), CslVariant::Era::Legacy, 2010));
    AirportTable u;
    u.airlines = {{"UAL", 1.0f}};
    u.types["UAL"]["B738"] = 1.0f;
    int legacy = 0;
    for (std::uint32_t seed = 1; seed <= 400; ++seed)
        legacy += ModelSelector::selectWithTable(gate(), ua, u, seed * 2654435761u).model->objPaths[0] ==
                  "/ua_globe.obj";
    MESSAGE("legacy share: " << legacy / 400.0);
    CHECK(legacy > 40);
    CHECK(legacy < 180);
}

TEST_CASE("look-alike types: alias first, then the nearest family member, smaller on a tie") {
    CslDb db;
    db["E175"].push_back(liv("/e175.obj", ""));
    db["E190"].push_back(liv("/e190.obj", ""));
    db["CRJ2"].push_back(liv("/crj2.obj", ""));
    RelatedTypes r;
    r.addAlias("E75L", "E175");
    r.addGroup({"CRJ1", "CRJ2", "CRJ7", "CRJ9", "CRJX"});
    r.addGroup({"E170", "E175", "E75S", "E75L", "E190", "E195"});
    CHECK(r.substitute("E175", db) == "E175");
    CHECK(r.substitute("E75L", db) == "E175");      // alias, though E190 is nearer in the list
    CHECK(r.substitute("E170", db) == "E175");
    CHECK(r.substitute("CRJ9", db) == "CRJ2");
    CHECK(r.substitute("E75S", db) == "E175");      // tie E175 / E190 → the smaller
    CHECK(r.substitute("B738", db).empty());
    db["E175"][0].retired = true;                   // only retired models: not usable
    CHECK(r.substitute("E75L", db) == "E190");
}

TEST_CASE("remote (tie-down) stands are filled after gates far more often") {
    std::vector<Stand> stands;
    for (int i = 0; i < 200; ++i) {
        Stand s;
        s.lat  = 35.55 + i * 1e-4;
        s.lon  = 139.78;
        s.type = i < 100 ? StandType::Gate : StandType::TieDown;
        stands.push_back(s);
    }
    int remoteInFirstHalf = 0, runs = 50;
    for (int r = 1; r <= runs; ++r) {
        auto order = Planner::rankStands(stands, static_cast<std::uint32_t>(r) * 2654435761u);
        for (int k = 0; k < 100; ++k) remoteInFirstHalf += order[k] >= 100;
    }
    const double share = static_cast<double>(remoteInFirstHalf) / (runs * 100);
    MESSAGE("remote share of the first half: " << share);
    CHECK(share > 0.10);    // still some remote stands at 50 % density
    CHECK(share < 0.35);    // but far fewer than the 50 % an even draw gives
    CHECK(Planner::isRemote(stands[150]));
    CHECK_FALSE(Planner::isRemote(stands[10]));
}

} // TEST_SUITE
