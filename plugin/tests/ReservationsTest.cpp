// Arrival gate reservation: name matching, the armed window, the reservation
// set, what a reservation clears, the CDU stability rule, the in-view test and
// the CDU adapter file.
#include <doctest/doctest.h>

#include "core/CduAdapters.h"
#include "engine/GateOccupancy.h"
#include "engine/Reservations.h"

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace Reservations;

TEST_SUITE("Reservations") {

// ── Names ─────────────────────────────────────────────────────────────────────

TEST_CASE("normalization: case, separators, scenery prefixes") {
    CHECK(normalizeEntry(" b-12 ") == "B12");
    CHECK(normalizeEntry("c4.a") == "C4A");
    CHECK(normalizeStand("Gate B12") == "B12");
    CHECK(normalizeStand("STAND 305L") == "305L");
    CHECK(normalizeStand("Std_12") == "12");
    CHECK(normalizeStand("GT-7") == "7");
    CHECK(normalizeStand("Parking 4") == "4");
    CHECK(normalizeStand("GATE") == "GATE");        // nothing left: not stripped
    CHECK(normalizeEntry("GATE B12") == "GATEB12");  // the entry is the gate name only
}

TEST_CASE("exact match only; every duplicate across terminals") {
    const std::vector<std::string> names{"Gate 12", "B12", "12", "Stand 12", "120", "12A", "T2 12"};
    auto m = match("12", names);
    CHECK(m == std::vector<std::size_t>{0, 2, 3});   // not 120, 12A or "T2 12"
    CHECK(match("b12", names) == std::vector<std::size_t>{1});
    CHECK(match("1", names).empty());               // no partial matching
    CHECK(match("", names).empty());
}

// ── Set ───────────────────────────────────────────────────────────────────────

TEST_CASE("multiple reservations add up; re-entry adds nothing") {
    Set s;
    CHECK(s.add({3}) == 1);
    CHECK(s.add({7, 8}) == 2);
    CHECK(s.stands().size() == 3);
    CHECK(s.add({3}) == 0);                         // re-entry
    CHECK(s.add({7, 9}) == 1);                      // only the new one counts
    CHECK(s.stands().size() == 4);
}

TEST_CASE("release on parking keeps the stand parked on; on disarm everything goes") {
    Set s;
    s.add({1, 2, 3});
    CHECK(s.releaseAllExcept(2) == 2);
    CHECK(s.stands() == std::set<std::size_t>{2});
    CHECK(s.release(2));
    CHECK_FALSE(s.release(2));
    s.add({4, 5});
    CHECK(s.releaseAllExcept(9) == 2);              // parked elsewhere: all released
    CHECK(s.empty());
    s.add({6});
    CHECK(s.releaseAll() == 1);
}

// ── Armed window ──────────────────────────────────────────────────────────────

static Sample ground(double t, float gs = 20.0f, float brake = 0.0f, bool engines = true) {
    Sample s; s.now = t; s.onGround = true; s.groundSpeedMs = gs; s.parkBrake = brake;
    s.anyEngineRunning = engines;
    return s;
}
static Sample air(double t) { Sample s; s.now = t; s.onGround = false; return s; }

TEST_CASE("arms at touchdown only after 60 s airborne") {
    ArmState a;
    CHECK(a.update(ground(0)) == Event::None);       // loaded on the ground
    CHECK_FALSE(a.armed());
    a.update(air(1));
    a.update(air(40));
    CHECK(a.update(ground(41)) == Event::None);      // a 40 s hop does not arm
    a.update(air(100));
    a.update(air(170));
    CHECK(a.update(ground(171)) == Event::Armed);
    CHECK(a.armed());
}

static ArmState armedState() {
    ArmState a;
    a.update(air(0));
    a.update(ground(100));
    return a;
}

TEST_CASE("disarms when parked (< 1 kt, brake set) for 10 s") {
    ArmState a = armedState();
    REQUIRE(a.armed());
    CHECK(a.update(ground(110, 0.2f, 1.0f)) == Event::None);
    CHECK(a.update(ground(115, 0.2f, 0.0f)) == Event::None);   // brake released: timer restarts
    CHECK(a.update(ground(116, 0.2f, 1.0f)) == Event::None);
    CHECK(a.update(ground(125, 0.2f, 1.0f)) == Event::None);
    CHECK(a.update(ground(126, 0.2f, 1.0f)) == Event::Parked);
    CHECK_FALSE(a.armed());
}

TEST_CASE("disarms when the engines are shut down") {
    ArmState a = armedState();
    CHECK(a.update(ground(110, 3.0f, 0.0f, false)) == Event::EnginesOff);
}

TEST_CASE("a bounced landing stays armed; a takeoff disarms") {
    ArmState a = armedState();
    a.update(air(101));
    CHECK(a.update(ground(103)) == Event::None);     // 2 s bounce
    CHECK(a.armed());
    a.update(air(200));
    CHECK(a.update(air(205)) == Event::None);
    CHECK(a.update(air(211)) == Event::TookOff);
    CHECK_FALSE(a.armed());
}

TEST_CASE("a new flight disarms") {
    ArmState a = armedState();
    a.reset();
    CHECK_FALSE(a.armed());
    CHECK(a.update(ground(500)) == Event::None);
}

// ── CDU stability ─────────────────────────────────────────────────────────────

TEST_CASE("typing B, B1, B12 reserves only B12") {
    StableText st;
    CHECK(st.feed("B", 0.00).empty());
    CHECK(st.feed("B", 0.50).empty());
    CHECK(st.feed("B1", 0.75).empty());
    CHECK(st.feed("B12", 1.25).empty());
    CHECK(st.feed("B12", 2.25).empty());
    CHECK(st.feed("B12", 3.25) == "B12");
    CHECK(st.feed("B12", 3.50).empty());             // processed once
}

TEST_CASE("each distinct entry once; a re-entry after clearing does nothing new") {
    StableText st;
    st.feed("C4", 0.0);
    CHECK(st.feed("C4", 2.0) == "C4");
    st.feed("", 3.0);
    CHECK(st.feed("", 9.0).empty());                 // empty never fires
    st.feed("C4", 10.0);
    CHECK(st.feed("C4", 12.5).empty());              // re-entry
    st.feed("D7", 13.0);
    CHECK(st.feed("D7", 15.0) == "D7");              // a different gate later
    st.reset();
    st.feed("C4", 20.0);
    CHECK(st.feed("C4", 22.0) == "C4");              // new armed session
}

TEST_CASE("cleanCduText trims and drops control bytes") {
    const char raw[] = {' ', 'B', '1', '2', ' ', ' ', '\0', 'X'};
    CHECK(cleanCduText(raw, sizeof(raw)) == "B12");
    const char ctl[] = {'\x01', 'A', '\x7f', '1'};
    CHECK(cleanCduText(ctl, sizeof(ctl)) == "A 1");
}

// ── What a reservation clears ─────────────────────────────────────────────────

static Stand at(const char* id, double lat, double lon, char code = 'C') {
    Stand s; s.id = id; s.lat = lat; s.lon = lon; s.headingDeg = 0.0f; s.widthCode = code;
    return s;
}

TEST_CASE("blocked: the stand, its gate's other positions, and statics it would touch") {
    const double lat = 37.615, lon = -122.39, mE = 1.0 / (111320.0 * std::cos(lat * 3.14159265 / 180.0));
    const Stand r = at("F22", lat, lon);
    const std::vector<Stand> plan{
        at("F22", lat, lon),                 // the reserved stand itself
        at("F22A", lat, lon + 15 * mE),      // another position of the gate
        at("F23", lat, lon + 30 * mE),       // next gate, 30 m: a narrowbody next to an E code touches
        at("F24", lat, lon + 80 * mE),       // far enough
    };
    StandLayout::Shape nb;   // planned narrowbody, nose gear on the stand point
    nb.fMin = -36; nb.fMax = 4; nb.halfWidth = 2;
    nb.hasWing = true; nb.wingFMin = -22; nb.wingFMax = -14; nb.wingRMin = -18; nb.wingRMax = 18;
    const std::vector<StandLayout::Shape> shapes(plan.size(), nb);

    auto b = blockedPlanStands({r}, plan, shapes, userShapeForSize('E'));
    CHECK(b[0]);
    CHECK(b[1]);
    CHECK(b[2]);
    CHECK_FALSE(b[3]);

    b = blockedPlanStands({r}, plan, shapes, userShapeForSize('A'));   // a small aircraft
    CHECK(b[0]);
    CHECK(b[1]);
    CHECK_FALSE(b[2]);
    CHECK(blockedPlanStands({}, plan, shapes, userShapeForSize('C')) == std::vector<bool>(4, false));
}

TEST_CASE("userShapeForSize puts the nose on the stand point") {
    const auto s = userShapeForSize('C');
    CHECK(s.fMax == doctest::Approx(0.0));
    CHECK(s.fMin < -30.0);
}

TEST_CASE("clearNow: out of view, behind, or within 150 m") {
    CHECK(clearNow(false, false, 2000));
    CHECK(clearNow(true, true, 2000));
    CHECK(clearNow(true, false, 149));
    CHECK_FALSE(clearNow(true, false, 400));
}

TEST_CASE("inView: camera looking north sees north, not south or far left") {
    // Local frame: +X east, +Z south. Target 500 m north = (0, 0, -500).
    CHECK(inView(0, 0, -500, 0, 0, 60, 40, 0));
    CHECK_FALSE(inView(0, 0, 500, 0, 0, 60, 40, 0));         // behind
    CHECK_FALSE(inView(-500, 0, -100, 0, 0, 60, 40, 0));     // 79 deg to the left
    CHECK(inView(-500, 0, -100, 270, 0, 60, 40, 0));         // looking west
    CHECK(inView(-150, 0, -500, 0, 0, 60, 40, 40));          // edge, widened by the radius
    CHECK_FALSE(inView(0, 400, -500, 0, 0, 60, 40, 0));      // well above a 40 deg vertical view
    CHECK(inView(0, 400, -500, 0, 38, 60, 40, 0));           // pitched up
}

} // TEST_SUITE

TEST_SUITE("CduAdapters") {

static std::filesystem::path writeTmp(const char* body) {
    const auto p = std::filesystem::temp_directory_path() / "xpgsi_cdu_adapters_test.json";
    std::ofstream(p) << body;
    return p;
}

TEST_CASE("parse and match: dataref presence, acf path, ICAO; first enabled wins") {
    const auto p = writeTmp(R"({"schema": 1, "adapters": [
        {"name": "Zibo", "enabled": true, "match": {"requires_dataref": "laminar/B738/fmc1/Line_entry"},
         "read": {"dataref": "laminar/B738/fmc1/Line_entry", "bytes": 128}},
        {"name": "Off", "enabled": false, "match": {"icao": ["B738"]}, "read": {"dataref": "x"}},
        {"name": "Laminar", "enabled": true, "match": {"acf_path_contains": "Aircraft/Laminar Research/"},
         "read": {"dataref": "sim/cockpit2/radios/indicators/fms_cdu1_text_line13"}},
        {"name": "ToLiss", "enabled": true, "match": {"icao": ["a320"], "acf_file_regex": "a320.*\\.acf"},
         "read": {"dataref": "AirbusFBW/X"}},
        {"name": "No read", "enabled": true, "match": {"icao": ["A21N"]}, "read": {"dataref": ""}},
        {"name": "No conditions", "enabled": true, "read": {"dataref": "y"}}
    ]})");
    std::vector<CduAdapters::Adapter> a;
    REQUIRE(CduAdapters::load(p, a));
    CHECK(a.size() == 5);                            // "No read" skipped
    CHECK(a[0].readBytes == 128);

    auto zibo = [](const std::string& n) { return n == "laminar/B738/fmc1/Line_entry"; };
    auto none = [](const std::string&) { return false; };
    CduAdapters::Aircraft b738{"b738.acf", "D:\\XP\\Aircraft\\Laminar Research\\Boeing B737-800\\b738.acf", "B738"};
    CHECK(CduAdapters::find(a, b738, zibo)->name == "Zibo");      // Zibo's dataref wins
    CHECK(CduAdapters::find(a, b738, none)->name == "Laminar");   // backslashes normalized
    CduAdapters::Aircraft a320{"A320_XP11.acf", "/xp/Aircraft/ToLiss/a320_XP11.acf", "A320"};
    CHECK(CduAdapters::find(a, a320, none)->name == "ToLiss");
    CduAdapters::Aircraft ff{"777-300ER.acf", "/xp/Aircraft/FF777/777-300ER.acf", "B77W"};
    CHECK(CduAdapters::find(a, ff, none) == nullptr);             // no conditions = never
    std::filesystem::remove(p);
}

TEST_CASE("missing or malformed adapter file") {
    std::vector<CduAdapters::Adapter> a;
    std::string err;
    CHECK_FALSE(CduAdapters::load("no_such_file.json", a, &err));
    CHECK_FALSE(CduAdapters::load(writeTmp("{\"schema\": 2}"), a, &err));
    CHECK(a.empty());
}

TEST_CASE("shipped cdu_adapters.json: Zibo enabled, unverified ones disabled") {
    std::vector<CduAdapters::Adapter> a;
    REQUIRE(CduAdapters::load(std::filesystem::path(FIXTURES_DIR) / ".." / ".." / "data" / "cdu_adapters.json", a));
    REQUIRE(a.size() == 3);
    CHECK(a[0].name.find("Zibo") != std::string::npos);
    CHECK(a[1].name.find("ToLiss") != std::string::npos);
    CHECK(a[2].readDataref.find("{n}") != std::string::npos);   // default: every line
    CHECK(a[0].enabled);
    CHECK_FALSE(a[1].enabled);   // not verified in the sim: hidden for the release
    CHECK_FALSE(a[2].enabled);
}

TEST_CASE("match: the gate part of a terminal-prefixed name") {
    const std::vector<std::string> names{"T2 Gate F15", "T3 Gate G7", "F1"};
    CHECK(match("F15", names) == std::vector<std::size_t>{0});
    CHECK(match("T2 Gate F15", names) == std::vector<std::size_t>{0});
    CHECK(match("F1", names) == std::vector<std::size_t>{2});   // exact wins
    CHECK(match("F", names).empty());
}

TEST_CASE("CDU gate entry needs the G prefix") {
    std::string s;
    CHECK(cduGateEntry("GB12", s));
    CHECK(s == "B12");
    CHECK(cduGateEntry("G/B12", s));
    CHECK(s == "B12");
    CHECK(cduGateEntry("g A2L", s));
    CHECK(s == "A2L");
    CHECK_FALSE(cduGateEntry("B12", s));
    CHECK_FALSE(cduGateEntry("G", s));
    CHECK_FALSE(cduGateEntry("G/", s));
    CHECK_FALSE(cduGateEntry("A1", s));
}

} // TEST_SUITE
