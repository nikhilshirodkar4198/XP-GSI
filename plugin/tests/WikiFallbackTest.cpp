// Wikipedia airline fallback: loading, brand expansion and blending with the
// adsb.lol tables.
#include <doctest/doctest.h>

#include "csl/ModelSelector.h"
#include "data/WikiFallback.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static fs::path writeTemp(const std::string& name, const std::string& text) {
    const fs::path p = fs::temp_directory_path() / ("xpgsi_wiki_" + name);
    std::ofstream(p) << text;
    return p;
}

static Fleets sampleFleets() {
    Fleets f;
    f.types["AAL"]["all"]           = {{"B738", 0.5f}, {"B77W", 0.5f}};
    f.types["AAL"]["medium_airport"] = {{"B738", 1.0f}};
    f.types["ENY"]["all"]           = {{"E175", 1.0f}};
    f.types["PDT"]["all"]           = {{"E145", 1.0f}};
    f.brands["brand:American Eagle (airline brand)"]["US"] = {{"ENY", 0.75f}, {"PDT", 0.25f}};
    f.brands["brand:American Eagle (airline brand)"]["*"]  = {{"ENY", 1.0f}};
    return f;
}

static WikiEntry sampleWiki() {
    WikiEntry w;
    w.icao = "KAVL";
    w.country = "US";
    w.airportType = "medium_airport";
    w.airlines = {{"AAL", 0.5f}, {"brand:American Eagle (airline brand)", 0.5f}};
    return w;
}

TEST_SUITE("WikiFallback") {

TEST_CASE("files load; an airport not in the file is nullopt") {
    const auto wiki = writeTemp("w.json", R"({"schema":1,"airports":{"KAVL":{"country":"US",
        "airport_type":"medium_airport","airlines":{"AAL":0.6,"brand:X":0.4}}}})");
    const auto fleets = writeTemp("f.json", R"({"schema":1,"fleets":{"AAL":{"all":{"B738":1}}},
        "brands":{"brand:X":{"*":{"SKW":1}}}})");
    auto e = WikiFallback::loadEntry(wiki, "KAVL");
    REQUIRE(e);
    CHECK(e->country == "US");
    CHECK(e->airlines.size() == 2);
    CHECK_FALSE(WikiFallback::loadEntry(wiki, "KXYZ"));
    auto f = WikiFallback::loadFleets(fleets);
    REQUIRE(f);
    CHECK(f->types.at("AAL").at("all").at("B738") == doctest::Approx(1.0));
    CHECK(f->brands.at("brand:X").at("*").at("SKW") == doctest::Approx(1.0));
    CHECK_FALSE(WikiFallback::loadFleets(fs::temp_directory_path() / "xpgsi_no_such_file.json"));
}

TEST_CASE("brands split by the country's operator shares, else everywhere") {
    const Fleets f = sampleFleets();
    WikiEntry w = sampleWiki();
    auto ops = WikiFallback::operators(w, f);
    CHECK(ops.at("AAL") == doctest::Approx(0.5));
    CHECK(ops.at("ENY") == doctest::Approx(0.375));
    CHECK(ops.at("PDT") == doctest::Approx(0.125));
    w.country = "CA";
    ops = WikiFallback::operators(w, f);
    CHECK(ops.at("ENY") == doctest::Approx(0.5));
    CHECK(ops.count("PDT") == 0);
    // An unknown brand is dropped and the rest renormalized.
    w.airlines = {{"AAL", 0.5f}, {"brand:Nobody", 0.5f}};
    ops = WikiFallback::operators(w, f);
    CHECK(ops.size() == 1);
    CHECK(ops.at("AAL") == doctest::Approx(1.0));
}

TEST_CASE("no adsb.lol table: Wikipedia alone, typed from the fleets by airport size") {
    const Fleets    f = sampleFleets();
    const WikiEntry w = sampleWiki();
    float share = 0;
    auto t = WikiFallback::combine(std::nullopt, &w, &f, &share);
    REQUIRE(t);
    CHECK(share == doctest::Approx(1.0));
    CHECK(t->icao == "KAVL");
    CHECK(t->airportType == "medium_airport");
    CHECK(t->airlines.at("AAL") == doctest::Approx(0.5));
    // Medium airport: AAL's medium-airport mix, not its widebodies.
    CHECK(t->types.at("AAL").size() == 1);
    CHECK(t->types.at("AAL").count("B738") == 1);
    CHECK(t->types.at("ENY").count("E175") == 1);
}

TEST_CASE("a thin adsb.lol table is blended in proportion to its stays") {
    const Fleets    f = sampleFleets();
    const WikiEntry w = sampleWiki();
    AirportTable a;
    a.icao = "KAVL";
    a.samples = 250;                        // half of kFullTrustSamples
    a.airlines = {{"AAL", 0.8f}, {"DAL", 0.2f}};
    a.types["AAL"] = {{"A321", 1.0f}};
    a.types["DAL"] = {{"A320", 1.0f}};
    float share = 0;
    auto t = WikiFallback::combine(a, &w, &f, &share);
    REQUIRE(t);
    CHECK(share == doctest::Approx(0.5));
    CHECK(t->samples == 250);
    CHECK(t->airlines.at("AAL") == doctest::Approx(0.5 * 0.8 + 0.5 * 0.5));
    CHECK(t->airlines.at("DAL") == doctest::Approx(0.1));
    CHECK(t->airlines.at("ENY") == doctest::Approx(0.5 * 0.375));
    // adsb.lol's own types win where it has them.
    CHECK(t->types.at("AAL").count("A321") == 1);
    CHECK(t->types.at("AAL").size() == 1);
}

TEST_CASE("a well-sampled table or a missing entry leaves the table unchanged") {
    const Fleets    f = sampleFleets();
    const WikiEntry w = sampleWiki();
    AirportTable a;
    a.samples  = WikiFallback::kFullTrustSamples;
    a.airlines = {{"DAL", 1.0f}};
    float share = 1;
    auto t = WikiFallback::combine(a, &w, &f, &share);
    REQUIRE(t);
    CHECK(share == 0.0f);
    CHECK(t->airlines.size() == 1);
    a.samples = 60;
    t = WikiFallback::combine(a, nullptr, &f, &share);
    REQUIRE(t);
    CHECK(t->airlines.size() == 1);
    CHECK_FALSE(WikiFallback::combine(std::nullopt, nullptr, &f));
}

TEST_CASE("airlines without any type list take the types the CSL library has their livery on") {
    AirportTable t;
    t.airlines = {{"XYZ", 0.5f}, {"AAL", 0.5f}};
    t.types["AAL"] = {{"B738", 1.0f}};
    CslDb db;
    CslVariant v;
    v.airlineCode = "XYZ";
    db["A320"].push_back(v);
    db["DH8D"].push_back(v);
    CslVariant other;
    other.airlineCode = "QQQ";
    db["B738"].push_back(other);
    CHECK(ModelSelector::fillTypesFromLiveries(t, db) == 1);
    CHECK(t.types.at("XYZ").size() == 2);
    CHECK(t.types.at("AAL").size() == 1);
}

} // TEST_SUITE
