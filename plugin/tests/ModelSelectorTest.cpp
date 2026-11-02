#include <doctest/doctest.h>

#include "csl/CslDb.h"
#include "csl/IcaoSize.h"
#include "csl/ModelSelector.h"
#include "engine/StandState.h"

// ── IcaoSize ─────────────────────────────────────────────────────────────────

TEST_SUITE("IcaoSize") {

TEST_CASE("known types return expected size codes") {
    CHECK(IcaoSize::lookup("B738") == 'C');
    CHECK(IcaoSize::lookup("A320") == 'C');
    CHECK(IcaoSize::lookup("B77W") == 'E');
    CHECK(IcaoSize::lookup("A388") == 'F');
    CHECK(IcaoSize::lookup("DH8D") == 'B');
    CHECK(IcaoSize::lookup("C172") == 'A');
    CHECK(IcaoSize::lookup("B752") == 'D');
    CHECK(IcaoSize::lookup("B748") == 'F');
}

TEST_CASE("unknown type returns question-mark") {
    CHECK(IcaoSize::lookup("ZZZZ") == '?');
    CHECK(IcaoSize::lookup("")     == '?');
}

} // TEST_SUITE

// ── ModelSelector ─────────────────────────────────────────────────────────────

static Stand makeStand(char widthCode) {
    Stand s;
    s.widthCode = widthCode;
    return s;
}

// First OBJ of the selected model, or an empty path if none was selected.
static std::filesystem::path sel(const Stand& s, const CslDb& db, std::uint32_t seed = 0) {
    const CslVariant* v = ModelSelector::selectForStand(s, db, seed);
    return (v && !v->objPaths.empty()) ? v->objPaths.front() : std::filesystem::path{};
}

static CslDb makeDb(const std::string& icao, const std::string& path) {
    CslDb db;
    CslVariant v;
    v.objPaths = {path};
    db[icao].push_back(v);
    return db;
}

TEST_SUITE("ModelSelector") {

TEST_CASE("empty db returns empty path") {
    CslDb db;
    CHECK(sel(makeStand('C'), db).empty());
}

TEST_CASE("exact size match returned") {
    auto db = makeDb("B738", "/path/B738.obj");  // B738 → C
    auto result = sel(makeStand('C'), db);
    CHECK(result == std::filesystem::path("/path/B738.obj"));
}

TEST_CASE("falls back to smaller size before larger") {
    // Only a B-size model available; requesting C
    auto db = makeDb("DH8D", "/path/DH8D.obj");   // DH8D → B
    auto result = sel(makeStand('C'), db);
    CHECK(!result.empty());  // should find DH8D as nearest smaller
    CHECK(result == std::filesystem::path("/path/DH8D.obj"));
}

TEST_CASE("never larger than the stand: only an E model for a C stand → nothing") {
    auto db = makeDb("B77W", "/path/B77W.obj");   // B77W → E
    CHECK(sel(makeStand('C'), db).empty());
}

TEST_CASE("never a non-airliner: GA / bizjet / unknown types are skipped") {
    CslDb db;
    for (const char* t : {"C208", "C172", "PC12", "GLID", "F16"}) {
        CslVariant v;
        v.objPaths = {std::string("/path/") + t + ".obj"};
        db[t].push_back(v);
    }
    CHECK(sel(makeStand('C'), db).empty());
    CHECK(sel(makeStand('A'), db).empty());   // even on a size-A airline stand
    CslVariant atr;
    atr.objPaths = {"/path/AT76.obj"};
    db["AT76"].push_back(atr);                // a real airliner is still found
    CHECK(sel(makeStand('C'), db) == std::filesystem::path("/path/AT76.obj"));
}

TEST_CASE("unknown stand width code defaults to C matching") {
    auto db = makeDb("B738", "/path/B738.obj");   // B738 → C
    auto result = sel(makeStand('?'), db);
    CHECK(!result.empty());
}

TEST_CASE("type with unknown size code is never returned") {
    CslDb db;
    CslVariant v;
    v.objPaths = {"/path/ZZZZ.obj"};
    db["ZZZZ"].push_back(v);
    CHECK(sel(makeStand('C'), db).empty());
}

TEST_CASE("prefers E model for E stand over C fallback") {
    CslDb db;
    CslVariant vC, vE;
    vC.objPaths = {"/path/A320.obj"};
    vE.objPaths = {"/path/B77W.obj"};
    db["A320"].push_back(vC);  // C
    db["B77W"].push_back(vE);  // E
    auto result = sel(makeStand('E'), db);
    CHECK(result == std::filesystem::path("/path/B77W.obj"));
}

// ── Airline selection ──────────────────────────────────────────────────────────

static Stand makeAirlineStand(char widthCode, std::vector<std::string> airlines) {
    Stand s;
    s.widthCode = widthCode;
    s.airlines  = std::move(airlines);
    return s;
}

static CslDb makeAirlineDb(const std::string& icao, const std::string& airline,
                            const std::string& path) {
    CslDb db;
    CslVariant v;
    v.objPaths     = {path};
    v.airlineCode = airline;
    db[icao].push_back(v);
    return db;
}

TEST_CASE("airline + exact size preferred over generic size match") {
    CslDb db;
    CslVariant generic, ual;
    generic.objPaths = {"/path/A320.obj"};
    ual.objPaths     = {"/path/B738_UAL.obj"};
    ual.airlineCode = "UAL";
    db["A320"].push_back(generic);
    db["B738"].push_back(ual);

    auto result = sel(makeAirlineStand('C', {"UAL"}), db);
    CHECK(result == std::filesystem::path("/path/B738_UAL.obj"));
}

TEST_CASE("airline match on a smaller type beats generic size match") {
    CslDb db;
    CslVariant generic, dal;
    generic.objPaths = {"/path/A320.obj"};
    dal.objPaths     = {"/path/CRJ2_DAL.obj"};   // CRJ2 → B, fits a C stand
    dal.airlineCode = "DAL";
    db["A320"].push_back(generic);
    db["CRJ2"].push_back(dal);

    auto result = sel(makeAirlineStand('C', {"DAL"}), db);
    CHECK(result == std::filesystem::path("/path/CRJ2_DAL.obj"));
}

TEST_CASE("airline livery on a larger type is not used") {
    CslDb db;
    CslVariant generic, dal;
    generic.objPaths = {"/path/A320.obj"};
    dal.objPaths     = {"/path/B77W_DAL.obj"};
    dal.airlineCode = "DAL";
    db["A320"].push_back(generic);
    db["B77W"].push_back(dal);

    auto result = sel(makeAirlineStand('C', {"DAL"}), db);
    CHECK(result == std::filesystem::path("/path/A320.obj"));
}

TEST_CASE("no matching airline falls back to size match") {
    CslDb db;
    CslVariant v;
    v.objPaths = {"/path/A320.obj"};
    db["A320"].push_back(v);

    auto result = sel(makeAirlineStand('C', {"UAL"}), db);
    CHECK(result == std::filesystem::path("/path/A320.obj"));
}

TEST_CASE("stand with no airlines uses size-only selection") {
    auto db = makeDb("B738", "/path/B738.obj");
    auto result = sel(makeStand('C'), db);
    CHECK(result == std::filesystem::path("/path/B738.obj"));
}

TEST_CASE("generic variant preferred over airline-specific for size-only stands") {
    CslDb db;
    CslVariant ual, generic;
    ual.objPaths     = {"/path/B738_UAL.obj"};
    ual.airlineCode = "UAL";
    generic.objPaths = {"/path/A320.obj"};
    db["B738"].push_back(ual);
    db["A320"].push_back(generic);

    auto result = sel(makeStand('C'), db);
    CHECK(result == std::filesystem::path("/path/A320.obj"));
}

// ── Seed-based variation ───────────────────────────────────────────────────────

TEST_CASE("seed=0 and seed=1 with two variants produce different results") {
    // Two generic C-size types in the db
    CslDb db;
    CslVariant v1, v2;
    v1.objPaths = {"/path/A320.obj"};
    v2.objPaths = {"/path/B738.obj"};
    db["A320"].push_back(v1);
    db["B738"].push_back(v2);

    auto r0 = sel(makeStand('C'), db, 0);
    auto r1 = sel(makeStand('C'), db, 1);
    // Both should be valid paths
    CHECK(!r0.empty());
    CHECK(!r1.empty());
    // With two candidates, seeds 0 and 1 pick different ones
    CHECK(r0 != r1);
}

TEST_CASE("same seed always returns same result (deterministic)") {
    CslDb db;
    CslVariant v1, v2;
    v1.objPaths = {"/path/A320.obj"};
    v2.objPaths = {"/path/B738.obj"};
    db["A320"].push_back(v1);
    db["B738"].push_back(v2);

    auto first  = sel(makeStand('C'), db, 42);
    auto second = sel(makeStand('C'), db, 42);
    CHECK(first == second);
}

TEST_CASE("seed with single variant always returns that variant") {
    auto db = makeDb("B738", "/path/B738.obj");
    for (std::uint32_t seed : {0u, 1u, 99u, 1000u}) {
        CHECK(sel(makeStand('C'), db, seed)
              == std::filesystem::path("/path/B738.obj"));
    }
}

// ── Whole-model selection ─────────────────────────────────────────────────────

TEST_CASE("returns the whole model: every OBJ part and its vertical offset") {
    CslDb db;
    CslVariant v;
    v.objPaths = {"/x/A20NCFMfan.obj", "/x/A20NCFM_AAF.obj"};
    v.vertOfsM = 1.25f;
    v.airlineCode = "AAF";
    db["A20N"].push_back(v);

    const CslVariant* r = ModelSelector::selectForStand(makeAirlineStand('C', {"AAF"}), db);
    REQUIRE(r != nullptr);
    CHECK(r == &db.at("A20N")[0]);       // pointer into the catalogue, not a copy
    CHECK(r->objPaths.size() == 2);
    CHECK(r->vertOfsM == doctest::Approx(1.25f));
}

TEST_CASE("empty db returns nullptr") {
    CslDb db;
    CHECK(ModelSelector::selectForStand(makeStand('C'), db) == nullptr);
}

TEST_CASE("positionSeed: stable for the same position, differs for another") {
    Stand a; a.lat = 25.7959; a.lon = -80.2870;
    Stand b = a;
    Stand c = a; c.lon += 0.0005;
    CHECK(ModelSelector::positionSeed(a) == ModelSelector::positionSeed(b));
    CHECK(ModelSelector::positionSeed(a) != ModelSelector::positionSeed(c));
}

} // TEST_SUITE
