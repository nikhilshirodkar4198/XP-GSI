#include <doctest/doctest.h>

#include "csl/CslDb.h"
#include "csl/ModelSelector.h"
#include "data/AirportData.h"
#include "engine/StandState.h"

#include <filesystem>
#include <map>
#include <string>

// FIXTURES_DIR is injected at compile time from CMake.
static const std::filesystem::path kData = std::filesystem::path(FIXTURES_DIR);

TEST_SUITE("AirportData") {

TEST_CASE("loads airlines, per-airline types and samples") {
    auto t = AirportData::loadFor(kData, "KTST");
    REQUIRE(t);
    CHECK(t->icao == "KTST");
    CHECK(t->samples == 120);
    CHECK(t->airlines.size() == 3);
    CHECK(t->airlines.at("SWA") == doctest::Approx(0.6f));
    CHECK(t->types.at("DAL").at("B77W") == doctest::Approx(0.3f));
}

TEST_CASE("missing file or other schema → nullopt") {
    CHECK_FALSE(AirportData::loadFor(kData, "ZZZZ"));
    CHECK_FALSE(AirportData::loadFor(kData, "KBAD"));   // schema 2
}

} // TEST_SUITE

// ── Table-driven selection ────────────────────────────────────────────────────

static CslVariant variant(const std::string& path, const std::string& airline) {
    CslVariant v;
    v.objPaths    = {path};
    v.airlineCode = airline;
    return v;
}

static Stand stand(char size, std::vector<std::string> airlines = {}, double lon = -86.29) {
    Stand s;
    s.widthCode = size;
    s.airlines  = std::move(airlines);
    s.lat = 39.71; s.lon = lon;
    return s;
}

static AirportTable table() { return *AirportData::loadFor(kData, "KTST"); }

TEST_SUITE("ModelSelector::selectWithTable") {

TEST_CASE("exact airline livery of a table type") {
    CslDb db;
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    db["B738"].push_back(variant("/afl738.obj", "AFL"));
    auto c = ModelSelector::selectWithTable(stand('C', {"SWA"}), db, table(), 7);
    REQUIRE(c.model);
    CHECK(c.reason == ModelSelector::Choice::Reason::ExactLivery);
    CHECK(c.airline == "SWA");
    CHECK(c.model->airlineCode == "SWA");
}

TEST_CASE("never picks an airline or type the table doesn't list") {
    // A library full of Aeroflot / MD-80 liveries must not leak in while a
    // table airline has a fitting type.
    CslDb db;
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    db["A321"].push_back(variant("/dal321.obj", "DAL"));
    db["A321"].push_back(variant("/aal321.obj", "AAL"));
    db["MD82"].push_back(variant("/md82.obj", "AFL"));
    db["A320"].push_back(variant("/afl320.obj", "AFL"));
    std::map<std::string, int> seen;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        auto c = ModelSelector::selectWithTable(stand('C', {}, -86.29 + seed * 1e-4), db, table(),
                                                seed * 2654435761u);
        REQUIRE(c.model);
        CHECK(c.model->airlineCode != "AFL");
        ++seen[c.airline];
    }
    // Roughly the table mix (SWA 0.6 / DAL 0.3 / AAL 0.1).
    CHECK(seen["SWA"] > seen["DAL"]);
    CHECK(seen["DAL"] > seen["AAL"]);
    CHECK(seen["AAL"] > 0);
}

TEST_CASE("type must fit the stand: no B77W at a C gate") {
    CslDb db;
    db["A321"].push_back(variant("/dal321.obj", "DAL"));
    db["B77W"].push_back(variant("/dal77w.obj", "DAL"));
    for (std::uint32_t seed = 0; seed < 100; ++seed) {
        auto c = ModelSelector::selectWithTable(stand('C', {"DAL"}), db, table(), seed * 40503u);
        REQUIRE(c.model);
        CHECK(c.type == "A321");
    }
}

TEST_CASE("airline livery on another of its types beats a foreign livery") {
    // SWA flies B737 and B738; only a B738 SWA livery and a foreign B737 exist.
    CslDb db;
    db["B737"].push_back(variant("/afl737.obj", "AFL"));
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        auto c = ModelSelector::selectWithTable(stand('C', {"SWA"}), db, table(), seed * 97u);
        REQUIRE(c.model);
        CHECK(c.model->airlineCode == "SWA");
    }
}

TEST_CASE("plain livery when the airline has none; never another airline's") {
    CslDb db;
    db["A321"].push_back(variant("/a321_plain.obj", ""));
    db["A321"].push_back(variant("/a321_afl.obj", "AFL"));
    auto c = ModelSelector::selectWithTable(stand('C', {"AAL"}), db, table(), 3);
    REQUIRE(c.model);
    CHECK(c.reason == ModelSelector::Choice::Reason::GenericLivery);
    CHECK(c.model->airlineCode.empty());
}

TEST_CASE("stand's 1301 airline wins over table weights") {
    CslDb db;
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    db["A321"].push_back(variant("/aal321.obj", "AAL"));
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        auto c = ModelSelector::selectWithTable(stand('C', {"AAL"}), db, table(), seed * 131u);
        CHECK(c.airline == "AAL");
    }
}

TEST_CASE("nothing in the table fits → no model, never another airline's paint") {
    CslDb db;
    db["E75L"].push_back(variant("/e75.obj", "SKW"));
    auto c = ModelSelector::selectWithTable(stand('C'), db, table(), 1);
    CHECK(c.reason == ModelSelector::Choice::Reason::NoTableMatch);
    CHECK(c.model == nullptr);
}

TEST_CASE("deterministic for a given seed") {
    CslDb db;
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    db["B737"].push_back(variant("/swa737.obj", "SWA"));
    db["A321"].push_back(variant("/dal321.obj", "DAL"));
    auto a = ModelSelector::selectWithTable(stand('C'), db, table(), 12345);
    auto b = ModelSelector::selectWithTable(stand('C'), db, table(), 12345);
    CHECK(a.model == b.model);
}

// ── Where airlines actually park (clusters) ───────────────────────────────────

static Stand standAt(char size, double lat, double lon) {
    Stand s;
    s.widthCode = size;
    s.lat = lat; s.lon = lon;
    return s;
}

static CslDb parkDb() {
    CslDb db;
    db["B738"].push_back(variant("/swa738.obj", "SWA"));
    db["A321"].push_back(variant("/dal321.obj", "DAL"));
    db["A321"].push_back(variant("/aal321.obj", "AAL"));
    db["B77W"].push_back(variant("/baw77w.obj", "BAW"));
    db["B77W"].push_back(variant("/dal77w.obj", "DAL"));
    db["A320"].push_back(variant("/baw320.obj", "BAW"));
    return db;
}

TEST_CASE("clusters are loaded") {
    auto t = table();
    REQUIRE(t.clusters.size() == 2);
    CHECK(t.clusters[0].n == doctest::Approx(10.0f));
    CHECK(t.clusters[0].airlines.at("BAW") == doctest::Approx(10.0f));
    CHECK(t.clusters[1].types.at("SWA").at("B738") == doctest::Approx(12.0f));
}

TEST_CASE("area evidence falls off with distance") {
    auto t = table();
    auto at   = ModelSelector::areaMix(standAt('E', 39.80000, -86.29000), t);
    auto near = ModelSelector::areaMix(standAt('E', 39.80000, -86.29150), t);   // ~128 m west
    auto far  = ModelSelector::areaMix(standAt('E', 39.70000, -86.29000), t);   // 11 km south
    CHECK(at.evidence > near.evidence);
    CHECK(near.evidence > 0.0f);
    CHECK(far.evidence == doctest::Approx(0.0f));
    CHECK(at.airlines.count("BAW") == 1);
}

TEST_CASE("international-gate area: only the airlines that park there, in their types") {
    // An E stand in the BAW cluster: never Southwest (60 % airport-wide),
    // and BA's 777 rather than its A320.
    auto db = parkDb();
    for (std::uint32_t seed = 0; seed < 200; ++seed) {
        auto c = ModelSelector::selectWithTable(standAt('E', 39.80000, -86.29000), db, table(),
                                                seed * 2654435761u);
        REQUIRE(c.model);
        CHECK(c.fromArea);
        CHECK(c.airline == "BAW");
        CHECK(c.type == "B77W");
    }
}

TEST_CASE("narrowbody stand in that area: same airline, a type that fits") {
    auto db = parkDb();
    auto c = ModelSelector::selectWithTable(standAt('C', 39.80000, -86.29000), db, table(), 77);
    REQUIRE(c.model);
    CHECK(c.airline == "BAW");
    CHECK(c.type == "A320");   // airport-wide BAW types fill in when the area's don't fit
}

TEST_CASE("Southwest's terminal gets Southwest") {
    auto db = parkDb();
    for (std::uint32_t seed = 0; seed < 100; ++seed) {
        auto c = ModelSelector::selectWithTable(standAt('C', 39.80500, -86.29000), db, table(), seed * 97u);
        CHECK(c.airline == "SWA");
    }
}

TEST_CASE("no parking data nearby: airport-wide mix") {
    auto db = parkDb();
    std::map<std::string, int> seen;
    for (std::uint32_t seed = 0; seed < 300; ++seed) {
        auto c = ModelSelector::selectWithTable(standAt('C', 39.70000, -86.29000), db, table(),
                                                seed * 40503u);
        CHECK_FALSE(c.fromArea);
        ++seen[c.airline];
    }
    CHECK(seen["SWA"] > seen["DAL"]);
    CHECK(seen["DAL"] > 0);
}

TEST_CASE("a stand's scenery airline list gives way where the data sees others park (2026-10-27, KLAS)") {
    auto db = parkDb();
    Stand s = standAt('C', 39.80000, -86.29000);   // only British Airways parks here
    s.airlines = {"AAL"};
    CHECK(ModelSelector::selectWithTable(s, db, table(), 5).airline == "BAW");
    s.airlines = {"AAL", "BAW"};                     // a list that fits the data still decides
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        const auto a = ModelSelector::selectWithTable(s, db, table(), seed * 97u).airline;
        CHECK((a == "AAL" || a == "BAW"));
    }
}

TEST_CASE("a special livery is placed at most once per airport") {
    // DAL flies A321 and B77W here; one standard and one special livery each.
    CslDb db;
    CslVariant std321 = variant("/dal321.obj", "DAL");
    CslVariant sp321  = variant("/dal321_retro.obj", "DAL");
    sp321.livery = "2"; sp321.special = true;
    db["A321"].push_back(std321);
    db["A321"].push_back(sp321);
    ModelSelector::VariantSet used;
    int special = 0, standard = 0;
    for (std::uint32_t seed = 0; seed < 60; ++seed) {
        auto c = ModelSelector::selectWithTable(stand('C', {"DAL"}, -86.29 + seed * 1e-4), db, table(),
                                                seed * 2654435761u, &used);
        REQUIRE(c.model);
        if (c.model->special) { ++special; used.insert(c.model); }
        else ++standard;
    }
    CHECK(special <= 1);
    CHECK(standard >= 59);
}

TEST_CASE("standard liveries are preferred; a special is an occasional pick") {
    // One standard and three specials: without the bias most first picks
    // would be specials (3 in 4).
    CslDb db;
    db["A321"].push_back(variant("/dal321.obj", "DAL"));
    for (const char* p : {"/dal321_retro.obj", "/dal321_pink.obj", "/dal321_team.obj"}) {
        CslVariant sp = variant(p, "DAL");
        sp.livery = "2"; sp.special = true;
        db["A321"].push_back(sp);
    }
    int special = 0;
    const int n = 4000;
    for (int i = 0; i < n; ++i) {
        auto c = ModelSelector::selectWithTable(stand('C', {"DAL"}, -86.29 + i * 1e-5), db, table(),
                                                static_cast<std::uint32_t>(i) * 2654435761u);
        REQUIRE(c.model);
        if (c.model->special) ++special;
    }
    CHECK(special > n / 20 * 0.8);   // kSpecialOdds = 20
    CHECK(special < n / 20 * 1.2);
}

TEST_CASE("only special liveries left: stand still gets a model of the type") {
    CslDb db;
    CslVariant sp = variant("/dal321_retro.obj", "DAL");
    sp.livery = "2"; sp.special = true;
    db["A321"].push_back(sp);
    db["A321"].push_back(variant("/a321_sn.obj", "@SN"));
    db["A321"].push_back(variant("/a321_plain.obj", ""));
    ModelSelector::VariantSet used{&db["A321"][0]};
    auto c = ModelSelector::selectWithTable(stand('C', {"DAL"}), db, table(), 1, &used);
    REQUIRE(c.model);
    CHECK_FALSE(c.model->special);
    CHECK(c.model->airlineCode.empty());   // generic livery instead of a second retro jet
}

TEST_CASE("only a used special and state paint left: the stand stays empty") {
    CslDb db;
    CslVariant sp = variant("/dal321_retro.obj", "DAL");
    sp.livery = "2"; sp.special = true;
    db["A321"].push_back(sp);
    db["A321"].push_back(variant("/a321_sn.obj", "@SN"));
    ModelSelector::VariantSet used{&db["A321"][0]};
    auto c = ModelSelector::selectWithTable(stand('C', {"DAL"}), db, table(), 1, &used);
    CHECK(c.model == nullptr);   // never a government aircraft or another airline's paint
}

TEST_CASE("X-CSL @-codes are state aircraft, not generic liveries") {
    CHECK_FALSE(ModelSelector::isGenericLivery(variant("/x.obj", "@SN")));   // retired by the overrides
    CHECK(ModelSelector::isGenericLivery(variant("/x.obj", "")));
    CHECK_FALSE(ModelSelector::isGenericLivery(variant("/x.obj", "SWA")));
}

} // TEST_SUITE
