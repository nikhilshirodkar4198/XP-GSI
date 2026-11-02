// Fixes from the KLAX / VOBL / KATL test flights (2026-10-26): same-gate
// alternatives, cargo paint, specials per airline, stand overrides, partner
// brand by gate, pulling-in detection, broken scenery packs.
#include <doctest/doctest.h>

#include "csl/CslScanner.h"
#include "csl/IcaoSize.h"
#include "data/WikiFallback.h"
#include "csl/ModelSelector.h"
#include "data/AirportData.h"
#include "data/RegionalBrands.h"
#include "data/StandOverrides.h"
#include "engine/Density.h"
#include "engine/GateOccupancy.h"
#include "engine/Planner.h"
#include "engine/StandEvidence.h"
#include "engine/StandLayout.h"
#include "engine/TerminalZones.h"
#include "scenery/AptDat.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace {

CslVariant liv(const std::string& obj, const std::string& airline, const std::string& tag = {}) {
    CslVariant v;
    v.objPaths    = {obj};
    v.airlineCode = airline;
    v.livery      = tag;
    return v;
}

Stand standAt(double lat, double lon, float hdg = 0.0f, char size = 'C') {
    Stand s;
    s.lat = lat; s.lon = lon; s.headingDeg = hdg; s.widthCode = size;
    return s;
}

} // namespace

TEST_SUITE("LiveTestFixes") {

TEST_CASE("lettered positions share the plain-numbered gate only when it exists") {
    const auto k = StandLayout::gateKeys({"201", "201A", "201B", "52A", "52B", "305L", "Gate 40", "Gate 40B"});
    CHECK(k[1] == "201");
    CHECK(k[2] == "201");
    CHECK(k[3] == "52A");             // no plain "52": separate gates
    CHECK(k[4] == "52B");
    CHECK(k[5] == "305");
    CHECK(k[7] == "GATE 40");
}

TEST_CASE("stand points closer than 25 m are one gate whatever their names") {
    StandLayout::Checker c;
    const auto fp = StandLayout::place(StandLayout::stubShape(), 0, 0, 0);
    c.add(fp, "2", 0, 0);
    const auto far = StandLayout::place(StandLayout::stubShape(), 40, 0, 0);
    CHECK(c.check(StandLayout::place(StandLayout::stubShape(), 17, 0, 0), "3", 17, 0) ==
          StandLayout::Checker::Verdict::SameGate);
    CHECK(c.check(far, "4", 40, 0) == StandLayout::Checker::Verdict::Fits);
}

TEST_CASE("cargo paint is flagged from tags, file names and cargo airlines, and never placed") {
    const auto file = fs::temp_directory_path() / "xpgsi_cargo_test.json";
    std::ofstream(file) << R"({"schema":2, "cargo_airlines":["FDX"], "cargo_livery_tags":["CRG","BCF","F"],
                               "cargo_name_parts":["crg","cargo"], "entries":{}})";
    CslDb db;
    db["B738"].push_back(liv("/x/B738fa_ASACRG.obj", "ASA", "CRG"));     // Alaska Air Cargo
    db["B738"].push_back(liv("/x/B738Wfa_ETHWLCRG.obj", "ETH", "WLCRG"));
    db["B738"].push_back(liv("/x/B738_FDX.obj", "FDX"));
    db["B738"].push_back(liv("/x/B744_cargo_paint.obj", "CAL"));
    db["B738"].push_back(liv("/x/B738_ASA.obj", "ASA"));
    CslScanner::applyLiveryOverrides(db, file);
    const auto& e = db.at("B738");
    CHECK(e[0].cargo);
    CHECK(e[1].cargo);
    CHECK(e[2].cargo);
    CHECK(e[3].cargo);
    CHECK_FALSE(e[4].cargo);
    AirportTable t;
    t.airlines = {{"ASA", 1.0f}};
    t.types["ASA"]["B738"] = 1.0f;
    Stand s = standAt(47.44, -122.30);
    for (std::uint32_t seed = 1; seed < 100; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u).model->objPaths[0] == "/x/B738_ASA.obj");
    fs::remove(file);
}

TEST_CASE("at most one special per airline per airport") {
    CslDb db;
    CslVariant a = liv("/a.obj", "FFT"); a.special = true;
    CslVariant b = liv("/b.obj", "FFT"); b.special = true;
    db["A21N"] = {a, b};
    db["A21N"].push_back(liv("/std.obj", "FFT"));
    AirportTable t;
    t.airlines = {{"FFT", 1.0f}};
    t.types["FFT"]["A21N"] = 1.0f;
    ModelSelector::VariantSet used;
    int specials = 0;
    for (std::uint32_t i = 0; i < 400; ++i) {
        Stand s = standAt(39.85, -104.67 + i * 1e-4);
        auto c = ModelSelector::selectWithTable(s, db, t, i * 2654435761u + 7, &used);
        if (c.model->special) { ++specials; used.insert(c.model); }
    }
    CHECK(specials <= 1);
}

TEST_CASE("stand overrides: regex, airlines, brand and types") {
    const auto file = fs::temp_directory_path() / "xpgsi_stand_overrides_test.json";
    std::ofstream(file) << R"({"schema":1,"airports":{"KLAX":[{"stands":"^52[A-Z]$",
        "airlines":["SKW"],"brand":"AAL","types":["CRJ2","E175"]}]}})";
    std::vector<Stand> stands(3);
    stands[0].id = "52A"; stands[1].id = "52I"; stands[2].id = "80";
    CHECK(StandOverrides::apply(file, "KLAX", stands) == 2);
    CHECK(stands[0].brand == "AAL");
    CHECK(stands[1].allowedTypes.size() == 2);
    CHECK(stands[2].brand.empty());
    CHECK(StandOverrides::apply(file, "KSFO", stands) == 0);
    fs::remove(file);
}

TEST_CASE("a regional at an overridden stand flies the stand's brand, on its types only") {
    CslDb db;
    CslVariant ua = liv("/crj2_ua.obj", "SKW"); ua.brand = "UAL";
    CslVariant aa = liv("/crj2_aa.obj", "SKW"); aa.brand = "AAL";
    db["CRJ2"] = {ua, aa};
    db["A321"].push_back(liv("/a321_aa.obj", "AAL"));
    AirportTable t;
    t.airlines = {{"SKW", 0.5f}, {"AAL", 0.5f}};
    t.types["SKW"]["CRJ2"] = 1.0f;
    t.types["AAL"]["A321"] = 1.0f;
    RegionalBrands b;
    b.addOperator("SKW", {"UAL", "DAL", "AAL"});
    ModelSelector::Extras ex;
    ex.brands = &b;
    ex.brandPresence = {{"UAL", 10.0f}, {"AAL", 0.1f}};   // airport-wide United dominates
    Stand s = standAt(33.9413, -118.3937);
    s.airlines = {"SKW"}; s.brand = "AAL"; s.allowedTypes = {"CRJ2", "E175"};
    for (std::uint32_t seed = 1; seed < 100; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &ex).model->objPaths[0] ==
              "/crj2_aa.obj");
    // Without an override, the gate's 1301 airline decides the partner.
    Stand t8 = standAt(33.9420, -118.3977);
    t8.airlines = {"SKW", "UAL"};
    std::set<std::string> seen;
    for (std::uint32_t seed = 1; seed < 100; ++seed) {
        auto c = ModelSelector::selectWithTable(t8, db, t, seed * 2654435761u, nullptr, &ex);
        if (c.airline == "SKW") seen.insert(c.model->objPaths[0].generic_string());
    }
    CHECK(seen == std::set<std::string>{"/crj2_ua.obj"});
}

TEST_CASE("pulling in is caught on the stand centreline, a taxi-by is not") {
    const Stand s = standAt(33.0, -84.0, 90.0f);    // nose points east
    GateOccupancy::Traffic t;
    t.onGround = true; t.speedMs = 3.0f; t.headingDeg = 92.0f;
    auto at = [&](double behindM, double sideM) {    // behind = west, side = north
        t.lat = 33.0 + sideM / 110574.0;
        t.lon = -84.0 - behindM / (111320.0 * std::cos(33.0 * 3.14159265 / 180.0));
    };
    at(60.0, 2.0);                                   // 60 m back on the centreline: beyond the old box
    CHECK_FALSE(GateOccupancy::occupies(s, t));
    CHECK(GateOccupancy::approaching(s, t));
    at(60.0, 20.0);                                  // turning into the next gate
    CHECK_FALSE(GateOccupancy::approaching(s, t));
    at(60.0, 2.0); t.headingDeg = 0.0f;              // on the taxilane behind, crossing
    CHECK_FALSE(GateOccupancy::approaching(s, t));
    t.headingDeg = 90.0f; t.speedMs = 9.0f;          // fast: taxiing, not parking
    CHECK_FALSE(GateOccupancy::approaching(s, t));
    // The tracker claims an approaching aircraft after 1 s.
    GateOccupancy::Tracker tr;
    t.speedMs = 3.0f;
    std::vector<Stand> stands{s};
    CHECK(tr.update(stands, {t}, 0.0).empty());
    CHECK(tr.update(stands, {t}, 1.1).size() == 1);
}

TEST_CASE("runway length: no jet on a 1,368 m runway (Kolhapur)") {
    CHECK(IcaoSize::minRunwayM("AT76") <= 1368);
    CHECK(IcaoSize::minRunwayM("A21N") > 1368);
    CHECK(IcaoSize::minRunwayM("A20N") > 1368);
    CHECK(IcaoSize::minRunwayM("BCS3") <= 1508);    // London City
    CHECK(IcaoSize::minRunwayM("B738") <= 1737);    // John Wayne
    CslDb db;
    db["A21N"].push_back(liv("/a21n_igo.obj", "IGO"));
    db["AT76"].push_back(liv("/at76_igo.obj", "IGO"));
    AirportTable t;
    t.airlines = {{"IGO", 1.0f}};
    t.types["IGO"] = {{"A21N", 0.9f}, {"AT76", 0.1f}};
    ModelSelector::Extras ex;
    ex.longestRunwayM = 1368;
    Stand s = standAt(16.66, 74.29);
    for (std::uint32_t seed = 1; seed < 100; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &ex).type == "AT76");
}

TEST_CASE("heavies at heavy stands: type weight by size match") {
    CHECK(ModelSelector::sizeMatchWeight('E', 'E') == doctest::Approx(1.0f));
    CHECK(ModelSelector::sizeMatchWeight('C', 'E') == doctest::Approx(0.15f));
    CslDb db;
    db["A321"].push_back(liv("/a321_etd.obj", "ETD"));
    db["B789"].push_back(liv("/b789_etd.obj", "ETD"));
    AirportTable t;
    t.airlines = {{"ETD", 1.0f}};
    t.types["ETD"] = {{"A321", 0.5f}, {"B789", 0.5f}};
    int heavy = 0;
    for (std::uint32_t seed = 1; seed <= 400; ++seed)
        heavy += ModelSelector::selectWithTable(standAt(24.44, 54.65, 0, 'E'), db, t, seed * 2654435761u).type == "B789";
    MESSAGE("widebody share at an E stand: " << heavy / 400.0);
    CHECK(heavy > 300);                                  // ~87 %, was ~50 %
}

TEST_CASE("long-haul fleet mix by distance from the airline's home base") {
    Fleets f;
    f.home["KLM"] = {52.31, 4.76};                                     // EHAM
    f.types["KLM"]["all"] = {{"B738", 1.0f}};
    f.types["KLM"]["d0"]  = {{"B738", 0.6f}, {"E190", 0.4f}};
    f.types["KLM"]["d2"]  = {{"B77W", 0.5f}, {"B789", 0.5f}};
    const auto* delhi = WikiFallback::fleetMixFor(f, "KLM", "large_airport", 28.56, 77.10);
    REQUIRE(delhi);
    CHECK(delhi->count("B77W") == 1);
    const auto* paris = WikiFallback::fleetMixFor(f, "KLM", "large_airport", 49.01, 2.55);
    REQUIRE(paris);
    CHECK(paris->count("E190") == 1);
    const auto* home = WikiFallback::fleetMixFor(f, "KLM", "large_airport", 52.31, 4.76);
    REQUIRE(home);
    CHECK(home->count("B738") == 1);                                   // the whole fleet at home
    CHECK(WikiFallback::fleetMixFor(f, "XXX", "large_airport", 0, 0) == nullptr);
}

TEST_CASE("widebodies on short routes near the hub (Air China at Pudong, Vietnam Airlines at Ho Chi Minh City)") {
    Fleets f;
    f.types["CCA"]["fleet"] = {{"A321", 0.6f}, {"A333", 0.2f}, {"B77W", 0.2f}};
    f.types["CCA"]["widebody_share"] = {{"ZSPD", 0.35f}};
    Fleets::Mix m = {{"A321", 0.7f}, {"B738", 0.3f}};          // the d0 mix: narrowbodies only
    WikiFallback::applyWidebodyShare(m, f, "CCA", "ZSPD");
    float wide = m["A333"] + m["B77W"];
    CHECK(wide == doctest::Approx(0.35f));
    CHECK(m["A321"] == doctest::Approx(0.65f * 0.7f));
    Fleets::Mix other = {{"A321", 1.0f}};
    WikiFallback::applyWidebodyShare(other, f, "CCA", "ZBAA");   // not listed: unchanged
    CHECK(other.size() == 1);
}

TEST_CASE("a retired type is never chosen or drawn for that airline") {
    CslDb db;
    db["A346"].push_back(liv("/a346_dlh.obj", "DLH"));
    db["A343"].push_back(liv("/a343_x.obj", ""));           // generic A343 only
    db["A388"].push_back(liv("/a388_dlh.obj", "DLH"));
    db["A346"].push_back(liv("/a346_vir.obj", "VIR"));      // other airlines keep theirs
    const std::map<std::string, std::set<std::string>> retired{{"DLH", {"A346"}}};
    CHECK(ModelSelector::retireTypes(db, retired) == 1);
    CHECK(db.at("A346")[0].retired);
    CHECK_FALSE(db.at("A346")[1].retired);
    AirportTable t;
    t.airlines = {{"DLH", 1.0f}};
    t.types["DLH"] = {{"A346", 0.7f}, {"A388", 0.25f}, {"A343", 0.05f}};   // KIAD-like
    ModelSelector::Extras ex;
    ex.retiredTypes = retired;
    Stand s = standAt(38.95, -77.45, 0.0f, 'F');
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        auto c = ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &ex);
        REQUIRE(c.model);
        CHECK(c.type != "A346");
        CHECK(c.model->objPaths[0] != "/a346_dlh.obj");
    }
}

TEST_CASE("state (@) liveries are retired; cargo stands take only cargo paint") {
    const auto file = fs::temp_directory_path() / "xpgsi_state_test.json";
    std::ofstream(file) << R"({"schema":2,"state_code_prefixes":["@"],"cargo_airlines":["FDX"],
                               "cargo_livery_tags":["CRG"],"cargo_name_parts":["crg"],"entries":{}})";
    CslDb db;
    db["B738"].push_back(liv("/x/B738_IDAF.obj", "@ID"));            // Indonesian Air Force
    db["B738"].push_back(liv("/x/B738_UAE.obj", "UAE"));
    db["B77L"].push_back(liv("/x/B77L_ETDCRG.obj", "ETD", "CRG"));    // Etihad Cargo 777F
    db["B77L"].push_back(liv("/x/B77L_ETD.obj", "ETD"));              // passenger paint
    db["MD11"].push_back(liv("/x/MD11_FDX.obj", "FDX"));
    CslScanner::applyLiveryOverrides(db, file);
    CHECK(db.at("B738")[0].retired);
    AirportTable t;
    t.cargoAirlines = {{"ETD", 0.8f}, {"FDX", 0.2f}, {"XXX", 0.5f}};  // XXX: no cargo paint, skipped
    t.cargoTypes["ETD"]["B77L"] = 1.0f;
    t.cargoTypes["FDX"]["MD11"] = 1.0f;
    Stand s = standAt(24.45, 54.62, 0.0f, 'E');
    s.op = OpType::Cargo;
    std::set<std::string> seen;
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        auto c = ModelSelector::selectCargo(s, db, t, seed * 2654435761u);
        REQUIRE(c.model);
        CHECK(c.model->cargo);
        seen.insert(c.model->objPaths[0].generic_string());
    }
    CHECK(seen == std::set<std::string>{"/x/B77L_ETDCRG.obj", "/x/MD11_FDX.obj"});
    fs::remove(file);
}

TEST_CASE("passenger stands on cargo / private-jet spots are recognised") {
    AirportTable t;
    t.nonPaxCells = {{24.4400, 54.6400}};
    CHECK(AirportData::nearNonPax(t, 24.4402, 54.6402));     // ~30 m away
    CHECK_FALSE(AirportData::nearNonPax(t, 24.4420, 54.6400));  // ~220 m away
}

TEST_CASE("tables carry OpenStreetMap stands for sparse sceneries") {
    const auto file = fs::temp_directory_path() / "xpgsi_osm_stands_test.json";
    std::ofstream(file) << R"({"schema":1,"icao":"VABB","samples":900,"airlines":{"IGO":1},
        "osm_stands":[[19.0920814,72.8587343,294.6,"C","Bay 29",0],[19.091047,72.8593089,300.8,"E","Bay 40",1],
                      ["bad"]]})";
    auto t = AirportData::load(file);
    REQUIRE(t);
    REQUIRE(t->osmStands.size() == 2);
    CHECK(t->osmStands[0].ref == "Bay 29");
    CHECK(t->osmStands[0].headingDeg == doctest::Approx(294.6f));
    CHECK(t->osmStands[1].size == 'E');
    CHECK(t->osmStands[1].remote);
    fs::remove(file);
}

TEST_CASE("packs X-Plane failed to load are read from Log.txt") {
    const std::string log =
        "0:28:40.469 E/SCN: DSF canceled due to missing art assets, file=Custom Scenery/Nimbus Simulation - KATL "
        "V3.5 - Atlanta XP12/Earth nav data/+30-090/+33-085.dsf, err=15 (dsf_ErrCanceled).\n"
        "0:28:40.469 E/SCN: WARNING: got error on DSF load for scenery file 33, -85, Custom Scenery/Other Pack/\n"
        "0:28:40.469 I/SCN: DSF load time: 4895281 for file Custom Scenery/Fine Pack/Earth nav data/x.dsf\n";
    const auto b = AptDat::brokenPacks(log);
    CHECK(b.count("Nimbus Simulation - KATL V3.5 - Atlanta XP12") == 1);
    CHECK(b.count("Other Pack") == 1);
    CHECK(b.count("Fine Pack") == 0);
}

// ── KLAS / VIDP / KSDF / KMSP test flights (2026-10-27) ──────────────────────

TEST_CASE("a scenery's airline list loses to what the data sees parked there (KLAS)") {
    CslDb db;
    db["B738"].push_back(liv("/b738_swa.obj", "SWA"));
    db["B738"].push_back(liv("/b738_ual.obj", "UAL"));
    AirportTable t;
    t.airlines = {{"SWA", 0.5f}, {"UAL", 0.1f}};
    t.types["SWA"] = {{"B738", 1.0f}};
    t.types["UAL"] = {{"B738", 1.0f}};
    ParkCluster c;   // Southwest's concourse
    c.lat = 36.08; c.lon = -115.15; c.n = 100;
    c.airlines = {{"SWA", 95.0f}, {"UAL", 5.0f}};
    c.types["SWA"]["B738"] = 95.0f;
    c.types["UAL"]["B738"] = 5.0f;
    t.clusters.push_back(c);
    Stand s = standAt(36.08, -115.15, 0, 'C');
    s.airlines = {"UAL", "DAL", "AAL"};   // Global Airports KLAS
    CHECK(ModelSelector::listedShare(s.airlines, c.airlines) == doctest::Approx(0.05f));
    int swa = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed)
        swa += ModelSelector::selectWithTable(s, db, t, seed * 2654435761u).airline == "SWA";
    CHECK(swa > 150);   // was 0: the list always won
    // A list that matches what parks there still decides.
    s.airlines = {"SWA"};
    for (std::uint32_t seed = 1; seed <= 50; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u).airline == "SWA");
    // A stand override's list always stands.
    s.airlines = {"UAL"};
    s.airlinesOverridden = true;
    for (std::uint32_t seed = 1; seed <= 50; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u).airline == "UAL");
}

TEST_CASE("one airline list on most stands is ignored (X-Plane 12 VIDP \"AIC IGO\" on 142 of 163)") {
    std::vector<Stand> st;
    for (int i = 0; i < 20; ++i) {
        Stand s = standAt(28.55 + i * 1e-4, 77.10, 0, 'C');
        s.airlines = i < 16 ? std::vector<std::string>{"IGO", "AIC"} : std::vector<std::string>{"UAE"};
        st.push_back(s);
    }
    st[0].airlines = {"AIC", "IGO"};       // same list, other order
    st[1].airlinesOverridden = true;       // an override stays
    CHECK(ModelSelector::clearBlanketLists(st) == 15);
    CHECK(st[0].airlines.empty());
    CHECK(st[1].airlines.size() == 2);
    CHECK(st[17].airlines == std::vector<std::string>{"UAE"});   // a gate-specific list stays
    // Small airports: a list on every one of 4 stands is kept.
    std::vector<Stand> small(4, standAt(10, 10, 0, 'C'));
    for (auto& s : small) s.airlines = {"IGO"};
    CHECK(ModelSelector::clearBlanketLists(small) == 0);
}

TEST_CASE("widebody stands: an airline counts by its widebodies, not all its flights (KDFW)") {
    CslDb db;
    db["B738"].push_back(liv("/b738_aal.obj", "AAL"));
    db["B77W"].push_back(liv("/b77w_aal.obj", "AAL"));
    db["B789"].push_back(liv("/b789_kal.obj", "KAL"));
    AirportTable t;
    t.airlines = {{"AAL", 0.9f}, {"KAL", 0.02f}};
    t.types["AAL"] = {{"B738", 0.9f}, {"B77W", 0.1f}};
    t.types["KAL"] = {{"B789", 1.0f}};
    int kal = 0;
    for (std::uint32_t seed = 1; seed <= 2000; ++seed)
        kal += ModelSelector::selectWithTable(standAt(32.9, -97.04, 0, 'E'), db, t, seed * 2654435761u).airline == "KAL";
    MESSAGE("Korean share at an E stand: " << kal / 2000.0);
    CHECK(kal > 120);    // ~9 %, was ~2 % (its share of all flights)
    CHECK(kal < 300);    // American still has most
    // Narrowbody stands are unchanged: Korean's 787 does not fit.
    for (std::uint32_t seed = 1; seed <= 50; ++seed)
        CHECK(ModelSelector::selectWithTable(standAt(32.9, -97.04, 0, 'C'), db, t, seed * 2654435761u).airline == "AAL");
}

TEST_CASE("long-haul visitors get widebody stands of their own (KDFW: about two at a time)") {
    AirportTable t;
    t.airlines = {{"AAL", 0.85f}, {"SWA", 0.10f}, {"KAL", 0.02f}, {"JAL", 0.02f}, {"ACA", 0.01f}};
    t.types["AAL"] = {{"B738", 0.9f}, {"B77W", 0.1f}};
    t.types["SWA"] = {{"B737", 1.0f}};
    t.types["KAL"] = {{"B789", 0.6f}, {"B748", 0.4f}};
    t.types["JAL"] = {{"A35K", 1.0f}};
    t.types["ACA"] = {{"BCS3", 0.6f}, {"B763", 0.4f}};   // mostly narrowbody: no visitor
    const auto v = ModelSelector::longHaulVisitors(t);
    CHECK(v.size() == 2);
    CHECK(v.count("KAL") == 1);
    CHECK(v.count("JAL") == 1);
    CHECK(ModelSelector::visitorStands(v, 850.0f) == 4);    // 4 %: 34 a day × 4 h / 24 ≈ 5.7, capped at 4
    CHECK(ModelSelector::visitorStands(v, 20.0f) == 0);     // 0.8 a day: none kept
    CHECK(ModelSelector::visitorStands(v, 40.0f) == 1);     // 1.6 a day: one
    CHECK(ModelSelector::visitorStands(v, 50000.0f) == ModelSelector::kMaxVisitorStands);
    // A stand kept for them takes a visitor even where the scenery lists American.
    CslDb db;
    db["B738"].push_back(liv("/b738_aal.obj", "AAL"));
    db["B77W"].push_back(liv("/b77w_aal.obj", "AAL"));
    db["B789"].push_back(liv("/b789_kal.obj", "KAL"));
    ModelSelector::Extras ex;
    ex.visitors     = &v;
    ex.visitorsOnly = true;
    Stand s = standAt(32.9, -97.04, 0, 'E');
    s.airlines = {"AAL"};
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &ex).airline == "KAL");   // JAL: no paint
    // Nothing drawable among them: the stand is filled as usual.
    CslDb noVisitor;
    noVisitor["B77W"].push_back(liv("/b77w_aal.obj", "AAL"));
    CHECK(ModelSelector::selectWithTable(s, noVisitor, t, 7, nullptr, &ex).airline == "AAL");
}

TEST_CASE("terminal assignments keep airlines at their terminals where there is no parking data (ZSPD)") {
    const fs::path file = fs::temp_directory_path() / "xpgsi_terminal_assignments.json";
    {
        std::ofstream f(file, std::ios::binary);
        f << R"({"schema":1,"airports":{"ZSPD":{
               "zones":[{"name":"T1","group":"T1"},{"name":"T2","group":"T2"}],
               "anchors":[[31.150,121.80,0],[31.151,121.80,0],[31.150,121.81,1],[31.151,121.81,1]],
               "airlines":[["CES","AFR"],["CCA","UAL"]]}}})";
    }
    CslDb db;
    for (const char* a : {"CES", "AFR", "CCA", "UAL"}) db["A320"].push_back(liv(std::string("/a320_") + a + ".obj", a));
    AirportTable t;   // Wikipedia-only
    t.airlines = {{"CES", 0.4f}, {"CCA", 0.3f}, {"AFR", 0.1f}, {"UAL", 0.2f}};
    for (const char* a : {"CES", "AFR", "CCA", "UAL"}) t.types[a] = {{"A320", 1.0f}};
    CHECK(AirportData::applyTerminalAssignments(t, file, "ZSPD") == 2);
    CHECK(t.clusters.size() == 4);
    const auto zones = TerminalZones::Zones::build(t, {});
    ModelSelector::Extras ex;
    ex.zones = &zones;
    std::map<std::string, int> t1, t2;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        ++t1[ModelSelector::selectWithTable(standAt(31.1505, 121.80, 0, 'C'), db, t, seed * 2654435761u, nullptr, &ex).airline];
        ++t2[ModelSelector::selectWithTable(standAt(31.1505, 121.81, 0, 'C'), db, t, seed * 2654435761u, nullptr, &ex).airline];
    }
    CHECK(t1["CCA"] + t1["UAL"] == 0);
    CHECK(t2["CES"] + t2["AFR"] == 0);
    CHECK(t1["CES"] > t1["AFR"]);   // table weights inside the terminal
    // An airport with parking data keeps its own; one without an entry is unchanged.
    AirportTable withData = t;
    withData.zones.clear();
    CHECK(AirportData::applyTerminalAssignments(withData, file, "ZSPD") == 0);
    AirportTable other;
    CHECK(AirportData::applyTerminalAssignments(other, file, "ZBAA") == 0);
    fs::remove(file);
}

TEST_CASE("airport partner weights decide a regional's brand (SkyWest at KSLC is Delta Connection)") {
    CslDb db;
    CslVariant dl = liv("/e75_skw_dl.obj", "SKW");  dl.brand = "DAL";
    CslVariant ua = liv("/e75_skw_ua.obj", "SKW");  ua.brand = "UAL";
    db["E75L"].push_back(dl);
    db["E75L"].push_back(ua);
    AirportTable t;
    t.airlines = {{"SKW", 0.5f}, {"DAL", 0.25f}, {"UAL", 0.25f}};
    t.types["SKW"] = {{"E75L", 1.0f}};
    ParkCluster c;   // United and Delta mainline parked around the stand, half each
    c.lat = 40.79; c.lon = -111.98; c.n = 60;
    c.airlines = {{"SKW", 30.0f}, {"DAL", 15.0f}, {"UAL", 15.0f}};
    c.types["SKW"]["E75L"] = 30.0f;
    t.clusters.push_back(c);
    RegionalBrands rb;
    rb.addOperator("SKW", {"UAL", "DAL", "AAL", "ASA"});
    ModelSelector::Extras ex;
    ex.brands = &rb;
    ex.brandPresence = rb.presence(nullptr, t);
    int dal = 0, skw = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        auto ch = ModelSelector::selectWithTable(standAt(40.79, -111.98, 0, 'C'), db, t, seed * 2654435761u, nullptr, &ex);
        if (ch.airline == "SKW") { ++skw; dal += ch.model && ch.model->brand == "DAL"; }
    }
    REQUIRE(skw > 20);
    CHECK(dal < skw * 0.8);                                   // the parked mix: about half
    ex.airportPartners["SKW"] = {{"DAL", 0.88f}, {"UAL", 0.07f}, {"AAL", 0.05f}};
    dal = skw = 0;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        auto ch = ModelSelector::selectWithTable(standAt(40.79, -111.98, 0, 'C'), db, t, seed * 2654435761u, nullptr, &ex);
        if (ch.airline == "SKW") { ++skw; dal += ch.model && ch.model->brand == "DAL"; }
    }
    CHECK(dal > skw * 0.8);                                   // the airport's weights
}

TEST_CASE("a stand override can match by position, whatever the scenery names the stands") {
    const fs::path file = fs::temp_directory_path() / "xpgsi_overrides_near.json";
    {
        std::ofstream f(file, std::ios::binary);
        f << R"({"schema":1,"airports":{"KLAX":[{"near":[33.9422,-118.3934,120],"brand":"AAL","types":["E75L"]}]}})";
    }
    std::vector<Stand> st = {standAt(33.9425, -118.3936), standAt(33.9440, -118.3990)};   // ~30 m and ~550 m away
    st[0].id = "R12"; st[1].id = "52A";
    CHECK(StandOverrides::apply(file, "KLAX", st) == 1);
    CHECK(st[0].brand == "AAL");
    CHECK(st[1].brand.empty());
    fs::remove(file);
}

TEST_CASE("a kept visitor stand never takes a visitor where the data says only others park (KORD T1)") {
    CslDb db;
    db["B77W"].push_back(liv("/b77w_ual.obj", "UAL"));
    db["A359"].push_back(liv("/a359_cpa.obj", "CPA"));
    AirportTable t;
    t.airlines = {{"UAL", 0.9f}, {"CPA", 0.02f}};
    t.types["UAL"] = {{"B77W", 1.0f}};
    t.types["CPA"] = {{"A359", 1.0f}};
    ParkCluster ual;   // United's terminal: only United parks here
    ual.lat = 41.978; ual.lon = -87.905; ual.n = 50;
    ual.airlines = {{"UAL", 50.0f}};
    ual.types["UAL"]["B77W"] = 50.0f;
    t.clusters.push_back(ual);
    const auto v = ModelSelector::longHaulVisitors(t);
    REQUIRE(v.count("CPA") == 1);
    ModelSelector::Extras ex;
    ex.visitors = &v;
    ex.visitorsOnly = true;
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
        CHECK(ModelSelector::selectWithTable(standAt(41.978, -87.905, 0, 'E'), db, t, seed * 2654435761u, nullptr, &ex)
                  .airline == "UAL");
}

TEST_CASE("an airline at its daily cap is not chosen again while another fits (three Etihads at KORD T5)") {
    CslDb db;
    db["B789"].push_back(liv("/b789_etd.obj", "ETD"));
    db["B77W"].push_back(liv("/b77w_uae.obj", "UAE"));
    AirportTable t;
    t.airlines = {{"ETD", 0.5f}, {"UAE", 0.5f}};
    t.types["ETD"] = {{"B789", 1.0f}};
    t.types["UAE"] = {{"B77W", 1.0f}};
    std::set<std::string> full = {"ETD"};
    ModelSelector::Extras ex;
    ex.full = &full;
    for (std::uint32_t seed = 1; seed <= 40; ++seed)
        CHECK(ModelSelector::selectWithTable(standAt(41.98, -87.90, 0, 'E'), db, t, seed * 2654435761u, nullptr, &ex)
                  .airline == "UAE");
    full = {"ETD", "UAE"};   // every airline full: still a static, not an empty stand
    CHECK(ModelSelector::selectWithTable(standAt(41.98, -87.90, 0, 'E'), db, t, 7, nullptr, &ex).model != nullptr);
}

TEST_CASE("all airlines parked at a terminal are full: still them, never another terminal's (KSFO T1)") {
    CslDb db;
    db["B739"].push_back(liv("/b739_asa.obj", "ASA"));
    db["B738"].push_back(liv("/b738_swa.obj", "SWA"));
    AirportTable t;
    t.airlines = {{"ASA", 0.1f}, {"SWA", 0.9f}};
    t.types["ASA"] = {{"B739", 1.0f}};
    t.types["SWA"] = {{"B738", 1.0f}};
    ParkCluster t1;   // only Alaska parks here
    t1.lat = 37.613; t1.lon = -122.385; t1.n = 50;
    t1.airlines = {{"ASA", 50.0f}};
    t1.types["ASA"]["B739"] = 50.0f;
    t.clusters.push_back(t1);
    std::set<std::string> full = {"ASA"};
    ModelSelector::Extras ex;
    ex.full = &full;
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
        CHECK(ModelSelector::selectWithTable(standAt(37.613, -122.385, 0, 'C'), db, t, seed * 2654435761u, nullptr, &ex)
                  .airline == "ASA");
}

TEST_CASE("a stand with little data: a list naming no one seen there goes, the few seen win (KSFO maintenance ramp)") {
    CslDb db;
    db["B738"].push_back(liv("/b738_dal.obj", "DAL"));
    db["B739"].push_back(liv("/b739_ual.obj", "UAL"));
    AirportTable t;
    t.airlines = {{"DAL", 0.5f}, {"UAL", 0.5f}};
    t.types["DAL"] = {{"B738", 1.0f}};
    t.types["UAL"] = {{"B739", 1.0f}};
    ParkCluster ramp;   // two United stays, under kMinAreaEvidence
    ramp.lat = 37.634; ramp.lon = -122.394; ramp.n = 2;
    ramp.airlines = {{"UAL", 2.0f}};
    ramp.types["UAL"]["B739"] = 2.0f;
    t.clusters.push_back(ramp);
    Stand s = standAt(37.634, -122.394, 0, 'C');
    s.airlines = {"DAL"};
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u).airline == "UAL");
}

TEST_CASE("a name shared by several stands is an apron, not one gate (KSDF \"UPS Ramp 7\")") {
    const auto k = StandLayout::gateKeys({"UPS Ramp 7", "UPS Ramp 7", "ups ramp 7 ", "UPS Ramp 5", "A14", "A14A"});
    CHECK(k[0] != k[1]);
    CHECK(k[1] != k[2]);
    CHECK(k[3] == "UPS RAMP 5");
    CHECK(k[4] == k[5]);   // lettered alternative still shares its gate
}

TEST_CASE("a multi-position gate tries its largest position first (VIDP A07 / A07L / A07R)") {
    const std::vector<std::string> keys  = {"A07", "B1", "A07", "A07", "B2"};
    const std::vector<char>        sizes = {'E', 'C', 'C', 'C', 'C'};
    // Random fill order with a narrowbody side position first.
    const auto out = StandLayout::largestFirstPerGate({2, 1, 0, 4, 3}, keys, sizes);
    REQUIRE(out.size() == 5);
    CHECK(out[0] == 0);   // A07 (E) takes the gate's place
    CHECK(out[1] == 2);
    CHECK(out[2] == 3);
    CHECK(out[3] == 1);   // other gates keep their order
    CHECK(out[4] == 4);
}

// ── Stand audit (2026-10-27): stands corrected from where aircraft park ─────

namespace {
ParkCell cellAt(double lat, double lon, std::initializer_list<int> pax, int nonPax = 0) {
    ParkCell c;
    c.lat = lat; c.lon = lon;
    int k = 0;
    for (int v : pax) c.pax[k++] = v;
    c.nonPax = nonPax;
    return c;
}
Stand named(const std::string& id, double lat, double lon, char size, OpType op,
            StandType type = StandType::Gate) {
    Stand s = standAt(lat, lon, 0, size);
    s.id = id; s.op = op; s.type = type;
    return s;
}
} // namespace

TEST_CASE("a stand marked too small takes the size that really parks there (KMSP A5: B, CRJ-900s)") {
    std::vector<Stand> st = {named("A5", 44.88000, -93.22000, 'B', OpType::Airline),
                             named("A6", 44.88000, -93.21900, 'B', OpType::Airline)};   // ~79 m east
    std::vector<ParkCell> cells = {cellAt(44.88010, -93.22005, {2, 30}),   // A5: C most days
                                   cellAt(44.88005, -93.21905, {12})};     // A6: only B
    const auto r = StandEvidence::apply(st, cells);
    CHECK(r.upsized == 1);
    CHECK(st[0].widthCode == 'C');
    CHECK(st[1].widthCode == 'B');
    // A few odd visits are not enough.
    StandEvidence::Evidence e;
    e.pax[0] = 40; e.pax[3] = 3;
    CHECK(StandEvidence::sizeFor(e, 'B') == 'B');
    // Never smaller.
    StandEvidence::Evidence small;
    small.pax[0] = 50;
    CHECK(StandEvidence::sizeFor(small, 'E') == 'E');
}

TEST_CASE("a multi-position gate counts together: the widebody stand grows, not a side one (EGLL 213L)") {
    std::vector<Stand> st = {named("213",  51.47000, -0.45000, 'D', OpType::Airline),
                             named("213L", 51.47000, -0.45030, 'C', OpType::Airline),   // ~21 m west
                             named("213R", 51.47000, -0.44970, 'C', OpType::Airline)};
    std::vector<ParkCell> cells = {cellAt(51.47000, -0.45028, {0, 0, 0, 10, 6})};   // nearest the L stand
    StandEvidence::apply(st, cells);
    CHECK(st[0].widthCode == 'F');
    CHECK(st[1].widthCode == 'C');
    CHECK(st[2].widthCode == 'C');
}

TEST_CASE("cargo / GA / none stands where airliners park become airline stands (MMMY W01, VTBS 513, RJCC)") {
    std::vector<Stand> st = {named("W01", 25.77, -100.10, 'D', OpType::Cargo, StandType::TieDown),
                             named("513", 25.78, -100.10, 'C', OpType::GA, StandType::TieDown),
                             named("R1",  25.79, -100.10, 'E', OpType::None, StandType::TieDown),
                             named("C1",  25.80, -100.10, 'E', OpType::Cargo, StandType::TieDown),
                             named("M1",  25.81, -100.10, 'E', OpType::Military, StandType::TieDown),
                             named("H1",  25.82, -100.10, 'E', OpType::None, StandType::Hangar)};
    std::vector<ParkCell> cells = {cellAt(25.77, -100.10, {0, 20}, 2),
                                   cellAt(25.78, -100.10, {0, 9}),
                                   cellAt(25.79, -100.10, {0, 5, 0, 5}),
                                   cellAt(25.80, -100.10, {0, 10}, 20),   // a real cargo apron
                                   cellAt(25.81, -100.10, {0, 20}),
                                   cellAt(25.82, -100.10, {0, 20})};
    const auto r = StandEvidence::apply(st, cells);
    CHECK(r.toAirline == 3);
    CHECK(st[0].op == OpType::Airline);
    CHECK(st[1].op == OpType::Airline);
    CHECK(st[2].op == OpType::Airline);
    CHECK(st[3].op == OpType::Cargo);
    CHECK(st[4].op == OpType::Military);
    CHECK(st[5].op == OpType::None);
}

TEST_CASE("an overridden stand is left as it is") {
    std::vector<Stand> st = {named("52A", 33.94, -118.40, 'B', OpType::Airline)};
    st[0].brand = "AAL";
    StandEvidence::apply(st, {cellAt(33.94, -118.40, {0, 30})});
    CHECK(st[0].widthCode == 'B');
}

TEST_CASE("X-Plane 12: packs below *GLOBAL_AIRPORTS* lose to the Global Airports") {
    const fs::path root = fs::temp_directory_path() / "xpgsi_xp12_root";
    fs::remove_all(root);
    auto apt = [](const fs::path& p, const char* icao, const char* lat) {
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f << "I\n1200 Version\n\n1 100 0 0 " << icao << " Test\n1300 " << lat << " 10.0 90.0 gate jets G1\n99\n";
    };
    apt(root / "Custom Scenery" / "Above" / "Earth nav data" / "apt.dat", "AAAA", "1.0");
    apt(root / "Custom Scenery" / "Below" / "Earth nav data" / "apt.dat", "BBBB", "2.0");
    {   // Global Airports define both
        const fs::path g = root / "Global Scenery" / "Global Airports" / "Earth nav data" / "apt.dat";
        fs::create_directories(g.parent_path());
        std::ofstream f(g, std::ios::binary);
        f << "I\n1200 Version\n\n1 100 0 0 AAAA Global\n1300 1.5 10.0 90.0 gate jets G1\n"
             "1 100 0 0 BBBB Global\n1300 2.5 10.0 90.0 gate jets G1\n99\n";
    }
    {
        std::ofstream ini(root / "Custom Scenery" / "scenery_packs.ini", std::ios::binary);
        ini << "I\n1000 Version\nSCENERY\n\nSCENERY_PACK Custom Scenery/Above/\n"
               "SCENERY_PACK *GLOBAL_AIRPORTS*\nSCENERY_PACK Custom Scenery/Below/\n";
    }
    fs::create_directories(root / "cache");
    const auto idx = AptDat::buildIndex(root, 12000, root / "cache");
    REQUIRE(idx.count("AAAA") == 1);
    REQUIRE(idx.count("BBBB") == 1);
    CHECK(idx.at("AAAA").sourcePack == "Above");             // above the entry: the custom airport
    CHECK(idx.at("BBBB").sourcePack == "Global Airports");   // below it: X-Plane shows the default
    fs::remove_all(root);
}

TEST_CASE("a gate marked \"none\" is an airline gate (Nimbus KORD, FlyTampa EKCH)") {
    const fs::path file = fs::temp_directory_path() / "xpgsi_none_gate_apt.dat";
    {
        std::ofstream f(file, std::ios::binary);
        f << "I\n1100 Version\n\n"
             "1 668 0 0 KORD Chicago O'Hare\n"
             "1300 41.97 -87.90 90.0 gate jets B1\n"
             "1301 C none\n"
             "1300 41.98 -87.90 90.0 tie_down jets R1\n"
             "1301 E none\n"
             "99\n";
    }
    AptDat::IndexEntry e{file, 0, 41.97, -87.90, "fixture"};
    {
        std::ifstream in(file, std::ios::binary);
        std::string line;
        std::streamoff pos = 0;
        while (std::getline(in, line)) {
            if (line.rfind("1 ", 0) == 0) { e.offset = pos; break; }
            pos = in.tellg();
        }
    }
    const auto a = AptDat::parseAirport(e);
    REQUIRE(a.stands.size() == 2);
    CHECK(a.stands[0].op == OpType::Airline);
    CHECK(a.stands[1].op == OpType::None);   // a remote one only with evidence (StandEvidence)
    fs::remove(file);
}

// ── XP12 test flights, 2026-10-28: KPDX, RJBB, VOGA, KSMF, UTTT ──────────────

TEST_CASE("an airport with a placeholder ID is parsed under its 1302 icao_code (VOGA)") {
    const fs::path file = fs::temp_directory_path() / "xpgsi_icao_code_apt.dat";
    {
        std::ofstream f(file, std::ios::binary);
        f << "I\n1100 Version\n\n"
             "1    561 0 0 XVI000D Manohar Intl\n"
             "1302 datum_lat 15.742333475\n"
             "1302 iata_code GOX\n"
             "1302 icao_code VOGA\n"
             "1300 15.74 73.86 90.0 gate heavy|jets A1\n"
             "1301 E airline\n"
             "99\n";
    }
    std::ifstream in(file, std::ios::binary);
    std::string line;
    std::streamoff pos = 0, off = 0;
    while (std::getline(in, line)) {
        if (line.rfind("1 ", 0) == 0) { off = pos; break; }
        pos = in.tellg();
    }
    in.close();
    const auto a = AptDat::parseAirport({file, off, 15.74, 73.86, "fixture"});
    CHECK(a.icao == "VOGA");
    CHECK(a.aptId == "XVI000D");
    CHECK(a.name == "Manohar Intl");
    CHECK(a.stands.size() == 1);
    // The index finds it under VOGA too.
    const auto e = AptDat::findAirport(file, "fixture", "VOGA");
    REQUIRE(e.has_value());
    CHECK(e->offset == off);
    CHECK_FALSE(AptDat::findAirport(file, "fixture", "XVI000D").has_value());
    fs::remove(file);
}

TEST_CASE("a rare airline shows no more aircraft than are parked at once (RJBB Finnair)") {
    // Finnair: 2.5 % of RJBB's ground time, ~24 airline aircraft parked by day.
    CHECK(ModelSelector::rareAirlineCap(0.025f, 24.0f) == 1);
    CHECK(ModelSelector::rareAirlineCap(0.06f, 24.0f) == 2);     // 1.44 parked → 2
    CHECK(ModelSelector::rareAirlineCap(0.30f, 24.0f) == -1);    // Peach: no hard cap
    CHECK(ModelSelector::rareAirlineCap(0.025f, -1.0f) == -1);   // unknown traffic
    // The share cap counts the stands placed so far, not the whole plan.
    CHECK(ModelSelector::shareCap(0.025f, 0) == 1);
    CHECK(ModelSelector::shareCap(0.30f, 0) == 1);
    CHECK(ModelSelector::shareCap(0.30f, 2) == 2);
    CHECK(ModelSelector::shareCap(0.30f, 23) == 11);

    // A capped airline is never chosen again, also not on a stand kept for
    // visitors where it is the only visitor parked: the stand takes another airline.
    CslDb db;
    db["A359"].push_back(liv("/x/A359_FIN.obj", "FIN"));
    db["A359"].push_back(liv("/x/A359_JAL.obj", "JAL"));
    AirportTable t;
    t.airlines = {{"JAL", 0.9f}, {"FIN", 0.1f}};
    t.types["JAL"]["A359"] = 1.0f;
    t.types["FIN"]["A359"] = 1.0f;
    ParkCluster c;
    c.lat = 34.43; c.lon = 135.23; c.n = 10;
    c.airlines = {{"FIN", 10.0f}};
    c.types["FIN"]["A359"] = 10.0f;
    t.clusters.push_back(c);
    const std::map<std::string, float> visitors{{"FIN", 0.1f}};
    const std::set<std::string> capped{"FIN"};
    ModelSelector::Extras x;
    x.visitors     = &visitors;
    x.visitorsOnly = true;
    Stand s = standAt(34.43, 135.23, 0.0f, 'E');
    s.op = OpType::Airline;
    CHECK(ModelSelector::selectWithTable(s, db, t, 7, nullptr, &x).airline == "FIN");
    x.capped = &capped;
    for (std::uint32_t seed = 1; seed < 50; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &x).airline != "FIN");
}

TEST_CASE("without airport data only paint in use, on a type the airline flies today (VOGA)") {
    CslDb db;
    db["B744"].push_back(liv("/x/B744_KLM.obj", "KLM"));      // KLM retired its 747s
    db["IL96"].push_back(liv("/x/IL96_AFL.obj", "AFL"));
    db["A320"].push_back(liv("/x/A320_AFL.obj", "AFL"));
    db["A320"].push_back(liv("/x/A320_OLD.obj", "IGO"));
    db["A320"].back().era = CslVariant::Era::Outdated;
    const ModelSelector::FleetTypes fleets{{"AFL", {"A320", "B77W"}}, {"KLM", {"B772", "B77W"}}, {"IGO", {"A320"}}};
    for (std::uint32_t seed = 1; seed < 60; ++seed) {
        const auto* e = ModelSelector::selectForStand(standAt(15.7, 73.8, 0.0f, 'F'), db, seed * 2654435761u,
                                                      nullptr, &fleets);
        REQUIRE(e != nullptr);
        CHECK(e->objPaths[0] == "/x/A320_AFL.obj");
    }
    // Wikipedia-only airlines: types from their liveries, only those they fly today.
    AirportTable t;
    t.airlines = {{"AFL", 1.0f}};
    CHECK(ModelSelector::fillTypesFromLiveries(t, db, &fleets) == 1);
    CHECK(t.types.at("AFL").count("A320") == 1);
    CHECK(t.types.at("AFL").count("IL96") == 0);
}

TEST_CASE("stands airliners really use fill first (KSMF remote stands typed gate)") {
    std::vector<Stand> stands;
    std::vector<int>   stays;
    for (int i = 0; i < 20; ++i) {
        stands.push_back(standAt(38.69 + i * 0.0005, -121.59));
        stays.push_back(i < 10 ? 200 : 1);   // 10 busy gates, 10 almost unused positions
    }
    int busyFirst = 0, busyUniform = 0, runs = 0;
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        const auto order   = Planner::rankStands(stands, seed * 2654435761u, &stays);
        const auto uniform = Planner::rankStands(stands, seed * 2654435761u);
        for (std::size_t k = 0; k < 10; ++k) {
            busyFirst   += order[k] < 10;
            busyUniform += uniform[k] < 10;
        }
        runs += 10;
    }
    CHECK(static_cast<double>(busyFirst) / runs > 0.75);              // ~0.83 expected
    CHECK(static_cast<double>(busyUniform) / runs < 0.6);             // ~0.5 without
    // Poor ground coverage (most stands without any stays): no usage weighting.
    std::vector<int> sparse(20, 0);
    sparse[0] = 50;
    CHECK(Planner::rankStands(stands, 99, &sparse) == Planner::rankStands(stands, 99));
    CHECK(Planner::usageWeight(0, 200) == doctest::Approx(Planner::kMinUsageWeight));
    CHECK(Planner::usageWeight(400, 200) == doctest::Approx(1.0));
}

TEST_CASE("an airline stand where mostly cargo or private aircraft park gets no static (KSMF 53-55)") {
    std::vector<Stand> stands{standAt(38.69290, -121.59424), standAt(38.69319, -121.59390), standAt(38.69534, -121.59144)};
    stands[0].id = "53"; stands[1].id = "52"; stands[2].id = "B9";
    for (auto& s : stands) { s.op = OpType::Airline; s.type = StandType::Gate; }
    auto cell = [](double lat, double lon, int c, int nonPax) {
        ParkCell p;
        p.lat = lat; p.lon = lon; p.pax[1] = c; p.nonPax = nonPax;
        return p;
    };
    const std::vector<ParkCell> cells{cell(38.69290, -121.59424, 5, 14), cell(38.69319, -121.59390, 13, 2),
                                      cell(38.69534, -121.59144, 165, 1)};
    const auto r = StandEvidence::apply(stands, cells);
    CHECK(r.notAirline == 1);
    CHECK(stands[0].op == OpType::None);
    CHECK(stands[1].op == OpType::Airline);
    CHECK(stands[2].op == OpType::Airline);
    const auto stays = StandEvidence::paxStaysPerStand(stands, cells);
    CHECK(stays == std::vector<int>{5, 13, 165});
}

// ── KSFO by hand, 2026-10-28 ─────────────────────────────────────────────────

TEST_CASE("an area takes the stands nearest its anchors, whatever their names (older layouts)") {
    const auto file = fs::temp_directory_path() / "xpgsi_areas_test.json";
    std::ofstream(file) << R"({"schema":1,"airports":{"KTST":[
        {"anchors":[[37.6000,-122.4000],[37.6010,-122.4000]], "airlines":{"SWA":0.9,"UAL":0.1}},
        {"anchors":[[37.6030,-122.4000]], "airlines":["UAL"], "brand":"UAL"},
        {"anchors":[[37.6100,-122.4000]], "max_m":50, "op":"none"},
        {"stands":"^Gate 9$", "airlines":["DAL"]}]}})";
    std::vector<Stand> s{standAt(37.6004, -122.4000), standAt(37.6022, -122.4000), standAt(37.6100, -122.4003),
                         standAt(37.6200, -122.4000), standAt(37.6001, -122.4000)};
    s[0].id = "Gate 20"; s[1].id = "Gate 21"; s[2].id = "GA Ramp"; s[3].id = "Far"; s[4].id = "Gate 9";
    for (auto& st : s) st.op = OpType::Airline;
    CHECK(StandOverrides::apply(file, "KTST", s) == 4);
    CHECK(s[0].airlines == std::vector<std::string>{"SWA", "UAL"});   // nearest anchor: the first area
    CHECK(s[0].airlineWeights.at("SWA") == doctest::Approx(0.9f));
    CHECK(s[1].airlines == std::vector<std::string>{"UAL"});          // 89 m from area 2, 133 m from area 1
    CHECK(s[1].brand == "UAL");
    CHECK(s[2].op == OpType::None);
    CHECK_FALSE(s[3].airlinesOverridden);                             // beyond every max_m
    CHECK(s[4].airlines == std::vector<std::string>{"DAL"});          // a single rule refines the area
    fs::remove(file);
}

TEST_CASE("KSFO's hand-made areas fit both X-Plane 11's old and X-Plane 12's layout") {
    const fs::path data = fs::path(FIXTURES_DIR).parent_path().parent_path() / "data" / "stand_overrides.json";
    std::vector<Stand> s{
        standAt(37.61236, -122.38400),   // XP11 "Gate 31", old Terminal 1 → Boarding Area B north
        standAt(37.61264, -122.38403),   // XP12 "Gate B9"
        standAt(37.61689, -122.38163),   // "Gate 51A", Terminal 2 south
        standAt(37.61509, -122.38208),   // "Gate 45", Boarding Area C
        standAt(37.62063, -122.38657),   // "Gate 77", Terminal 3 regional gates
        standAt(37.61715, -122.39079),   // "Gate G91"
        standAt(37.61260, -122.39004),   // "Gate A6"
        standAt(37.63562, -122.39153),   // North Maintenance Ramp
    };
    s[7].id = "North Maintenance Ramp";
    for (auto& st : s) st.op = OpType::Airline;
    StandOverrides::apply(data, "KSFO", s);
    auto top = [](const Stand& st) {
        std::string best;
        float w = -1;
        for (const auto& [a, v] : st.airlineWeights) if (v > w) { w = v; best = a; }
        return best;
    };
    CHECK(top(s[0]) == "ASA");
    CHECK(s[0].brand == "ASA");
    CHECK(top(s[1]) == "ASA");
    CHECK(top(s[2]) == "SWA");
    CHECK(top(s[3]) == "DAL");
    CHECK(top(s[4]) == "SKW");
    CHECK(s[4].brand == "UAL");
    CHECK(top(s[5]) == "UAL");
    CHECK(s[6].airlineWeights.count("UAL") == 0);   // Boarding Area A: foreign airlines
    CHECK(s[7].op == OpType::None);
}

TEST_CASE("the share cap never overrules what parks at a stand (KSFO Southwest)") {
    CslDb db;
    db["B738"].push_back(liv("/x/B738_SWA.obj", "SWA"));
    db["B738"].push_back(liv("/x/B738_UAL.obj", "UAL"));
    AirportTable t;
    t.airlines = {{"UAL", 0.98f}, {"SWA", 0.02f}};
    t.types["UAL"]["B738"] = 1.0f;
    t.types["SWA"]["B738"] = 1.0f;
    ParkCluster c;
    c.lat = 37.6169; c.lon = -122.3816; c.n = 40;
    c.airlines = {{"SWA", 40.0f}};
    c.types["SWA"]["B738"] = 40.0f;
    t.clusters.push_back(c);
    const std::set<std::string> full{"SWA"};
    ModelSelector::Extras x;
    x.full = &full;
    Stand s = standAt(37.6169, -122.3816);
    for (std::uint32_t seed = 1; seed < 40; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &x).airline == "SWA");
}

TEST_CASE("a regional in an area of fixed brand only in that brand's paint, else the area's others") {
    CslDb db;
    db["E170"].push_back(liv("/x/E170_TCF.obj", "TCF"));
    db["E170"].back().brand = "DAL";                       // Delta Connection only
    db["B739"].push_back(liv("/x/B739_ASA.obj", "ASA"));
    AirportTable t;
    t.airlines = {{"ASA", 0.7f}, {"SKW", 0.3f}};
    t.types["ASA"]["B739"] = 1.0f;
    t.types["SKW"]["E170"] = 1.0f;
    const auto file = fs::temp_directory_path() / "xpgsi_brand_area.json";
    std::ofstream(file) << R"({"schema":1,"operators":{"SKW":["UAL","DAL","AAL","ASA"]}})";
    const auto brands = RegionalBrands::load(file);
    REQUIRE(brands);
    ModelSelector::Extras x;
    x.brands = &*brands;
    Stand s = standAt(37.6126, -122.3840);
    s.airlines = {"ASA", "SKW"};
    s.airlineWeights = {{"ASA", 0.1f}, {"SKW", 0.9f}};
    s.airlinesOverridden = true;
    s.brand = "ASA";
    for (std::uint32_t seed = 1; seed < 40; ++seed)
        CHECK(ModelSelector::selectWithTable(s, db, t, seed * 2654435761u, nullptr, &x).airline == "ASA");
    // A stand none of the area's airlines fits stays empty (no "any Alaska paint").
    db["DH8D"].push_back(liv("/x/DH8D_ASA.obj", "ASA"));
    Stand small = s;
    small.widthCode = 'B';
    CHECK(ModelSelector::selectWithTable(small, db, t, 7, nullptr, &x).model == nullptr);
    fs::remove(file);
}

// ── ZGGG (Wikipedia only), 2026-10-28 ────────────────────────────────────────

TEST_CASE("100 % fills every usable stand, remote ones included (ZGGG)") {
    // 166 gates + 120 remote stands, the remote ones at weight 0.35.
    const double weighted = Density::weightedStands(166, 120);
    CHECK(Density::target(weighted, 286, 1.0f, -1) == 286);
    CHECK(Density::target(weighted, 286, Density::kAutoMax, -1) ==
          static_cast<std::size_t>(std::llround(weighted * Density::kAutoMax)));   // unchanged up to kAutoMax
    const auto at90 = Density::target(weighted, 286, 0.9f, -1);
    CHECK(at90 > Density::target(weighted, 286, Density::kAutoMax, -1));
    CHECK(at90 < 286);
}

TEST_CASE("an airport without adsb.lol data takes its traffic from the infobox, not the route count") {
    // ZGGG: route score 448, infobox ~680 arrivals a day.
    CHECK(Density::dailyArrivals(-1.0f, 680.0f, 448.2f) == doctest::Approx(680.0f));
    CHECK(Density::dailyArrivals(100.0f, 680.0f, 448.2f) == doctest::Approx(680.0f));
}

TEST_CASE("cargo stands at an airport without adsb.lol cargo data use the article's cargo airlines") {
    WikiEntry w;
    w.icao = "ZGGG";
    w.airlines = {{"CSN", 1.0f}};
    w.cargoAirlines = {{"FDX", 0.6f}, {"CSN", 0.3f}, {"XXX", 0.1f}};
    w.cargoDailyEst = 30.0f;
    Fleets f;
    f.types["CSN"]["all"] = {{"A321", 1.0f}};
    f.cargo["FDX"] = {{"B77L", 0.7f}, {"MD11", 0.3f}};
    f.cargo["CSN"] = {{"B77L", 1.0f}};
    const auto t = WikiFallback::combine(std::nullopt, &w, &f);
    REQUIRE(t);
    CHECK(t->cargoAirlines.size() == 2);   // XXX: no freighter types known
    CHECK(t->cargoAirlines.at("FDX") == doctest::Approx(0.6f / 0.9f));
    CHECK(t->cargoTypes.at("FDX").at("MD11") == doctest::Approx(0.3f));
    CHECK(t->cargoDaily == doctest::Approx(30.0f));
}

// ── LOWW, UTTT, SAAV, YSWS, 2026-10-28 ───────────────────────────────────────

TEST_CASE("widebodies keep the ICAO clearance from their neighbours; the inner wing counts") {
    CHECK(StandLayout::clearanceFor(34.1, 35.8) == doctest::Approx(StandLayout::kClearanceSmallM));
    CHECK(StandLayout::clearanceFor(34.1, 60.1) == doctest::Approx(StandLayout::kClearanceLargeM));
    // A 787-like shape: span 60, outer wing far aft, inner wing + engines forward.
    ObjGeometry::Extents b788;
    b788.valid = true; b788.minX = -30.0f; b788.maxX = 30.0f; b788.minZ = -29.0f; b788.maxZ = 28.6f;
    b788.wingFound = true; b788.wingMinZ = -1.2f; b788.wingMaxZ = 12.3f;
    b788.innerWingFound = true; b788.innerWingMinZ = -10.5f; b788.innerWingMaxZ = 4.9f;
    ObjGeometry::Extents a320 = b788;
    a320.minX = -17.2f; a320.maxX = 17.2f; a320.minZ = -16.5f; a320.maxZ = 21.2f;
    a320.wingMinZ = 0.3f; a320.wingMaxZ = 6.3f; a320.innerWingMinZ = -5.2f; a320.innerWingMaxZ = 3.3f;
    const auto big = StandLayout::shapeFor(b788, 26.0), small = StandLayout::shapeFor(a320, 11.4);
    REQUIRE(big.hasInner);
    // Side by side, the A320 52 m out: 30 + 17.2 = 47.2 → 4.8 m between tips, under 7.5 m.
    CHECK(StandLayout::overlaps(StandLayout::place(big, 0, 0, 0), StandLayout::place(small, 52, 0, 0)));
    CHECK_FALSE(StandLayout::overlaps(StandLayout::place(big, 0, 0, 0), StandLayout::place(small, 56, 0, 0)));
    // The A320 9 m ahead and 40 m out: its wingtip beside the 787's inner wing / engine.
    auto fa = StandLayout::place(big, 0, 0, 0), fb = StandLayout::place(small, 40, 9, 0);
    CHECK(StandLayout::overlaps(fa, fb));
}

TEST_CASE("fleet-mix types: hand-made per airport, small-airport mix, range from home") {
    Fleets f;
    f.types["IGO"]["all"] = {{"A20N", 0.6f}, {"AT76", 0.4f}};
    f.home["IGO"] = {13.1979, 77.7063};                         // Bengaluru
    f.types["DAL"]["all"] = {{"B739", 1.0f}};
    f.types["DAL"]["small_airport"] = {{"B712", 1.0f}};
    f.types["ARG"]["all"] = {{"B738", 1.0f}};
    f.airportTypes["ARG"]["SAAV"] = {{"E190", 1.0f}};
    WikiEntry tas;
    tas.icao = "UTTT"; tas.airlines = {{"IGO", 1.0f}}; tas.onGroundEst = 9.0f;
    auto t = WikiFallback::combine(std::nullopt, &tas, &f, nullptr, 41.2579, 69.2812);
    REQUIRE(t);
    CHECK(t->types.at("IGO").count("AT76") == 0);              // 3,300 km from Bengaluru
    CHECK(t->types.at("IGO").count("A20N") == 1);
    WikiEntry small;
    small.icao = "KXYZ"; small.airlines = {{"DAL", 1.0f}}; small.onGroundEst = 0.4f;
    t = WikiFallback::combine(std::nullopt, &small, &f);
    CHECK(t->types.at("DAL").count("B712") == 1);
    CHECK(t->types.at("DAL").count("B739") == 0);
    small.onGroundEst = 5.0f;                                    // not small: the whole fleet
    t = WikiFallback::combine(std::nullopt, &small, &f);
    CHECK(t->types.at("DAL").count("B739") == 1);
    WikiEntry saav;
    saav.icao = "SAAV"; saav.airlines = {{"ARG", 1.0f}};
    t = WikiFallback::combine(std::nullopt, &saav, &f);
    CHECK(t->types.at("ARG").size() == 1);
    CHECK(t->types.at("ARG").count("E190") == 1);
    CHECK(WikiFallback::typicalRangeKm("B738") == 0.0);
}

TEST_CASE("an airport without data finds its nearest neighbour with data") {
    const auto file = fs::temp_directory_path() / "xpgsi_wiki_pos.json";
    std::ofstream(file) << R"({"schema":1,"airports":{
        "YSSY":{"airlines":{"QFA":1},"pos":[-33.9461,151.1772]},
        "YSCB":{"airlines":{"QFA":1},"pos":[-35.3069,149.1950]},
        "YSWS":{"airlines":{"QFA":1}}}})";
    double km = 0;
    CHECK(WikiFallback::nearestWithData(file, -33.8833, 150.7200, "YSWS", 150.0, &km) == "YSSY");
    CHECK(km == doctest::Approx(42.6).epsilon(0.05));
    CHECK(WikiFallback::nearestWithData(file, -30.0, 140.0, "YSWS").empty());   // nothing within 150 km
    fs::remove(file);
}

TEST_CASE("a size tag after a MARS letter still groups the gate (Global Airports LTFM A2-(F) / A2L-(C) / A2R-(C))") {
    const auto k = StandLayout::gateKeys({"A2-(F)", "A2L-(C)", "A2R-(C)", "A3-(F)", "A10L-(C)", "A10-(E)", "A2L"});
    CHECK(k[0] == "A2");
    CHECK(k[1] == "A2");
    CHECK(k[2] == "A2");
    CHECK(k[3] == "A3");
    CHECK(k[4] == "A10");
    CHECK(k[5] == "A10");
    CHECK(StandLayout::gateKey("Gate 5 (Remote)") == "GATE 5");
    // Two narrowbodies on A2L and A2R, 51 m apart: the second is refused.
    StandLayout::Checker c;
    const auto fp = StandLayout::place(StandLayout::stubShape(), 0, 0, 0);
    c.add(fp, k[1], 0, 0);
    CHECK(c.check(StandLayout::place(StandLayout::stubShape(), 51, 0, 0), k[2], 51, 0) ==
          StandLayout::Checker::Verdict::SameGate);
}

TEST_CASE("KSMF stands 51-60 are cargo stands, by position and by name") {
    const fs::path data = fs::path(FIXTURES_DIR).parent_path().parent_path() / "data" / "stand_overrides.json";
    std::vector<Stand> s{standAt(38.69319, -121.59390), standAt(38.69346, -121.59617), standAt(38.70000, -121.60000),
                         standAt(38.69534, -121.59144)};
    s[0].id = "52"; s[1].id = "Remote 9"; s[2].id = "Gate 57"; s[3].id = "B9";
    for (auto& st : s) { st.op = OpType::Airline; st.type = StandType::Gate; }
    StandOverrides::apply(data, "KSMF", s);
    CHECK(s[0].op == OpType::Cargo);
    CHECK(s[1].op == OpType::Cargo);    // a stand point of 59, another name
    CHECK(s[2].op == OpType::Cargo);    // by name elsewhere
    CHECK(s[3].op == OpType::Airline);  // Terminal B gate untouched
    // StandEvidence leaves them alone, however many airliners parked there.
    std::vector<ParkCell> cells(1);
    cells[0].lat = 38.69319; cells[0].lon = -121.59390; cells[0].pax[1] = 50;
    StandEvidence::apply(s, cells);
    CHECK(s[0].op == OpType::Cargo);
}

TEST_CASE("the local mix is the stand's own neighbourhood where it has data") {
    AirportTable t;
    ParkCluster swa, ual;
    swa.lat = 37.6169; swa.lon = -122.3816; swa.n = 10; swa.airlines = {{"SWA", 10.0f}};
    ual.lat = 37.6181; ual.lon = -122.3816; ual.n = 30; ual.airlines = {{"UAL", 30.0f}};   // 133 m north
    t.clusters = {swa, ual};
    const auto m = ModelSelector::areaMix(standAt(37.6169, -122.3816), t);
    CHECK(m.airlines.at("SWA") > 5.0f * (m.airlines.count("UAL") ? m.airlines.at("UAL") : 0.0f));
    // Too little nearby: the wide area.
    const auto far = ModelSelector::areaMix(standAt(37.6150, -122.3816), t);
    CHECK(far.airlines.count("UAL") == 1);
}

} // TEST_SUITE

TEST_CASE("terminal assignments: zones and their airlines from separate files (license split)") {
    const fs::path zonesFile = fs::temp_directory_path() / "xpgsi_terminal_zones.json";
    const fs::path airFile   = fs::temp_directory_path() / "xpgsi_terminal_airlines.json";
    {
        std::ofstream f(zonesFile, std::ios::binary);
        f << R"({"schema":1,"airports":{"ZSPD":{
               "zones":[{"name":"T1","group":"T1"},{"name":"T2","group":"T2"}],
               "anchors":[[31.150,121.80,0],[31.150,121.81,1]]}}})";
        std::ofstream a(airFile, std::ios::binary);
        a << R"({"schema":1,"airports":{"ZSPD":{"airlines":[["CES"],["CCA"]]}}})";
    }
    AirportTable t;
    t.airlines = {{"CES", 0.5f}, {"CCA", 0.5f}};
    AirportTable noAirlines = t;
    CHECK(AirportData::applyTerminalAssignments(noAirlines, zonesFile, "ZSPD") == 0);   // airlines file missing
    CHECK(AirportData::applyTerminalAssignments(t, zonesFile, "ZSPD", airFile) == 2);
    REQUIRE(t.clusters.size() == 2);
    CHECK(t.clusters[0].airlines.count("CES") == 1);
    CHECK(t.clusters[1].airlines.count("CCA") == 1);
    fs::remove(zonesFile);
    fs::remove(airFile);
}
