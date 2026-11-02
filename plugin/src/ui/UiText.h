#pragma once
// UiText — status line and error banners for the main window (Phase 8 UI
// spec). Pure string logic, no XPLM / ImGui calls, so it is unit-tested.

#include <string>
#include <vector>

namespace UiText {

struct Status {
    bool        running = false;
    std::string icao;           // active airport; empty = none yet
    std::string category;       // "major hub" / "standard" / "regional"
    int         parked  = 0;    // statics on stands now
    int         cleared = 0;    // statics removed for live traffic (cooldown)
    int         loading = 0;    // planned statics not yet placed
    bool        waitingForFlight = false;  // running, flight still loading / settling
    std::string preparing;      // start-up work under way, e.g. "building the airport index"; empty = none
};

// e.g. "Running · KLGA (major hub) · 42 parked · 3 cleared · loading 5".
// "loading N" only while N > 0.
std::string statusLine(const Status& s);

// Short form for the window header: "KLAX · 42 parked", "KLAX · loading 5",
// "KSMO · stopped", "stopped", "waiting for flight".
std::string summary(const Status& s);

struct Problems {
    bool        xplaneTooOld      = false;   // older than 11.50
    bool        cslScanDone       = false;
    int         cslModels         = 0;
    bool        noEligibleStands  = false;   // active airport has no airline stands
    std::string airportIcao;
    bool        vatsimMode        = false;
    bool        xpilotMissing     = false;   // VATSIM mode but xPilot's dataref not found
};

// One sentence each: what is wrong and how to fix it. Most severe first.
std::vector<std::string> banners(const Problems& p);

} // namespace UiText
