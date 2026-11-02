// Pure logic behind the Phase 8 window: density per airport, target,
// auto-start rules, status/banner text, airport types.
#include <doctest/doctest.h>

#include "core/Settings.h"
#include "data/AirportTypes.h"
#include "engine/AutoStart.h"
#include "engine/Density.h"
#include "engine/Planner.h"
#include "ui/UiText.h"

#include <filesystem>

// ── Density per airport ───────────────────────────────────────────────────────
TEST_SUITE("Density") {

TEST_CASE("traffic → category: hub, standard from 10 airline arrivals a day, else regional") {
    CHECK(Density::categoryFor(true, 5.0f)    == SizeCategory::Hub);      // major hub wins
    CHECK(Density::categoryFor(true, -1.0f)   == SizeCategory::Hub);
    CHECK(Density::categoryFor(false, 40.0f)  == SizeCategory::Standard); // e.g. KAVL
    CHECK(Density::categoryFor(false, 10.0f)  == SizeCategory::Standard);
    CHECK(Density::categoryFor(false, 9.9f)   == SizeCategory::Regional); // e.g. KMLB
    CHECK(Density::categoryFor(false, 0.0f)   == SizeCategory::Regional);
    CHECK(Density::categoryFor(false, -1.0f)  == SizeCategory::Regional); // no data
    // The larger measure counts: KEYW has an adsb.lol coverage gap.
    CHECK(Density::dailyArrivals(0.2f, 23.0f)   == doctest::Approx(23.0f));
    CHECK(Density::dailyArrivals(40.0f, -1.0f)  == doctest::Approx(40.0f));
    CHECK(Density::dailyArrivals(-1.0f, -1.0f)  < 0.0f);
    // The route score only without either.
    CHECK(Density::dailyArrivals(-1.0f, -1.0f, 100.0f) == doctest::Approx(100.0f * Density::kScoreToArrivals));
    CHECK(Density::dailyArrivals(30.0f, -1.0f, 100.0f) == doctest::Approx(30.0f));
}

TEST_CASE("parked by day = arrivals × ground time / active day") {
    CHECK(Density::groundHours(0.0f) == doctest::Approx(Density::kNarrowGroundHours));
    CHECK(Density::groundHours(1.0f) == doctest::Approx(Density::kWideGroundHours));
    CHECK(Density::groundHours(0.5f) > Density::kNarrowGroundHours);
    // EGLL ≈ 640 arrivals, ~65 % widebody ground hours → ~130 parked.
    CHECK(Density::parkedAtOnce(640.0f, 0.65f) == doctest::Approx(131.0f).epsilon(0.03));
    // KPDX ≈ 260, narrowbody → ~31.
    CHECK(Density::parkedAtOnce(260.0f, 0.03f) == doctest::Approx(31.0f).epsilon(0.05));
    CHECK(Density::parkedAtOnce(-1.0f, 0.5f) < 0.0f);
    // Density = parked over the stands; a scenery with twice the stands gets
    // the same aircraft.
    CHECK(Density::autoPassenger(31.0f, 62.0) == doctest::Approx(0.5f));
    CHECK(Density::autoPassenger(31.0f, 124.0) == doctest::Approx(0.25f));
    CHECK(Density::autoPassenger(500.0f, 100.0) == doctest::Approx(Density::kAutoMax));
    CHECK(Density::autoPassenger(0.1f, 100.0) == doctest::Approx(Density::kAutoFloor));
    CHECK(Density::autoPassenger(-1.0f, 100.0) == doctest::Approx(Density::kNoDataDensity));
    CHECK(std::string(Density::name(SizeCategory::Regional)) == "regional");
}

TEST_CASE("automatic cargo density: cargo aircraft on the ground over the cargo stands") {
    CHECK(Density::autoCargo(4.0f, 34)   == doctest::Approx(0.05f));   // KMSP: was 73 %, a crowded apron
    CHECK(Density::autoCargo(138.0f, 94) == doctest::Approx(0.61f));   // KSDF, the UPS hub
    CHECK(Density::autoCargo(45.0f, 29)  == doctest::Approx(0.65f));   // KMIA
    CHECK(Density::autoCargo(500.0f, 10) == doctest::Approx(Density::kCargoMax));
    CHECK(Density::autoCargo(0.0f, 20)   == doctest::Approx(Density::kCargoMin));
    CHECK(Density::autoCargo(10.0f, 0)   == doctest::Approx(Density::kCargoMin));
}

TEST_CASE("a density set at one airport is kept for that airport and that kind only") {
    Settings s;
    Density::set(s, "KMSP", Density::Kind::Cargo, 0.20f);
    CHECK(Density::get(s, "KMSP", Density::Kind::Cargo, 0.05f)     == doctest::Approx(0.20f));
    CHECK(Density::get(s, "KMSP", Density::Kind::Passenger, 0.74f) == doctest::Approx(0.74f));   // automatic
    CHECK(Density::get(s, "KSDF", Density::Kind::Cargo, 0.61f)     == doctest::Approx(0.61f));
    CHECK(Density::isCustom(s, "KMSP", Density::Kind::Cargo));
    CHECK_FALSE(Density::isCustom(s, "KMSP", Density::Kind::Passenger));
    Density::set(s, "", Density::Kind::Cargo, 0.9f);   // no airport yet: nothing kept
    CHECK(s.densityByAirport.count("") == 0);
}

TEST_CASE("range 0–100 %, whole percent steps") {
    Settings s;
    Density::set(s, "KLAS", Density::Kind::Passenger, 1.20f);
    CHECK(Density::get(s, "KLAS", Density::Kind::Passenger, 0.5f) == doctest::Approx(1.00f));
    Density::set(s, "KLAS", Density::Kind::Passenger, -0.5f);
    CHECK(Density::get(s, "KLAS", Density::Kind::Passenger, 0.5f) == doctest::Approx(0.00f));
    Density::set(s, "KLAS", Density::Kind::Passenger, 0.4449f);
    CHECK(Density::get(s, "KLAS", Density::Kind::Passenger, 0.5f) == doctest::Approx(0.44f));
}

TEST_CASE("reset returns that kind to automatic") {
    Settings s;
    Density::set(s, "KLAS", Density::Kind::Passenger, 0.20f);
    Density::set(s, "KLAS", Density::Kind::Cargo, 0.80f);
    Density::reset(s, "KLAS", Density::Kind::Passenger);
    CHECK(Density::get(s, "KLAS", Density::Kind::Passenger, 0.75f) == doctest::Approx(0.75f));
    CHECK(Density::get(s, "KLAS", Density::Kind::Cargo, 0.05f)     == doctest::Approx(0.80f));
    Density::reset(s, "KLAS", Density::Kind::Cargo);
    CHECK(s.densityByAirport.empty());
}

TEST_CASE("fill share is the slider value (the bent scale was reverted)") {
    for (float s : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        CHECK(Density::fillShare(s) == doctest::Approx(s));
}

TEST_CASE("remote stands count less toward the target, unless most stands are remote") {
    // EGLL-like: 103 gates + 40 remote → full remote discount.
    CHECK(Planner::remoteWeight(103, 40) == doctest::Approx(0.35));
    // RJTT: 103 gates of 242 (43 %) → slightly raised.
    const double w = Planner::remoteWeight(103, 139);
    CHECK(w == doctest::Approx(0.35 + 0.65 * (0.5 - 103.0 / 242) / 0.5));
    // Mostly remote (5 gates, 45 remote) and remote-only: close to / exactly 1.
    CHECK(Planner::remoteWeight(5, 45) > 0.85);
    CHECK(Planner::remoteWeight(0, 30) == doctest::Approx(1.0));
    CHECK(Density::target(Density::weightedStands(0, 30), 30, 0.5f, -1) == 15);   // not near-empty
    CHECK(Density::target(Density::weightedStands(103, 40), 143, 0.75f, -1) == 88);
}

TEST_CASE("target = round(usable × fill share); unlimited unless a limit is set") {
    CHECK(Density::target(275, 0.75f, -1)  == 206);   // EGLL-sized, no limit
    CHECK(Density::target(275, 1.00f, -1)  == 275);
    CHECK(Density::target(275, 0.00f, -1)  == 0);
    CHECK(Density::target(140, 0.75f, 60)  == 60);    // optional limit
    CHECK(Density::target(87,  0.50f, -1)  == 44);    // 43.5 rounds half away from zero
    CHECK(Density::target(10,  0.75f, 0)   == 0);     // limit 0 = none placed
    CHECK(Density::target(0,   0.75f, -1)  == 0);
}

TEST_CASE("a small regional airport still gets one static") {
    CHECK(Density::target(1, 0.25f, -1) == 1);        // round(0.25) = 0 → 1
    CHECK(Density::target(2, 0.25f, -1) == 1);
    CHECK(Density::target(6, 0.25f, -1) == 2);        // 1.5 rounds up
    CHECK(Density::target(3, 0.00f, -1) == 0);        // 0 % still means none
    CHECK(Density::target(3, 0.25f, 0)  == 0);        // and so does a limit of 0
}

} // TEST_SUITE

// ── Auto-start rules ──────────────────────────────────────────────────────────
TEST_SUITE("AutoStart") {

using A = AutoStart::Action;

TEST_CASE("manual mode never starts or stops by itself") {
    AutoStart a;
    a.onFlightLoading(0);
    CHECK(a.tick(StartMode::Manual, false, 100, 1) == A::None);
    CHECK(a.tick(StartMode::Manual, true, 101, 0) == A::None);
}

TEST_CASE("start when sim starts: waits for a loaded flight + 5 s of quiet scenery") {
    AutoStart a;
    CHECK(a.tick(StartMode::OnSimStart, false, 10, -1) == A::None);   // no flight yet
    a.onFlightLoading(10);
    CHECK(a.tick(StartMode::OnSimStart, false, 12, -1) == A::None);
    a.onSceneryLoaded(13);                                                   // pushes the timer
    CHECK(a.tick(StartMode::OnSimStart, false, 17, -1) == A::None);
    CHECK(a.tick(StartMode::OnSimStart, false, 18, -1) == A::Start);
    CHECK(a.tick(StartMode::OnSimStart, true, 19, -1) == A::None);   // already running
}

TEST_CASE("manual Stop suppresses auto-start until the next flight loads") {
    AutoStart a;
    a.onFlightLoading(0);
    a.onManualStop();
    CHECK(a.tick(StartMode::OnSimStart, false, 100, -1) == A::None);
    a.onFlightLoading(200);                                                  // next flight
    CHECK(a.tick(StartMode::OnSimStart, false, 206, -1) == A::Start);
}

TEST_CASE("VATSIM: starts on pilot connection, stops on disconnect") {
    AutoStart a;
    a.onFlightLoading(0);
    CHECK(a.tick(StartMode::VatsimConnected, false, 10, 0) == A::None);   // not connected
    CHECK(a.tick(StartMode::VatsimConnected, false, 11, 2) == A::None);   // observer
    CHECK(a.tick(StartMode::VatsimConnected, false, 12, 1) == A::Start);
    CHECK(a.tick(StartMode::VatsimConnected, true, 13, 1) == A::None);
    CHECK(a.tick(StartMode::VatsimConnected, true, 14, 0) == A::Stop);
}

TEST_CASE("VATSIM: connected before the flight is ready waits for it") {
    AutoStart a;
    a.onFlightLoading(100);
    CHECK(a.tick(StartMode::VatsimConnected, false, 102, 1) == A::None);
    CHECK(a.tick(StartMode::VatsimConnected, false, 105, 1) == A::Start);
}

TEST_CASE("VATSIM: xPilot absent (-1) never starts") {
    AutoStart a;
    a.onFlightLoading(0);
    CHECK(a.tick(StartMode::VatsimConnected, false, 100, -1) == A::None);
}

TEST_CASE("VATSIM: manual Stop while connected suppresses until next flight") {
    AutoStart a;
    a.onFlightLoading(0);
    CHECK(a.tick(StartMode::VatsimConnected, false, 10, 1) == A::Start);
    a.onManualStop();
    CHECK(a.tick(StartMode::VatsimConnected, false, 11, 1) == A::None);
}

} // TEST_SUITE

// ── Status line and banners ───────────────────────────────────────────────────
TEST_SUITE("UiText") {

TEST_CASE("running status line with counts") {
    UiText::Status s;
    s.running = true; s.icao = "KLGA"; s.category = "large";
    s.parked = 42; s.cleared = 3; s.loading = 5;
    CHECK(UiText::statusLine(s) ==
          "Running \xC2\xB7 KLGA (large) \xC2\xB7 42 parked \xC2\xB7 3 cleared \xC2\xB7 loading 5");
    s.loading = 0;
    CHECK(UiText::statusLine(s) ==
          "Running \xC2\xB7 KLGA (large) \xC2\xB7 42 parked \xC2\xB7 3 cleared");
}

TEST_CASE("stopped and waiting status lines") {
    UiText::Status s;
    CHECK(UiText::statusLine(s) == "Stopped");
    s.running = true; s.waitingForFlight = true;
    CHECK(UiText::statusLine(s).find("waiting") != std::string::npos);
    s.preparing = "building the airport index";
    CHECK(UiText::statusLine(s).find("building the airport index") != std::string::npos);
    CHECK(UiText::summary(s) == "preparing");
}

TEST_CASE("header summary: airport and parked count, or the state") {
    UiText::Status s;
    CHECK(UiText::summary(s) == "stopped");
    s.icao = "KSMO";
    CHECK(UiText::summary(s) == "KSMO \xC2\xB7 stopped");
    s.running = true; s.parked = 42; s.loading = 5;
    CHECK(UiText::summary(s) == "KSMO \xC2\xB7 42 parked");
    s.parked = 0;
    CHECK(UiText::summary(s) == "KSMO \xC2\xB7 loading 5");
    s.icao.clear();
    CHECK(UiText::summary(s) == "waiting for flight");
}

TEST_CASE("banners: each problem gives one sentence with a fix") {
    UiText::Problems p;
    CHECK(UiText::banners(p).empty());

    p.cslScanDone = true; p.cslModels = 0;
    p.xplaneTooOld = true;
    p.noEligibleStands = true; p.airportIcao = "KSMQ";
    auto b = UiText::banners(p);
    REQUIRE(b.size() == 3);
    CHECK(b[0].find("11.50") != std::string::npos);
    CHECK(b[1].find("CSL") != std::string::npos);
    CHECK(b[1].find("X-Plane 11") == std::string::npos);   // the same advice in XP12
    CHECK(b[2].find("KSMQ") != std::string::npos);
}

TEST_CASE("VATSIM mode without xPilot") {
    UiText::Problems p;
    p.vatsimMode = true; p.xpilotMissing = true;
    auto b = UiText::banners(p);
    REQUIRE(b.size() == 1);
    CHECK(b[0].find("xPilot") != std::string::npos);
}

} // TEST_SUITE

// ── Airport size types ────────────────────────────────────────────────────────
TEST_SUITE("AirportTypes") {

TEST_CASE("shipped airports_eligible.json gives size types") {
    AirportTypes t;
    REQUIRE(t.load(std::filesystem::path(FIXTURES_DIR) / ".." / ".." / "data" / "airports_eligible.json"));
    CHECK(t.size() > 1000);
    CHECK(t.typeOf("KLGA") == "large_airport");
    CHECK(t.typeOf("KIND") == "large_airport");
    CHECK(t.typeOf("ZZZZ").empty());
}

TEST_CASE("shipped major_hubs.json: busiest airports and big US Class B only") {
    AirportTypes t;
    REQUIRE(t.loadHubs(std::filesystem::path(FIXTURES_DIR) / ".." / ".." / "data" / "major_hubs.json"));
    CHECK(t.hubCount() == 73);   // 72 by the rule + OMAA
    for (const char* hub : {"KATL", "EGLL", "OMDB", "YSSY", "KSFO", "KBOS", "KSAN", "PHNL", "EDDM", "OMAA"})
        CHECK(t.isHub(hub));
    // 50 %: below 40 M worldwide, or a quieter US Class B airport.
    for (const char* other : {"YBBN", "YMML", "KSMF", "KAUS", "KPDX", "KPIT", "KCVG", "KCLE",
                              "KMEM", "KMCI", "KSTL", "KMSY", "KHOU", "KADW", "KNKX"})
        CHECK_FALSE(t.isHub(other));
}

TEST_CASE("missing or malformed file → empty") {
    AirportTypes t;
    CHECK_FALSE(t.load("no_such_file.json"));
    CHECK_FALSE(t.load(std::filesystem::path(FIXTURES_DIR) / "settings" / "invalid.json"));
    CHECK(t.size() == 0);
}

} // TEST_SUITE
