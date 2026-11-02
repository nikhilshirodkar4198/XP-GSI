#include <doctest/doctest.h>
#include "core/Settings.h"
#include <filesystem>
#include <fstream>

static std::filesystem::path settingsDir() {
    return std::filesystem::path(FIXTURES_DIR) / "settings";
}

TEST_SUITE("Settings") {

TEST_CASE("missing file returns defaults") {
    auto s = Settings::load(settingsDir() / "nonexistent.json");
    CHECK(s.conflictRadiusM == doctest::Approx(60.0f));
    CHECK(s.cooldownSec     == doctest::Approx(0.0f));
    CHECK(s.enabled         == true);
}

TEST_CASE("valid file overrides all fields") {
    auto s = Settings::load(settingsDir() / "valid.json");
    CHECK(s.conflictRadiusM == doctest::Approx(80.0f));
    CHECK(s.cooldownSec     == doctest::Approx(300.0f));
    CHECK(s.enabled         == true);
}

TEST_CASE("partial file (only enabled key) leaves others at default") {
    auto s = Settings::load(settingsDir() / "disabled.json");
    CHECK(s.enabled         == false);
    CHECK(s.conflictRadiusM == doctest::Approx(60.0f));
    CHECK(s.cooldownSec     == doctest::Approx(0.0f));
}

TEST_CASE("invalid JSON returns defaults") {
    auto s = Settings::load(settingsDir() / "invalid.json");
    CHECK(s.conflictRadiusM == doctest::Approx(60.0f));
    CHECK(s.cooldownSec     == doctest::Approx(0.0f));
    CHECK(s.enabled         == true);
}

TEST_CASE("start_mode defaults to OnSimStart") {
    auto s = Settings::load(settingsDir() / "valid.json");
    CHECK(s.startMode == StartMode::OnSimStart);
}

// ── save() round-trip ─────────────────────────────────────────────────────────

static std::filesystem::path tmpSettings() {
    return std::filesystem::temp_directory_path() / "xpgsi_test_roundtrip.json";
}

TEST_CASE("save/load round-trip preserves all fields") {
    auto tmp = tmpSettings();
    // Remove any stale file from a previous run
    std::filesystem::remove(tmp);

    Settings a;
    a.conflictRadiusM  = 42.5f;
    a.cooldownSec      = 77.0f;
    a.enabled          = false;
    a.startMode        = StartMode::Manual;
    a.maxRenderDistKm  = 20.0f;

    CHECK(Settings::save(tmp, a));
    auto b = Settings::load(tmp);

    CHECK(b.conflictRadiusM  == doctest::Approx(42.5f));
    CHECK(b.cooldownSec      == doctest::Approx(77.0f));
    CHECK(b.enabled          == false);
    CHECK(b.startMode        == StartMode::Manual);
    CHECK(b.maxRenderDistKm  == doctest::Approx(20.0f));
    CHECK_FALSE(std::filesystem::exists(tmp.string() + ".tmp"));   // atomic replace

    std::filesystem::remove(tmp);
}

TEST_CASE("save/load round-trip preserves VatsimConnected start mode") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);

    Settings a;
    a.startMode = StartMode::VatsimConnected;
    Settings::save(tmp, a);
    auto b = Settings::load(tmp);
    CHECK(b.startMode == StartMode::VatsimConnected);

    std::filesystem::remove(tmp);
}

TEST_CASE("fixed_seed defaults to 0 (new layout every run) and round-trips") {
    CHECK(Settings{}.fixedSeed == 0);
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);

    Settings a;
    a.fixedSeed = 123456789;
    Settings::save(tmp, a);
    CHECK(Settings::load(tmp).fixedSeed == 123456789);

    std::filesystem::remove(tmp);
}

TEST_CASE("save/load round-trip preserves window geometry") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);

    Settings a;
    a.windowGeometrySaved = true;
    a.windowLeft   = 100;
    a.windowTop    = 900;
    a.windowRight  = 400;
    a.windowBottom = 750;
    Settings::save(tmp, a);
    auto b = Settings::load(tmp);

    CHECK(b.windowGeometrySaved == true);
    CHECK(b.windowLeft   == 100);
    CHECK(b.windowTop    == 900);
    CHECK(b.windowRight  == 400);
    CHECK(b.windowBottom == 750);

    std::filesystem::remove(tmp);
}

TEST_CASE("defaults: every airport's density automatic, no aircraft limit") {
    auto s = Settings::load(settingsDir() / "nonexistent.json");
    CHECK(s.densityByAirport.empty());
    CHECK(s.maxAircraft   == -1);                    // unlimited
    CHECK(s.startMode     == StartMode::OnSimStart);
}

TEST_CASE("unlimited aircraft (-1) round-trips") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);
    Settings::save(tmp, Settings{});
    CHECK(Settings::load(tmp).maxAircraft == -1);
    std::filesystem::remove(tmp);
}

TEST_CASE("save/load round-trip preserves per-airport densities and max aircraft") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);

    Settings a;
    a.densityByAirport["KMSP"] = {0.83f, 0.12f};
    a.densityByAirport["KSDF"] = {-1.0f, 0.70f};   // passenger automatic
    a.maxAircraft     = 150;
    Settings::save(tmp, a);
    auto b = Settings::load(tmp);

    REQUIRE(b.densityByAirport.size() == 2);
    CHECK(b.densityByAirport["KMSP"].passenger == doctest::Approx(0.83f));
    CHECK(b.densityByAirport["KMSP"].cargo     == doctest::Approx(0.12f));
    CHECK(b.densityByAirport["KSDF"].passenger < 0.0f);
    CHECK(b.densityByAirport["KSDF"].cargo     == doctest::Approx(0.70f));
    CHECK(b.maxAircraft     == 150);

    std::filesystem::remove(tmp);
}

TEST_CASE("out-of-range values are clamped on load") {
    auto tmp = tmpSettings();
    {
        std::ofstream f(tmp);
        f << R"({"density_by_airport": {"KLAS": {"passenger": 1.5, "cargo": "x"}, "KSDF": {"cargo": -0.2},
                                        "KMSP": 3},
                 "aircraft_limit": 9999, "restore_cooldown_sec": -5,
                 "window": {"left": 500, "top": 100, "right": 400, "bottom": 900}})";
    }
    auto s = Settings::load(tmp);
    CHECK(s.densityByAirport["KLAS"].passenger == doctest::Approx(1.00f));   // max 100 %
    CHECK(s.densityByAirport["KLAS"].cargo     < 0.0f);                      // wrong type → automatic
    CHECK(s.densityByAirport.count("KSDF") == 0);                            // nothing valid left
    CHECK(s.densityByAirport.count("KMSP") == 0);
    CHECK(s.maxAircraft   == Settings::kAircraftLimitMax);
    CHECK(s.cooldownSec   == doctest::Approx(0.0f));
    CHECK_FALSE(s.windowGeometrySaved);                 // degenerate geometry dropped
    std::filesystem::remove(tmp);
}

TEST_CASE("corrupt or non-object file → defaults") {
    auto tmp = tmpSettings();
    for (const char* body : {"{ \"density\": ", "[1, 2, 3]", "", "null"}) {
        { std::ofstream f(tmp); f << body; }
        auto s = Settings::load(tmp);
        CHECK(s.densityByAirport.empty());
        CHECK(s.maxAircraft  == -1);
        CHECK(s.startMode    == StartMode::OnSimStart);
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("legacy keys are ignored (pre-UI, the first UI build, the per-category builds)") {
    auto tmp = tmpSettings();
    { std::ofstream f(tmp); f << R"({"density": {"large": 0.2, "medium": 0.5, "small": 0.35},
                                     "traffic_density": {"large": 0.75, "medium": 0.75, "small": 0.75},
                                     "traffic_density_v2": {"hub": 0.8, "medium": 0.45},
                                     "traffic_density_v3": {"hub": 0.6, "standard": 0.4, "regional": 0.2},
                                     "max_aircraft": 200, "occupancy_large": 0.8,
                                     "vatsim_plugin_sig": "x"})"; }
    auto s = Settings::load(tmp);
    CHECK(s.densityByAirport.empty());   // every airport automatic
    CHECK(s.maxAircraft   == -1);
    std::filesystem::remove(tmp);
}

TEST_CASE("cargo aircraft option: on by default, saved and loaded") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);
    CHECK(Settings::load(tmp).cargoEnabled);
    Settings a;
    a.cargoEnabled = false;
    Settings::save(tmp, a);
    CHECK_FALSE(Settings::load(tmp).cargoEnabled);
    std::filesystem::remove(tmp);
}

TEST_CASE("save without geometry flag does not persist geometry") {
    auto tmp = tmpSettings();
    std::filesystem::remove(tmp);

    Settings a;
    a.windowGeometrySaved = false;
    a.windowLeft = 999;
    Settings::save(tmp, a);
    auto b = Settings::load(tmp);

    CHECK(b.windowGeometrySaved == false);
    // windowLeft should remain at default (0), not 999
    CHECK(b.windowLeft == 0);

    std::filesystem::remove(tmp);
}

} // TEST_SUITE

TEST_CASE("settings round-trip in a folder with non-ASCII characters (accented Windows user names)") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"xpgsi_t\u00E9st_\u65E5\u672C");
    fs::create_directories(dir);
    const fs::path file = dir / "settings.json";
    Settings s;
    s.debugLog = true;
    s.maxAircraft = 42;
    REQUIRE(Settings::save(file, s));
    const Settings back = Settings::load(file);
    CHECK(back.debugLog);
    CHECK(back.maxAircraft == 42);
    std::error_code ec;
    fs::remove_all(dir, ec);
}