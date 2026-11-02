#pragma once
#include <filesystem>
#include <map>
#include <string>

// When the plugin starts placing statics (Phase 8 UI spec).
enum class StartMode {
    OnSimStart,     // default: automatically for every flight, once the flight has
                    // loaded and scenery has settled (not at the main menu)
    Manual,         // only on Start (menu, window button or xpgsi/start)
    VatsimConnected // when xPilot reports a pilot connection; stops on disconnect
};

// Plugin settings loaded from / saved to <pluginRoot>/settings.json.
// All fields have safe defaults so the file is entirely optional; a missing
// or unparsable file gives the defaults, and out-of-range values are clamped.
struct Settings {
    StartMode startMode        = StartMode::OnSimStart;

    // Traffic density: share of usable stands filled, passenger and cargo
    // apart, automatic from each airport's traffic (engine/Density.h). The
    // user's slider settings are kept per airport (ICAO → 0.00–1.00; < 0 =
    // automatic).
    struct AirportDensity { float passenger = -1.0f; float cargo = -1.0f; };
    std::map<std::string, AirportDensity> densityByAirport;
    // Cargo aircraft on the scenery's cargo stands.
    bool      cargoEnabled     = true;

    // Advanced.
    int       maxAircraft      = -1;           // aircraft limit; -1 = unlimited (default)
    float     conflictRadiusM  = 60.0f;        // metres: the user this close keeps a cleared stand empty
    // Seconds until a stand cleared for the user or other traffic gets a
    // static again; 0 (default) = never, it stays empty.
    float     cooldownSec      = 0.0f;
    float     maxRenderDistKm  = 15.0f;        // cull stands farther than this; 0 = no limit
    // Layout seed. 0 (default) = a new random seed every time the plugin is
    // enabled, so each run shows a different (but data-weighted) set of
    // occupied gates, airlines and types. Any other value reproduces one
    // layout exactly — the seed in use is logged, for bug reports.
    int       fixedSeed        = 0;
    bool      enabled          = true;         // false → no statics, no conflict checks
    bool      debugLog         = false;        // Advanced: debug detail (timings) in the log

    // Window geometry in global desktop boxels (left/top/right/bottom also
    // give the size). windowGeometrySaved = false → default placement.
    bool windowGeometrySaved = false;
    int  windowLeft          = 0;
    int  windowTop           = 0;
    int  windowRight         = 0;
    int  windowBottom        = 0;
    // The gate window (arrival gate reservation), same rules.
    bool gateGeometrySaved   = false;
    int  gateLeft            = 0;
    int  gateTop             = 0;
    int  gateRight           = 0;
    int  gateBottom          = 0;

    // Range offered when the user switches the aircraft limit on.
    static constexpr int kAircraftLimitMax     = 500;
    static constexpr int kAircraftLimitDefault = 100;

    // Load from JSON; returns defaults if the file is absent or malformed.
    static Settings load(const std::filesystem::path& path);

    // Write all fields to JSON, overwriting any existing file. Returns false
    // if the file could not be written.
    static bool     save(const std::filesystem::path& path, const Settings& s);

    // Clamp every field to its valid range.
    void sanitize();
};

const char* startModeToStr(StartMode m);
