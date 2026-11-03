// XP-GSI — Gate Static Injector for X-Plane
// XPluginStart / Stop / Enable / Disable / ReceiveMessage entry points, the
// flight loop, and the glue between scans, planner, renderer and window.
//
// SDK API sources verified against vendor/XPSDK/CHeaders/XPLM:
//   XPLMPlugin.h       — XPluginStart, XPLMGetMyID, XPLMGetPluginInfo,
//                        XPLMGetVersions, PLUGIN_API (XPLM200);
//                        XPLM_MSG_PLANE_LOADED 102 (param = plane index, 0 = user),
//                        XPLM_MSG_AIRPORT_LOADED 103, XPLM_MSG_SCENERY_LOADED 104,
//                        XPLM_MSG_ENTERED_VR 109 / XPLM_MSG_EXITING_VR 110 (XPLM301)
//   XPLMProcessing.h   — XPLMCreateFlightLoop, XPLMScheduleFlightLoop,
//                        XPLMDestroyFlightLoop, XPLMCreateFlightLoop_t (XPLM300),
//                        XPLMGetElapsedTime (XPLM200)
//   XPLMMenus.h        — XPLMFindPluginsMenu, XPLMAppendMenuItem,
//                        XPLMCreateMenu, XPLMDestroyMenu (XPLM200)
//   XPLMUtilities.h    — XPLMGetSystemPath, XPLMDebugString, XPLMCreateCommand,
//                        XPLMRegisterCommandHandler / XPLMUnregisterCommandHandler (XPLM210)
//   XPLMDataAccess.h   — XPLMFindDataRef, XPLMGetDatad / XPLMGetDatai / XPLMGetDataf,
//                        XPLMGetDataRefTypes (XPLM200)
// Datarefs:
//   sim/flightmodel/position/latitude|longitude (double) — user position
//   sim/graphics/VR/enabled (int)                       — DataRefs.txt of the XP11 install
//   xpilot/login/status (int: 0 disconnected, 1 pilot, 2 observer) — xPilot plugin
//     source plugin/src/xpilot.cpp (XPilot::ProcessPacket); name also present in
//     the installed xPilot.xpl. Read via XPLMGetDataRefTypes, not assumed.
//   TCAS targets, read only (DataRefs.txt of the XP11 install; X-Plane 11.50+):
//     sim/cockpit2/tcas/indicators/tcas_num_acf (int), sim/operation/override/override_TCAS
//     (int), sim/cockpit2/tcas/targets/position/x|y|z (float[64], local metres),
//     .../psi (float[64], true heading), .../V_msc (float[64], m/s),
//     .../weight_on_wheels (int[64]). Index 0 is the user's aircraft.
//     xPilot fills them through XPMP2 (AIMultiplayer.cpp AIUpdateTCASTargets,
//     consecutive slots from 1) while it holds TCAS control (xpilot.cpp
//     TryGetTcasControl, unless TCAS is disabled in xPilot's settings). xPilot
//     never sets a transponder mode, so XPMP2's default Mode C applies and
//     parked aircraft are listed too (XPMPAircraft.h ShowAsAIPlane).
//   XPLMLocalToWorld (XPLMGraphics.h, XPLM200)

#include <XPLMDataAccess.h>
#include <XPLMGraphics.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>
#include <algorithm>
#include <cinttypes>
#include <fstream>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "app/ArrivalGate.h"
#include "core/Guard.h"
#include "core/Timing.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/WorkQueue.h"
#include "csl/CslDb.h"
#include "csl/CslScanner.h"
#include "csl/IcaoSize.h"
#include "csl/ModelSelector.h"
#include "csl/ObjGeometry.h"
#include "csl/ObjLightPatch.h"
#include "data/AirportData.h"
#include "data/AirportTypes.h"
#include "data/WikiFallback.h"
#include "engine/TerminalZones.h"
#include "csl/RelatedTypes.h"
#include "data/RegionalBrands.h"
#include "data/StandOverrides.h"
#include "engine/AutoStart.h"
#include "engine/ConflictEngine.h"
#include "engine/Density.h"
#include "engine/FollowUser.h"
#include "engine/GateOccupancy.h"
#include "engine/Planner.h"
#include "engine/StandEvidence.h"
#include "engine/StandLayout.h"
#include "engine/StandState.h"
#include "geo/Geo.h"
#include "renderer/CslDatarefs.h"
#include "renderer/StaticRenderer.h"
#include "scenery/AptDat.h"
#include "ui/GateWindow.h"
#include "ui/MainWindow.h"
#include "ui/UiText.h"

// ── Plugin identity ───────────────────────────────────────────────────────────
#ifndef XPGSI_VERSION_STR
#  define XPGSI_VERSION_STR "0.0.0-dev"
#endif

static constexpr char kPluginName[] = "XP-GSI";
static constexpr char kPluginSig[]  = "xpgsi.plugin";
static constexpr char kPluginDesc[] =
    "Gate Static Injector — populates airline gates with cold-and-dark statics.";

static constexpr int    kMinXplaneVersion = 11500;   // XP 11.50 (minimum version)
static constexpr int    kSpawnPerTick     = 3;       // statics added/removed per 1 Hz tick
static constexpr double kSaveDebounceSec  = 1.0;
static constexpr double kMarsPositionM    = 40.0;    // positions of one gate within this are interchangeable

// ── Module-level state ────────────────────────────────────────────────────────
static XPLMMenuID       gPluginMenu    = nullptr;
static XPLMFlightLoopID gFlightLoopId  = nullptr;
static WorkQueue        gWorkQueue;
static StaticRenderer   gRenderer;
static Settings         gSettings;
static MainWindow       gWindow;
static AutoStart        gAutoStart;
static int              gXplaneVersion = 0;

// Running = the user (or auto-start) wants statics; placement then happens
// once the flight is ready and the index has loaded.
static bool             gRunning        = false;
static bool             gScansStarted   = false;
static bool             gPlacementBusy  = false;   // a parse/selection task is in flight
static std::uint32_t    gPlacementGen   = 0;       // bumped by resetPlacement(); stale results dropped
static bool             gFirstTickDone  = false;
// Layout seed for this run (Planner): new on every XPluginEnable unless
// settings.json pins "fixed_seed", so each run shows a different layout.
static std::uint32_t    gSessionSeed = 1;
static std::string      gDataVersion = "unknown";   // data/VERSION.txt
static int              gPluginMenuIdx = -1;        // our item in the Plugins menu

// Plugin time per call (Advanced > Debug logging: a summary every kTimingLogSec).
static TickStats        gLoopStats;
static double           gTimingLoggedAt = 0.0;
constexpr double        kTimingLogSec = 60.0;

static void logTimingIfDue(double now) {
    if (Log::getLevel() != Log::Level::Debug || now - gTimingLoggedAt < kTimingLogSec) return;
    gTimingLoggedAt = now;
    const TickStats& d = ImGuiWindow::drawStats();
    Log::debug("Timing: flight loop (1 Hz) p50 %.3f / p95 %.3f / max %.3f ms over %zu; "
               "window draw (per frame while open) p50 %.3f / p95 %.3f / max %.3f ms over %zu",
               static_cast<double>(gLoopStats.percentile(0.5)), static_cast<double>(gLoopStats.percentile(0.95)),
               static_cast<double>(gLoopStats.maxEver()), gLoopStats.count(),
               static_cast<double>(d.percentile(0.5)), static_cast<double>(d.percentile(0.95)),
               static_cast<double>(d.maxEver()), d.count());
}

#if IBM
constexpr const char* kOsName = "Windows x64";
#elif APL
constexpr const char* kOsName = "macOS";
#else
constexpr const char* kOsName = "Linux x64";
#endif

// First line of data/VERSION.txt (the data build date); "unknown" if absent.
static std::string readDataVersion(const std::filesystem::path& file) {
    std::ifstream f(file);
    std::string v;
    if (f && std::getline(f, v)) {
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (!v.empty()) return v;
    }
    return "unknown";
}

// Sparse sceneries (OpenStreetMap stands): a scenery is too sparse below
// max(kSparseMinStands, daily arrivals / kArrivalsPerStand) airline stands, and
// OpenStreetMap is used only if it has at least twice as many. An OSM stand
// within kOsmDedupeM of any scenery stand is the same stand. The floor was 6;
// at 3 a small field with 3–5 marked stands keeps only those (2026-10-27:
// X-Plane 12's defaults, 69 → 33 airports still filled in). kArrivalsPerStand
// was 6: KFAT's default scenery (5 airline gates, 37 arrivals a day) got
// OpenStreetMap positions up to 160 m from any marked stand, aircraft on bare
// ramp; only a scenery with under half the stands it needs is filled in
// (VABB's 9 for ~300 arrivals still is).
constexpr double kSparseMinStands  = 3.0;
constexpr double kArrivalsPerStand = 12.0;
constexpr double kOsmDedupeM       = 30.0;

// Widebody (code E / F) share of the table's airline mix (Density::groundHours).
static float widebodyShare(const AirportTable& t) {
    double wide = 0.0, total = 0.0;
    for (const auto& [a, w] : t.airlines) {
        auto it = t.types.find(a);
        if (it == t.types.end()) continue;
        double sum = 0.0, big = 0.0;
        for (const auto& [type, tw] : it->second) {
            sum += tw;
            if (IcaoSize::lookup(type) >= 'E') big += tw;
        }
        if (sum <= 0.0) continue;
        wide  += w * big / sum;
        total += w;
    }
    return total > 0.0 ? static_cast<float>(wide / total) : 0.0f;
}

// Settings persistence (debounced): < 0 = clean.
static double           gSettingsDirtyAt = -1.0;

// Active airport (set when its plan is loaded).
struct ActiveAirport {
    std::string  icao;
    double       refLat = 0, refLon = 0;     // apt.dat index position
    std::string  type;                       // OurAirports type, "" if not listed
    std::string  dataDate;                   // adsb.lol table build date, "" = none (log only)
    SizeCategory category = SizeCategory::Regional;
    size_t       eligible = 0;               // passenger stands in the plan
    size_t       remote   = 0;               // of which remote (tie-down) stands
    size_t       cargo    = 0;               // cargo stands in the plan
    float        autoPassenger = Density::kNoDataDensity;   // automatic densities (Density.h)
    float        autoCargo     = Density::kCargoMin;
    float        parkedAtOnce  = -1.0f;      // Density::parkedAtOnce, < 0 = unknown
    bool         noEligibleStands = false;
};
static ActiveAirport    gAirport;

// Apt.dat index, set on the main thread after background scan completes.
static AptDat::AptIndex gAptIndex;
static bool             gIndexReady = false;

// CSL catalogue, set on the main thread after background CSL scan completes.
// Immutable once published; shared so worker-side model selection and the
// renderer can hold it without copying or racing a later rescan.
static std::shared_ptr<const CslDb>        gCslDb;
static bool                                gCslReady = false;
static std::shared_ptr<const AirportTypes> gAirportTypes;

// Per plan stand: cooldown expiry (XPLMGetElapsedTime); -1 = never cleared.
static std::vector<float> gCooldownUntil;

// Stands another aircraft (VATSIM / AI) has parked on: emptied, never refilled
// while this airport's plan lives.
static GateOccupancy::Tracker gGateTraffic;
// Arrival gate reservation (app/ArrivalGate) and its window (ui/GateWindow).
static ArrivalGate            gArrival;
static GateWindow             gGateWin;
static bool                   gTcasWarned = false;
static XPLMProbeRef           gTrafficProbe = nullptr;   // terrain below other aircraft
// A CSL reference point sits a few metres above the ground when parked
// (XPMP2 VERT_OFFSET; up to ~6 m for an A380): below this counts as on the ground.
static constexpr float        kTrafficGroundAglM = 12.0f;

// xPilot (VATSIM startup mode).
static XPLMDataRef  gXpilotStatusRef = nullptr;
static double       gXpilotNextLookup = 0.0;

// User position datarefs (XPLM200).
static XPLMDataRef gLatRef = nullptr;
static XPLMDataRef gLonRef = nullptr;
// sim/flightmodel/position/elevation (double, metres MSL) and
// sim/flightmodel/failures/onground_any (int, 1 = user on the ground):
// DataRefs.txt of the XP11 install.
static XPLMDataRef gElevRef     = nullptr;
static XPLMDataRef gOnGroundRef = nullptr;
static XPLMDataRef gPsiRef      = nullptr;
static XPLMDataRef gGsRef       = nullptr;

// Follow the user (engine/FollowUser.h).
static double           gNextFollowCheck   = 0.0;
static double           gLastAirportSwitch = -1e9;
static double           gReplanAt = -1.0;          // draw distance changed: re-place at this time

// Commands.
static XPLMCommandRef gCmdToggleWindow   = nullptr;
static XPLMCommandRef gCmdGateWindow     = nullptr;

// Menu item indices
static constexpr uintptr_t kMenuOpenWindow = 0;
static constexpr uintptr_t kMenuStart      = 1;
static constexpr uintptr_t kMenuStop       = 2;
static constexpr uintptr_t kMenuGate       = 3;

// ── Forward declarations ──────────────────────────────────────────────────────
static float FlightLoopCB(float, float, int, void*);
static void  MenuHandlerCB(void*, void*);
static int   CommandCB(XPLMCommandRef, XPLMCommandPhase, void*);
static void  ensureScans();
static void  startScan();
static void  startCslScan();
static void  placeAirport();
static void  resetPlacement();
static void  startRunning(bool manual, const char* why);
static void  stopRunning(bool manual, const char* why);
static void  applyTarget();
static void  conflictTick();
static void  markSettingsDirty();
static void  saveSettingsIfDue(bool force);
static int   xpilotStatus();
static MainWindow::Model windowModel();

// Run `fn` at the start of the next flight loop (before the flight model)
// instead of now. Window clicks, menu items and commands arrive after the
// flight loop; X-Plane 12 logs "called XPLMDestroyInstance ... during a
// post-flightloop callback. This is discouraged" (live XP12.4.4 Log.txt,
// 2026-10-28) when instances are created or destroyed there. A negative
// interval in XPLMScheduleFlightLoop counts flight loops: -1 = the next one
// (XPLMProcessing.h, XPLM210). Same behaviour in XP11, one frame later.
static void onNextFlightLoop(std::function<void()> fn) {
    gWorkQueue.postToMain(std::move(fn));
    if (gFlightLoopId) XPLMScheduleFlightLoop(gFlightLoopId, -1.0f, 1);
}

static XPLMCommandRef makeCommand(const char* name, const char* desc) {
    // XPLMCreateCommand / XPLMRegisterCommandHandler (XPLMUtilities.h, XPLM210):
    // handler runs before X-Plane's (inBefore = 1); refcon tells them apart.
    XPLMCommandRef c = XPLMCreateCommand(name, desc);
    XPLMRegisterCommandHandler(c, [](XPLMCommandRef r, XPLMCommandPhase ph, void* v) { return Guard::run("Command", 1, [&] { return CommandCB(r, ph, v); }); }, 1, nullptr);
    return c;
}

static void releaseCommand(XPLMCommandRef& c) {
    if (c) XPLMUnregisterCommandHandler(c, CommandCB, 1, nullptr);
    c = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
static int pluginStart(char* outName, char* outSig, char* outDesc) {
    std::strncpy(outName, kPluginName, 255);
    std::strncpy(outSig,  kPluginSig,  255);
    std::strncpy(outDesc, kPluginDesc, 255);

    // Resolve plugin root from our own .xpl file path.
    char xplPath[512] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, xplPath, nullptr, nullptr);
    Paths::init(xplPath);
    Paths::initXplaneRoot();

    int xplmVer = 0;
    XPLMHostApplicationID hostID = xplm_Host_Unknown;
    XPLMGetVersions(&gXplaneVersion, &xplmVer, &hostID);
    Paths::setXplaneVersion(gXplaneVersion);

    Log::init(Paths::logsDir() / "XP-GSI.log");
    Log::info("=== XP-GSI " XPGSI_VERSION_STR " ===");
    gDataVersion = readDataVersion(Paths::dataDir() / "VERSION.txt");
    Log::info("Data version %s", gDataVersion.c_str());
    Log::info("X-Plane version %d  XPLM version %d  OS %s", gXplaneVersion, xplmVer, kOsName);
    Log::info("Plugin root:  %s", Paths::pluginRoot().u8string().c_str());
    Log::info("X-Plane root: %s", Paths::xplaneRoot().u8string().c_str());

    // One line in X-Plane's own Log.txt so a successful load is visible there
    // too. XPLMDebugString (XPLMUtilities.h) has no version guard — base API.
    {
        char line[640];
        std::snprintf(line, sizeof(line),
                      "XP-GSI " XPGSI_VERSION_STR ": started, log at %s\n",
                      (Paths::logsDir() / "XP-GSI.log").u8string().c_str());
        XPLMDebugString(line);
    }

    gLatRef = XPLMFindDataRef("sim/flightmodel/position/latitude");
    gLonRef = XPLMFindDataRef("sim/flightmodel/position/longitude");
    gElevRef     = XPLMFindDataRef("sim/flightmodel/position/elevation");
    gOnGroundRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    // DataRefs.txt (XP11): psi float, true heading in degrees; groundspeed
    // float, m/s.
    gPsiRef      = XPLMFindDataRef("sim/flightmodel/position/psi");
    gGsRef       = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    if (!gLatRef || !gLonRef)
        Log::warn("XP-GSI: user position datarefs not found");

    // Plugins → XP-GSI → Open window / Start / Stop (XPLMMenus.h, XPLM200).
    const int subMenuIdx = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "XP-GSI", nullptr, 0);
    gPluginMenuIdx = subMenuIdx;
    gPluginMenu = XPLMCreateMenu("XP-GSI", XPLMFindPluginsMenu(), subMenuIdx, [](void* m, void* i) { Guard::run("Menu", [&] { MenuHandlerCB(m, i); }); }, nullptr);
    XPLMAppendMenuItem(gPluginMenu, "Open window", reinterpret_cast<void*>(kMenuOpenWindow), 0);
    XPLMAppendMenuItem(gPluginMenu, "Start",       reinterpret_cast<void*>(kMenuStart),      0);
    XPLMAppendMenuItem(gPluginMenu, "Stop",        reinterpret_cast<void*>(kMenuStop),       0);
    XPLMAppendMenuItem(gPluginMenu, "Reserve gate...", reinterpret_cast<void*>(kMenuGate), 0);

    gCmdToggleWindow   = makeCommand("xpgsi/toggle_window", "XP-GSI: open or close the XP-GSI window");
    gCmdGateWindow     = makeCommand("xpgsi/gate_window",   "XP-GSI: open or close the reserve gate window");
    gArrival.registerApi((Paths::dataDir() / "cdu_adapters.json").u8string());

    Log::info("XPluginStart complete.");
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
static void pluginStop() {
    Log::info("XPluginStop.");

    releaseCommand(gCmdToggleWindow);
    releaseCommand(gCmdGateWindow);
    gArrival.unregisterApi();
    gGateWin.destroy();

    saveSettingsIfDue(/*force=*/true);
    gWindow.destroy();
    if (gFlightLoopId) {
        XPLMDestroyFlightLoop(gFlightLoopId);
        gFlightLoopId = nullptr;
    }
    if (gPluginMenu) {
        XPLMDestroyMenu(gPluginMenu);
        gPluginMenu = nullptr;
    }
    // The "XP-GSI" entry in the Plugins menu too (XPLMRemoveMenuItem, XPLM210).
    if (gPluginMenuIdx >= 0) {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), gPluginMenuIdx);
        gPluginMenuIdx = -1;
    }

    // Destroy renderer instances before shutting down the work queue and logger.
    gRenderer.destroyAll();
    // Only after every instance is gone: unregister the CSL datarefs we own.
    CslDatarefs::shutdown();

    gWorkQueue.shutdown();
    gAptIndex.clear();
    gIndexReady = false;
    gCslDb.reset();
    gCslReady = false;
    gAirportTypes.reset();
    gCooldownUntil.clear();

    Log::info("XPluginStop complete.");
    Log::shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
static int pluginEnable() {
    Log::info("XPluginEnable.");

    gSettings       = Settings::load(Paths::settingsFile());
    Log::setLevel(gSettings.debugLog ? Log::Level::Debug : Log::Level::Info);
    gSettingsDirtyAt = -1.0;
    gRunning        = false;
    gPlacementBusy  = false;
    gFirstTickDone  = false;
    gAutoStart      = AutoStart{};
    gSessionSeed = gSettings.fixedSeed != 0 ? static_cast<std::uint32_t>(gSettings.fixedSeed)
                                            : Planner::newSessionSeed();
    Log::info("XP-GSI: layout seed %u%s", gSessionSeed,
              gSettings.fixedSeed != 0 ? " (fixed_seed from settings.json)"
                                       : " (new this run; set \"fixed_seed\" to reproduce)");

    if (!gWindow.isCreated()) {
        if (gSettings.windowGeometrySaved)
            gWindow.create("XP-GSI", gSettings.windowLeft, gSettings.windowTop,
                           gSettings.windowRight, gSettings.windowBottom, 320, 240, 900, 900);
        else
            gWindow.create("XP-GSI", 0, 0, 0, 0, 320, 240, 900, 900, 400, 470);
        gWindow.setHooks({
            windowModel,
            [] {
                onNextFlightLoop([] {
                    if (gRunning) stopRunning(true, "window"); else startRunning(true, "window");
                });
            },
            [](Density::Kind k, float v) {
                Density::set(gSettings, gAirport.icao, k, v);
                markSettingsDirty();
                onNextFlightLoop(applyTarget);
            },
            [](Density::Kind k) {
                Density::reset(gSettings, gAirport.icao, k);
                markSettingsDirty();
                onNextFlightLoop(applyTarget);
            },
            [](StartMode m) {
                gSettings.startMode = m;
                Log::info("XP-GSI: startup mode → %s", startModeToStr(m));
                markSettingsDirty();
                if (m != StartMode::Manual) ensureScans();
            },
            [](bool on) {
                gSettings.cargoEnabled = on;
                Log::info("XP-GSI: cargo aircraft on cargo stands %s", on ? "on" : "off");
                markSettingsDirty();
                onNextFlightLoop(applyTarget);   // cargo statics only; the passenger ones stay
            },
            [](int n) { gSettings.maxAircraft = n; markSettingsDirty(); onNextFlightLoop(applyTarget); },
            [](float radius, float cool, float dist) {
                gSettings.conflictRadiusM = radius;
                gSettings.cooldownSec     = cool;
                if (dist != gSettings.maxRenderDistKm) gReplanAt = XPLMGetElapsedTime() + 1.5;
                gSettings.maxRenderDistKm = dist;
                markSettingsDirty();
            },
            [] { gGateWin.open(true); },
            [](bool fixed) {
                // Keep the layout on screen now: fix today's seed (never 0, which means "new each run").
                gSettings.fixedSeed = fixed ? static_cast<int>(gSessionSeed != 0 ? gSessionSeed : 1u) : 0;
                Log::info("XP-GSI: layout seed %s", fixed ? "fixed" : "new every placement");
                markSettingsDirty();
            },
            [](bool on) {
                gSettings.debugLog = on;
                Log::setLevel(on ? Log::Level::Debug : Log::Level::Info);
                Log::info("XP-GSI: debug logging %s", on ? "on" : "off");
                markSettingsDirty();
            },
        });
    }
    if (!gGateWin.isCreated()) {
        if (gSettings.gateGeometrySaved)
            gGateWin.create("Reserve gate", gSettings.gateLeft, gSettings.gateTop,
                            gSettings.gateRight, gSettings.gateBottom, 320, 300, 800, 900);
        else
            gGateWin.create("Reserve gate", 0, 0, 0, 0, 300, 260, 800, 900, 320, 440);
        gGateWin.attach(&gArrival);
    }
    // In VR already? (sim/graphics/VR/enabled, int, DataRefs.txt)
    if (XPLMDataRef vr = XPLMFindDataRef("sim/graphics/VR/enabled")) {
        gWindow.setVr(XPLMGetDatai(vr) != 0);
        gGateWin.setVr(XPLMGetDatai(vr) != 0);
    }

    // Flight loop at 1 Hz; it only runs while a flight is loaded, never at the
    // main menu (observed: posted results wait until the flight appears).
    XPLMCreateFlightLoop_t flParams{};
    flParams.structSize   = static_cast<int>(sizeof(flParams));
    flParams.phase        = xplm_FlightLoop_Phase_BeforeFlightModel;
    flParams.callbackFunc = [](float a, float b, int n, void* r) { return Guard::run("FlightLoop", -1.0f, [&] { return FlightLoopCB(a, b, n, r); }); };
    flParams.refcon       = nullptr;
    gFlightLoopId = XPLMCreateFlightLoop(&flParams);
    XPLMScheduleFlightLoop(gFlightLoopId, 1.0f, 1);
    gArrival.enable();

    if (gXplaneVersion < kMinXplaneVersion)
        Log::warn("XP-GSI: X-Plane %d is older than 11.50 — XP-GSI will not run", gXplaneVersion);
    else if (!gSettings.enabled)
        Log::info("XP-GSI: disabled via settings.json (\"enabled\": false)");
    else if (gSettings.startMode != StartMode::Manual)
        ensureScans();   // background prep so the first flight is ready quickly
    Log::info("XP-GSI: startup mode = %s", startModeToStr(gSettings.startMode));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
static void pluginDisable() {
    Log::info("XPluginDisable.");

    if (gFlightLoopId) {
        XPLMDestroyFlightLoop(gFlightLoopId);
        gFlightLoopId = nullptr;
    }
    gArrival.disable();
    saveSettingsIfDue(/*force=*/true);
    gWindow.setVisible(false);
    gGateWin.close();

    // Destroy all instances now; any in-flight OBJ load callback will discard
    // itself via the generation counter.
    gRunning = false;
    resetPlacement();

    // Drain any pending scan results so they don't arrive after disable.
    gWorkQueue.drainMain();
}

// ─────────────────────────────────────────────────────────────────────────────
static void pluginReceiveMessage(XPLMPluginID from, int msg, void* param) {
    (void)from;
    const double now = XPLMGetElapsedTime();
    switch (msg) {
        case XPLM_MSG_PLANE_LOADED:
            if (reinterpret_cast<intptr_t>(param) != 0) break;   // 0 = user's plane
            [[fallthrough]];
        case XPLM_MSG_AIRPORT_LOADED:
            Log::info("XP-GSI: new flight / airport loaded — statics wait for scenery to settle");
            gAutoStart.onFlightLoading(now);
            gArrival.onNewFlight();
            resetPlacement();
            break;
        case XPLM_MSG_SCENERY_LOADED:
            gAutoStart.onSceneryLoaded(now);
            break;
#if defined(XPLM301)
        case XPLM_MSG_ENTERED_VR: gWindow.setVr(true);  gGateWin.setVr(true);  break;
        case XPLM_MSG_EXITING_VR: gWindow.setVr(false); gGateWin.setVr(false); break;
#endif
        default: break;
    }
}

// ── Follow the user ───────────────────────────────────────────────────────────
static bool userOnGround() {
    return gOnGroundRef && XPLMGetDatai(gOnGroundRef) != 0;
}

// Placement is allowed on the ground or below 10,000 ft MSL.
static bool lowEnoughToPlace() {
    if (userOnGround() || !gElevRef) return true;
    return XPLMGetDatad(gElevRef) * 3.28084 < FollowUser::kCeilingFt;
}

// Medium / large airports and major hubs: where an airline arrival goes.
// Without the type list (still loading) every airport counts.
static bool airlineAirport(const std::string& icao, const std::string& aptId = {}) {
    if (!gAirportTypes || gAirportTypes->size() == 0) return true;
    for (const std::string* code : {&icao, &aptId}) {
        if (code->empty()) continue;
        if (gAirportTypes->isHub(*code)) return true;
        const std::string t = gAirportTypes->typeOf(*code);
        if (t == "large_airport" || t == "medium_airport") return true;
    }
    return false;
}

// Nearest apt.dat airport (optionally airline airports only). Equirectangular
// distance for the scan (cheap over ~37 000 entries), haversine for the result.
static const AptDat::IndexEntry* nearestAirport(double lat, double lon, bool airlineOnly,
                                                std::string* icaoOut, double* distKmOut) {
    const double kx = std::cos(lat * 3.14159265358979323846 / 180.0);
    double best = 1e30;
    const AptDat::IndexEntry* bestEntry = nullptr;
    const std::string* bestIcao = nullptr;
    for (const auto& [icao, entry] : gAptIndex) {
        const double dy = entry.refLat - lat, dx = (entry.refLon - lon) * kx;
        const double d2 = dx * dx + dy * dy;
        if (d2 >= best || (airlineOnly && !airlineAirport(icao, entry.aptId))) continue;
        best = d2; bestEntry = &entry; bestIcao = &icao;
    }
    if (bestEntry) {
        if (icaoOut)   *icaoOut   = *bestIcao;
        if (distKmOut) *distKmOut = haversineKm(lat, lon, bestEntry->refLat, bestEntry->refLon);
    }
    return bestEntry;
}

// Every 5 s: below the ceiling, move to a nearer airline airport; above it,
// drop a plan left far behind. The next tick's placeAirport() fills the new one.
static void followUser(double now) {
    if (now < gNextFollowCheck || !gLatRef || !gLonRef || gAirport.icao.empty()) return;
    gNextFollowCheck = now + 5.0;
    const double lat = XPLMGetDatad(gLatRef), lon = XPLMGetDatad(gLonRef);
    FollowUser::Input in;
    in.lowEnough      = lowEnoughToPlace();
    in.onGround       = userOnGround();
    in.hasPlan        = gRenderer.hasPlan();
    in.activeKm       = haversineKm(lat, lon, gAirport.refLat, gAirport.refLon);
    in.sinceSwitchSec = now - gLastAirportSwitch;
    std::string icao;
    if (in.lowEnough && !in.onGround) {   // the index scan only when it can matter
        in.haveNearest     = nearestAirport(lat, lon, /*airlineOnly=*/true, &icao, &in.nearestKm) != nullptr;
        in.nearestIsActive = icao == gAirport.icao;
    }
    switch (FollowUser::decide(in)) {
        case FollowUser::Action::Cleanup:
            Log::info("XP-GSI: %.0f km from %s above %.0f ft — statics removed until the next descent",
                      in.activeKm, gAirport.icao.c_str(), FollowUser::kCeilingFt);
            resetPlacement();
            break;
        case FollowUser::Action::Switch:
            Log::info("XP-GSI: following the user — %s (%.0f km) is nearer than %s (%.0f km); re-planning",
                      icao.c_str(), in.nearestKm, gAirport.icao.c_str(), in.activeKm);
            gLastAirportSwitch = now;
            resetPlacement();
            break;
        case FollowUser::Action::None: break;
    }
}

// Night = sun below the horizon. sim/graphics/scenery/sun_pitch_degrees:
// float, degrees, read-only (DataRefs.txt, XP11 and XP12).
static bool isNight() {
    static XPLMDataRef ref = XPLMFindDataRef("sim/graphics/scenery/sun_pitch_degrees");
    return ref && XPLMGetDataf(ref) < 0.0f;
}

// ── Flight loop ───────────────────────────────────────────────────────────────
static float FlightLoopCB(float /*elapsedSinceLast*/, float /*timeSinceLastFlight*/,
                          int /*counter*/, void* /*refcon*/) {
    const ScopedTick timed(gLoopStats);
    gWorkQueue.drainMain();
    const double now = XPLMGetElapsedTime();
    logTimingIfDue(now);

    if (!gFirstTickDone) {
        gFirstTickDone = true;
        // A flight is loaded (the loop does not run at the main menu). Covers
        // plugin reloads mid-flight, when no load message arrives.
        if (!gAutoStart.flightLoaded()) gAutoStart.onFlightLoading(now);
    }

    if (gXplaneVersion >= kMinXplaneVersion && gSettings.enabled) {
        switch (gAutoStart.tick(gSettings.startMode, gRunning, now, xpilotStatus())) {
            case AutoStart::Action::Start:
                startRunning(false, gSettings.startMode == StartMode::VatsimConnected
                                        ? "xPilot connected to VATSIM" : "flight loaded");
                break;
            case AutoStart::Action::Stop:
                stopRunning(false, "xPilot disconnected from VATSIM");
                break;
            case AutoStart::Action::None: break;
        }
    }

    if (gRunning && gIndexReady && !gPlacementBusy) followUser(now);
    // Only with CSL models to draw: without a library (or before its scan
    // ends) the stub would only put invisible quads on the stands; the
    // window's banner says what is missing.
    if (gRunning && gIndexReady && gCslReady && gCslDb && !gCslDb->empty() && !gRenderer.hasPlan() && !gPlacementBusy &&
        !gAirport.noEligibleStands && gAutoStart.flightReady(now) && lowEnoughToPlace())
        placeAirport();

    if (gRunning) {
        applyTarget();
        gRenderer.setNight(isNight());
        conflictTick();   // before step(): never spawn onto an occupied stand
        gArrival.tick(now, gRenderer, gCooldownUntil, gSettings.cooldownSec);
        gRenderer.step(kSpawnPerTick);
        gRenderer.reposition();
    }
    gGateWin.tick(now);
    // Draw distance changed in the window: re-place once the value settles
    // (it only applies when a plan is made).
    if (gReplanAt > 0.0 && now >= gReplanAt) {
        gReplanAt = -1.0;
        Log::info("XP-GSI: draw distance %.0f km — re-placing statics",
                  static_cast<double>(gSettings.maxRenderDistKm));
        resetPlacement();
    }
    saveSettingsIfDue(false);
    return 1.0f;
}

// ── Running state ─────────────────────────────────────────────────────────────
static void startRunning(bool manual, const char* why) {
    if (manual) gAutoStart.onManualStart();
    if (gRunning) return;
    if (gXplaneVersion < kMinXplaneVersion) {
        Log::warn("XP-GSI: Start ignored — X-Plane 11.50 or newer is required");
        return;
    }
    Log::info("XP-GSI: started (%s)", why);
    gRunning = true;
    gAirport.noEligibleStands = false;
    ensureScans();
}

static void stopRunning(bool manual, const char* why) {
    if (manual) gAutoStart.onManualStop();
    if (!gRunning) return;
    Log::info("XP-GSI: stopped (%s)%s", why,
              manual && gSettings.startMode != StartMode::Manual
                  ? " — automatic start suppressed until the next flight loads" : "");
    gRunning = false;
    resetPlacement();
}

static void resetPlacement() {
    gRenderer.destroyAll();
    gCooldownUntil.clear();
    gGateTraffic.reset(0);
    gArrival.onPlan({}, {}, 0.0, 0.0);
    if (gTrafficProbe) {
        XPLMDestroyProbe(gTrafficProbe);   // XPLMScenery.h, XPLM200
        gTrafficProbe = nullptr;
    }
    gAirport = ActiveAirport{};
    // A parse already in flight is discarded when it lands (see placeAirport).
    ++gPlacementGen;
    gPlacementBusy = false;
}

// ── Scans ─────────────────────────────────────────────────────────────────────
static void ensureScans() {
    if (gScansStarted) return;
    gScansStarted = true;
    startScan();
    startCslScan();
    auto file = Paths::dataDir() / "airports_eligible.json";
    auto hubs = Paths::dataDir() / "major_hubs.json";
    gWorkQueue.post([file, hubs]() {
        auto types = std::make_shared<AirportTypes>();
        const bool ok    = types->load(file);
        const bool hubOk = types->loadHubs(hubs);
        gWorkQueue.postToMain([types, ok, hubOk, file, hubs]() {
            gAirportTypes = types;
            if (ok) Log::info("AirportTypes: %zu airports with an OurAirports size type", types->size());
            else    Log::warn("AirportTypes: could not read '%s' — every airport counts as small",
                              file.u8string().c_str());
            if (hubOk) Log::info("AirportTypes: %zu major hubs", types->hubCount());
            else       Log::warn("AirportTypes: could not read '%s' — no airport counts as a major hub",
                                 hubs.u8string().c_str());
        });
    });
}

static void startScan() {
    gIndexReady = false;
    auto xplaneRoot = Paths::xplaneRoot();
    auto cacheDir   = Paths::cacheDir();
    int  xpVer      = Paths::xplaneVersion();
    gWorkQueue.post([xplaneRoot, cacheDir, xpVer]() {
        const Stopwatch sw;
        auto index = AptDat::buildIndex(xplaneRoot, xpVer, cacheDir);
        const double ms = sw.ms();
        gWorkQueue.postToMain([idx = std::move(index), ms]() mutable {
            gAptIndex   = std::move(idx);
            gIndexReady = true;
            Log::info("AptDat: index ready — %zu airports (%.0f ms)", gAptIndex.size(), ms);
        });
    });
}

static void startCslScan() {
    auto xplaneRoot = Paths::xplaneRoot();
    auto overrides  = Paths::dataDir() / "csl_livery_overrides.json";
    gWorkQueue.post([xplaneRoot, overrides]() {
        const Stopwatch sw;
        CslDb scanned = CslScanner::scan(xplaneRoot);
        const double scanMs = sw.ms();
        const auto fixed = CslScanner::applyLiveryOverrides(scanned, overrides);
        Log::info("CslScanner: scan %.0f ms, livery rules %.0f ms", scanMs, sw.ms() - scanMs);
        const std::size_t retiredModels = ModelSelector::retireTypes(
            scanned, ModelSelector::loadRetiredTypes(overrides.parent_path() / "retired_types.json"));
        Log::info("CslScanner: %zu model(s) of types their airline has retired (retired_types.json)",
                  retiredModels);
        Log::info("CslScanner: %zu livery entr(ies) from %s applied (%zu model(s) changed); %zu skipped "
                  "because the texture differs from the audited one (another library version)",
                  fixed.matched, overrides.filename().u8string().c_str(), fixed.changed, fixed.stale);
        auto db = std::make_shared<const CslDb>(std::move(scanned));
        gWorkQueue.postToMain([db = std::move(db)]() mutable {
            gCslDb    = std::move(db);
            gCslReady = true;
            // A plan built before the catalogue arrived uses stubs: rebuild it.
            if (gRenderer.hasPlan() && !gCslDb->empty()) {
                Log::info("CslScanner: catalogue arrived after placement — re-planning %s",
                          gAirport.icao.c_str());
                resetPlacement();
            }
        });
    });
}

// ── Placement: nearest airport → seeded plan → renderer ──────────────────────
static void placeAirport() {
    if (!gLatRef || !gLonRef || gAptIndex.empty()) return;

    const double userLat = XPLMGetDatad(gLatRef);
    const double userLon = XPLMGetDatad(gLonRef);
    // In the air (approach, follow-the-user): airline airports only.
    double bestDist = 0;
    const AptDat::IndexEntry* bestEntry =
        nearestAirport(userLat, userLon, /*airlineOnly=*/!userOnGround(), nullptr, &bestDist);
    if (!bestEntry) return;
    Log::info("StaticRenderer: nearest airport %.1f km away — planning", bestDist);

    // A new layout every placement (new flight, airport or Start), not once
    // per X-Plane session — unless settings.json fixes the seed.
    if (gSettings.fixedSeed == 0) {
        gSessionSeed = Planner::newSessionSeed();
        Log::info("XP-GSI: layout seed %u for this placement", gSessionSeed);
    }

    gPlacementBusy = true;
    AptDat::IndexEntry                  entryCopy = *bestEntry;
    std::shared_ptr<const CslDb>        db        = gCslDb;   // may be null: CSL scan not finished
    std::shared_ptr<const AirportTypes> types     = gAirportTypes;
    std::filesystem::path               dataDir   = Paths::dataDir();
    std::filesystem::path               objCache  = Paths::cacheDir() / "obj";
    const std::uint32_t                 session   = gSessionSeed;
    const std::uint32_t                 gen       = gPlacementGen;
    const std::filesystem::path         xpRoot    = Paths::xplaneRoot();
    const int                           xpVer     = Paths::xplaneVersion();
    gWorkQueue.post([entryCopy, db, types, dataDir, objCache, session, gen, xpRoot, xpVer]() {
        const Stopwatch planWatch;
        auto airport = AptDat::parseAirport(entryCopy);
        // The pack that defines this airport failed to load in X-Plane (Log.txt):
        // X-Plane draws the default airport, so place by the Global Airports
        // stands instead (KATL: an XP12-only Nimbus pack in X-Plane 11).
        {
            const std::string pack = entryCopy.file.parent_path().parent_path().filename().u8string();
            std::ifstream     lf(xpRoot / "Log.txt", std::ios::binary);
            const std::string logText((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
            const auto broken = AptDat::brokenPacks(logText);
            if (broken.count(pack)) {
                const auto global = AptDat::globalAptPath(xpRoot, xpVer);
                if (auto alt = AptDat::findAirport(global, "Global Airports", airport.icao)) {
                    Log::warn("AptDat: %s — X-Plane could not load scenery pack '%s' (see Log.txt: "
                              "DSF canceled); it shows the default airport, so statics follow the "
                              "Global Airports stands", airport.icao.c_str(), pack.c_str());
                    airport = AptDat::parseAirport(*alt);
                } else {
                    Log::warn("AptDat: %s — scenery pack '%s' failed to load in X-Plane and the "
                              "airport is not in Global Airports; statics may not match the "
                              "visible ground markings", airport.icao.c_str(), pack.c_str());
                }
            }
        }
        if (const auto n = StandOverrides::apply(dataDir / "stand_overrides.json", airport.icao, airport.stands))
            Log::info("StandOverrides: %s — %zu stand(s) adjusted", airport.icao.c_str(), n);
        std::vector<Stand> allStands = airport.stands;   // every stand: gate-name matching

        std::vector<Stand> airlineStands;
        for (auto& s : airport.stands)
            if (s.op == OpType::Airline)
                airlineStands.push_back(std::move(s));

        // Real airline / type mix from the shipped adsb.lol table, if any —
        // under the airport's ICAO code, else its apt.dat ID.
        if (!airport.aptId.empty())
            Log::info("AptDat: %s is '%s' (%s) in the apt.dat — its data is looked up as %s",
                      airport.icao.c_str(), airport.aptId.c_str(), airport.name.c_str(), airport.icao.c_str());
        auto table = AirportData::loadFor(dataDir, airport.icao);
        if (!table && !airport.aptId.empty()) table = AirportData::loadFor(dataDir, airport.aptId);
        if (table && table->samples < AirportData::kMinSamples) {
            Log::info("AirportData: %s table has only %d ground stays (< %d) — not used",
                      airport.icao.c_str(), table->samples, AirportData::kMinSamples);
            table.reset();
        }
        if (table)
            Log::info("AirportData: %s — %d ground stays, %zu airlines",
                      airport.icao.c_str(), table->samples, table->airlines.size());
        // The scenery's stand uses and sizes, corrected from where aircraft
        // really parked (engine/StandEvidence).
        if (table && !table->parkCells.empty()) {
            const auto fix = StandEvidence::apply(allStands, table->parkCells);
            airlineStands.clear();
            for (const auto& s : allStands)
                if (s.op == OpType::Airline) airlineStands.push_back(s);
            if (fix.toAirline || fix.upsized || fix.notAirline)
                Log::info("StandEvidence: %s — from where aircraft really park: %zu stand(s) marked cargo / GA / "
                          "none are airline stands, %zu airline stand(s) given a larger size, %zu airline "
                          "stand(s) where mostly cargo / private aircraft park left empty",
                          airport.icao.c_str(), fix.toAirline, fix.upsized, fix.notAirline);
        }
        // Airline arrivals a day (Density::dailyArrivals): adsb.lol or the
        // Wikipedia infobox, whichever is higher.
        auto wiki = WikiFallback::loadEntry(dataDir / "wiki_airlines.json", airport.icao);
        if (!wiki && !airport.aptId.empty()) wiki = WikiFallback::loadEntry(dataDir / "wiki_airlines.json", airport.aptId);
        const float dailyArrivals = Density::dailyArrivals(table ? table->dailyArrivals : -1.0f,
                                                           wiki ? wiki->arrivalsEst : -1.0f,
                                                           wiki ? wiki->destinationScore : -1.0f);
        const std::string dataDate = table ? table->generated : std::string{};   // window footer
        // Thin or missing adsb.lol data: blend in the airlines Wikipedia lists
        // for the airport, with types from the operators' adsb.lol fleets.
        // The types each airline flies today (ModelSelector::FleetTypes).
        const auto fleets = WikiFallback::loadFleets(dataDir / "airline_fleets.json");
        ModelSelector::FleetTypes fleetTypes;
        if (fleets)
            for (const auto& [code, byCat] : fleets->types)
                for (const char* cat : {"all", "fleet"})
                    if (auto it = byCat.find(cat); it != byCat.end())
                        for (const auto& [type, w] : it->second)
                            if (w > 0.0f) fleetTypes[code].insert(type);
        if (!table || table->samples < WikiFallback::kFullTrustSamples) {
            if (wiki) {
                float wikiShare = 0.0f;
                table = WikiFallback::combine(std::move(table), &*wiki, fleets ? &*fleets : nullptr, &wikiShare,
                                              entryCopy.refLat, entryCopy.refLon);
                if (table && wikiShare > 0.0f) {
                    const std::size_t fromCsl =
                        db ? ModelSelector::fillTypesFromLiveries(*table, *db, fleets ? &fleetTypes : nullptr) : 0;
                    Log::info("WikiFallback: %s — %.0f%% of the airline mix from Wikipedia "
                              "(%zu airlines listed), %zu airlines in the plan mix, %zu typed from CSL liveries",
                              airport.icao.c_str(), wikiShare * 100.0f, wiki->airlines.size(),
                              table->airlines.size(), fromCsl);
                }
            }
        }
        // Nothing about this airport at all (YSWS, opened 2026; user,
        // 2026-10-28: Qatar and Lufthansa A380s there from the whole library):
        // the airlines of the nearest airport with data, without its
        // long-haul visitors and widebodies, and nothing tied to its ground
        // (parking, terminals, stands) or its traffic.
        if (!table && !wiki) {
            double km = 0.0;
            const std::string nb = WikiFallback::nearestWithData(dataDir / "wiki_airlines.json", entryCopy.refLat,
                                                                 entryCopy.refLon, airport.icao,
                                                                 WikiFallback::kNeighbourKm, &km);
            if (!nb.empty()) {
                auto nt = AirportData::loadFor(dataDir, nb);
                if (nt && nt->samples < AirportData::kMinSamples) nt.reset();
                const auto nw = WikiFallback::loadEntry(dataDir / "wiki_airlines.json", nb);
                if ((!nt || nt->samples < WikiFallback::kFullTrustSamples) && nw)
                    nt = WikiFallback::combine(std::move(nt), &*nw, fleets ? &*fleets : nullptr, nullptr, nw->lat, nw->lon);
                if (nt) {
                    nt->clusters.clear(); nt->zones.clear(); nt->anchors.clear(); nt->osmStands.clear();
                    nt->nonPaxCells.clear(); nt->parkCells.clear();
                    nt->onGround = -1.0f; nt->dailyArrivals = -1.0f;
                    for (const auto& [a, w] : ModelSelector::longHaulVisitors(*nt)) {
                        nt->airlines.erase(a);
                        nt->types.erase(a);
                    }
                    for (auto& [a, typeMix] : nt->types) {
                        std::map<std::string, float> narrow;
                        for (const auto& [t, w] : typeMix)
                            if (IcaoSize::lookup(t) < 'E') narrow[t] = w;
                        if (!narrow.empty()) typeMix = std::move(narrow);
                    }
                    Log::info("AirportData: no data for %s — the airlines of %s (%.0f km), without its long-haul "
                              "visitors and widebodies", airport.icao.c_str(), nb.c_str(), km);
                    nt->icao = airport.icao;
                    if (!nt->airlines.empty()) table = std::move(nt);
                }
            }
        }
        // Airline aircraft parked at a daytime moment (Density::parkedAtOnce):
        // the automatic density and the cap on rare airlines.
        const float parkedAtOnce = Density::parkedAtOnce(dailyArrivals, table ? widebodyShare(*table) : 0.0f);
        // No parking data: which airlines use which terminal, from the
        // hand-written assignments or the domestic / international rule.
        if (table) {
            if (const auto n = AirportData::applyTerminalAssignments(*table, dataDir / "terminal_assignments.json",
                                                                     airport.icao, dataDir / "terminal_airlines.json"))
                Log::info("AirportData: %s — terminal assignments for %zu OpenStreetMap terminal zone(s) "
                          "(no parking data here)", airport.icao.c_str(), n);
        }
        // A scenery with far too few stands for the airport (X-Plane's default
        // VABB: 9 airline stands, ~300 arrivals a day): add the OpenStreetMap
        // parking positions it lacks.
        // Airports without a table carry theirs in osm_stands.json.
        std::vector<OsmStand> osmStands = table ? table->osmStands : std::vector<OsmStand>{};
        if (osmStands.empty()) osmStands = AirportData::loadOsmStands(dataDir / "osm_stands.json", airport.icao);
        if (osmStands.empty() && !airport.aptId.empty())
            osmStands = AirportData::loadOsmStands(dataDir / "osm_stands.json", airport.aptId);
        if (table && !osmStands.empty()) {
            const double need = std::max(kSparseMinStands, static_cast<double>(dailyArrivals) / kArrivalsPerStand);
            const std::size_t have = airlineStands.size();
            if (static_cast<double>(have) < need && osmStands.size() >= 2 * std::max<std::size_t>(have, 1)) {
                std::size_t added = 0;
                for (const auto& o : osmStands) {
                    bool taken = false;
                    for (const auto& s : allStands) {
                        double e = 0, n = 0;
                        StandLayout::toLocal(o.lat, o.lon, s.lat, s.lon, e, n);
                        if (std::hypot(e, n) < kOsmDedupeM) { taken = true; break; }
                    }
                    if (taken) continue;
                    Stand s;
                    s.id         = "OSM " + (o.ref.empty() ? std::to_string(added + 1) : o.ref);
                    s.lat        = o.lat;
                    s.lon        = o.lon;
                    s.headingDeg = o.headingDeg;
                    s.type       = o.remote ? StandType::TieDown : StandType::Gate;
                    s.widthCode  = o.size;
                    s.op         = OpType::Airline;
                    allStands.push_back(s);
                    airlineStands.push_back(std::move(s));
                    ++added;
                }
                Log::info("OsmStands: %s — scenery has %zu airline stand(s) for %.0f arrivals a day; "
                          "added %zu OpenStreetMap parking position(s) (no ground markings there)",
                          airport.icao.c_str(), have, static_cast<double>(dailyArrivals), added);
            }
        }

        // One airline list on most of the airport's stands is no gate
        // information (ModelSelector::clearBlanketLists).
        if (const auto n = ModelSelector::clearBlanketLists(airlineStands))
            Log::info("ModelSelector: %s — one scenery airline list on %zu of %zu airline stands ignored there "
                      "(not gate-specific)", airport.icao.c_str(), n, airlineStands.size());

        // No passenger static on a cargo or private-jet apron (spots where
        // adsb.lol saw mostly such aircraft park).
        if (table && !table->nonPaxCells.empty()) {
            const std::size_t before = airlineStands.size();
            airlineStands.erase(std::remove_if(airlineStands.begin(), airlineStands.end(),
                                               [&](const Stand& s) { return AirportData::nearNonPax(*table, s.lat, s.lon); }),
                                airlineStands.end());
            if (airlineStands.size() != before)
                Log::info("AirportData: %s — %zu airline stand(s) skipped: on an apron where mostly cargo or "
                          "private aircraft park", airport.icao.c_str(), before - airlineStands.size());
        }

        // Fill order for this run (Planner); the renderer fills a prefix of it.
        // Stands airliners really use come first (Planner::rankStands usage weight).
        std::vector<Stand> ranked;
        ranked.reserve(airlineStands.size());
        const std::vector<int> paxStays = table ? StandEvidence::paxStaysPerStand(airlineStands, table->parkCells)
                                                : std::vector<int>{};
        for (size_t i : Planner::rankStands(airlineStands, session, table ? &paxStays : nullptr))
            ranked.push_back(airlineStands[i]);

        // Cargo stands (the scenery's 1301 "cargo"), filled from the airport's
        // cargo mix. Always planned, so the cargo switch and the cargo density
        // only change the cargo target (StaticRenderer::setTargets); merged
        // with the passenger stands in proportion for the layout pass.
        if (table && !table->cargoAirlines.empty() && db && !db->empty()) {
            std::vector<Stand> cargoStands;
            for (const auto& s : allStands)
                if (s.op == OpType::Cargo) cargoStands.push_back(s);
            std::vector<Stand> cargoRanked;
            for (size_t i : Planner::rankStands(cargoStands, session ^ 0xC4A60u))
                cargoRanked.push_back(cargoStands[i]);
            if (!cargoRanked.empty()) {
                std::vector<Stand> merged;
                const double np = static_cast<double>(ranked.size()), nc = static_cast<double>(cargoRanked.size());
                std::size_t i = 0, j = 0;
                while (i < ranked.size() || j < cargoRanked.size()) {
                    const bool takeCargo = i >= ranked.size() ||
                        (j < cargoRanked.size() && (j + 0.5) / nc < (i + 0.5) / std::max(np, 1.0));
                    merged.push_back(takeCargo ? cargoRanked[j++] : ranked[i++]);
                }
                Log::info("Cargo: %s — %zu cargo stand(s) for %.0f cargo arrivals a day (%zu cargo airlines)",
                          airport.icao.c_str(), cargoRanked.size(), static_cast<double>(table->cargoDaily),
                          table->cargoAirlines.size());
                ranked = std::move(merged);
            }
        }

        if (!table)
            Log::info("AirportData: no data table for %s — size-only model selection",
                      airport.icao.c_str());

        // Model selection, nose-gear measurement and layout for every stand,
        // here on the worker (pure logic + file reads, no XPLM calls).
        // Stands are taken in fill order; a stand joins the plan only if its
        // aircraft keeps clear of every aircraft already planned and its gate
        // is not already used (StandLayout). Too big → retry one size class
        // smaller; still no room → the stand stays empty.
        struct Geo { ObjGeometry::Extents ext; float noseFwd; };
        std::unordered_map<const CslVariant*, Geo> geo;   // once per distinct model
        auto geoFor = [&](const CslVariant* v) -> const Geo& {
            auto it = geo.find(v);
            if (it == geo.end()) {
                auto ext = ObjGeometry::measure(v->objPaths);
                it = geo.emplace(v, Geo{ext, ObjGeometry::noseGearForwardM(ext)}).first;
            }
            return it->second;
        };
        const bool haveCsl = db && !db->empty();

        std::vector<Stand>      plan;
        std::vector<StandModel> models;
        StandLayout::Checker    layout;
        ModelSelector::VariantSet usedSpecials;   // one aircraft per special livery
        size_t sameGate = 0, overlap = 0, noModel = 0, downsized = 0, areaStands = 0, zoneStands = 0;
        int    byReason[6] = {};
        // Which terminal each stand serves and which airlines park there.
        const TerminalZones::Zones zones = table ? TerminalZones::Zones::build(*table, ranked)
                                                 : TerminalZones::Zones{};
        if (!zones.empty())
            Log::info("TerminalZones: %s — %zu %s", airport.icao.c_str(), zones.zoneCount(),
                      zones.fromOsm() ? "terminal zone(s) from OpenStreetMap"
                                      : "stand group(s) (no OpenStreetMap terminals)");
        // Look-alike types for ones the CSL library lacks; regionals in their
        // partners' paint.
        const auto related = RelatedTypes::load(dataDir / "related_types.json");
        const auto brands  = RegionalBrands::load(dataDir / "regional_brands.json");
        ModelSelector::Extras extras;
        extras.zones   = &zones;
        extras.related = related ? &*related : nullptr;
        extras.retiredTypes = ModelSelector::loadRetiredTypes(dataDir / "retired_types.json");
        extras.longestRunwayM = airport.longestRunwayM;
        if (airport.longestRunwayM > 0)
            Log::info("AptDat: %s — longest paved runway %.0f m (types needing more are not placed)",
                      airport.icao.c_str(), airport.longestRunwayM);
        if (brands && table) {
            extras.brands        = &*brands;
            extras.brandPresence = brands->presence(wiki ? &*wiki : nullptr, *table);
            extras.airportPartners = brands->partnersAt(airport.icao);
        }
        // Gate of each stand, over all the airport's stand names (lettered
        // alternatives of a plain-numbered stand share its gate). Keyed by
        // name and position: a name can stand for several stands.
        auto standKey = [](const Stand& st) {
            char pos[64];
            std::snprintf(pos, sizeof(pos), "@%.7f,%.7f", st.lat, st.lon);
            return st.id + pos;
        };
        std::unordered_map<std::string, std::string> gateOf;
        {
            std::vector<std::string> names;
            for (const auto& st : allStands) names.push_back(st.id);
            const auto keys = StandLayout::gateKeys(names);
            for (std::size_t i = 0; i < names.size(); ++i) gateOf[standKey(allStands[i])] = keys[i];
        }
        auto gateFor = [&](const Stand& st) {
            const auto gk = gateOf.find(standKey(st));
            return gk != gateOf.end() ? gk->second : StandLayout::gateKey(st.id);
        };
        // Widebody stands kept for long-haul visitors (ModelSelector::
        // visitorStands), where they park if the data says so; moved to the
        // front of the fill order so they show at any density.
        const auto visitors = table ? ModelSelector::longHaulVisitors(*table) : std::map<std::string, float>{};
        std::unordered_set<std::string> visitorKeys;
        if (const std::size_t want = ModelSelector::visitorStands(visitors, dailyArrivals); want > 0 && haveCsl) {
            // Only stands where those airlines really park: never a widebody
            // stand elsewhere (Cathay at KORD's United T1
            // satellite, Royal Jordanian at United's terminal).
            std::vector<std::size_t> preferred;
            for (std::size_t i = 0; i < ranked.size(); ++i) {
                const Stand& s = ranked[i];
                if (s.op != OpType::Airline || s.widthCode < 'E' || s.widthCode > 'F' ||
                    !s.allowedTypes.empty() || !s.brand.empty() || s.airlinesOverridden ||
                    !zones.visitorsWelcome(zones.zoneOf(s.lat, s.lon), visitors))
                    continue;
                float seen = 0.0f;
                for (const auto& [a, n] : ModelSelector::areaMix(s, *table, &zones).airlines)
                    if (visitors.count(a)) seen += n;
                if (seen >= ModelSelector::kVisitorMinEvidence) preferred.push_back(i);
            }
            std::vector<Stand> front, rest;
            std::unordered_set<std::size_t> chosen;
            for (std::size_t k = 0; k < preferred.size() && chosen.size() < want; ++k) chosen.insert(preferred[k]);
            for (std::size_t i = 0; i < ranked.size(); ++i) {
                if (chosen.count(i)) { visitorKeys.insert(standKey(ranked[i])); front.push_back(ranked[i]); }
                else rest.push_back(ranked[i]);
            }
            front.insert(front.end(), rest.begin(), rest.end());
            ranked = std::move(front);
            Log::info("ModelSelector: %s — %zu long-haul visitor airline(s), ~%.1f arrivals a day: %zu widebody "
                      "stand(s) kept for them", airport.icao.c_str(), visitors.size(),
                      static_cast<double>([&] { float s = 0; for (const auto& [a, w] : visitors) s += w; return s; }() *
                                          dailyArrivals),
                      visitorKeys.size());
        }
        extras.visitors = visitors.empty() ? nullptr : &visitors;

        // A gate's positions together, largest first: the first one tried
        // takes the gate (StandLayout::largestFirstPerGate).
        {
            std::vector<std::size_t> order(ranked.size());
            std::vector<std::string> keys;
            std::vector<char>        sizes;
            for (std::size_t i = 0; i < ranked.size(); ++i) {
                order[i] = i;
                keys.push_back(gateFor(ranked[i]));
                sizes.push_back(ranked[i].widthCode);
            }
            std::vector<Stand> byGate;
            byGate.reserve(ranked.size());
            for (std::size_t i : StandLayout::largestFirstPerGate(order, keys, sizes)) byGate.push_back(ranked[i]);
            ranked = std::move(byGate);
        }
        // No airline shows more than its share of what is parked at a moment
        // allows (ModelSelector::kMaxShareFactor): stands are drawn one by
        // one, so a rare airline could otherwise come up at several
        // (three Etihad 787s at KORD's T5, four Singapore at VTBS).
        // Counted over the stands placed so far (ModelSelector::shareCap), as
        // the renderer shows a prefix of the plan; a rare airline never shows
        // more aircraft than are really parked here at once (rareAirlineCap).
        std::map<std::string, int> placedBy, cargoPlacedBy;
        std::set<std::string>      full, cargoFull, capped;
        std::size_t                paxPlaced = 0, cargoPlaced = 0;
        auto shareOf = [&](bool cargo) {
            std::map<std::string, float> out;
            if (!table) return out;
            const auto& w = cargo ? table->cargoAirlines : table->airlines;
            float total = 0.0f;
            for (const auto& [k, v] : w) total += v;
            if (total > 0.0f)
                for (const auto& [k, v] : w) out[k] = v / total;
            return out;
        };
        const auto paxShare = shareOf(false), cargoShare = shareOf(true);
        auto refreshFull = [&](bool cargo) {
            auto& f = cargo ? cargoFull : full;
            f.clear();
            const auto& share = cargo ? cargoShare : paxShare;
            for (const auto& [a, n] : cargo ? cargoPlacedBy : placedBy) {
                const auto it = share.find(a);
                if (it != share.end() && n >= ModelSelector::shareCap(it->second, cargo ? cargoPlaced : paxPlaced))
                    f.insert(a);
            }
        };
        extras.capped = &capped;
        // Each gate's positions (MARS: one widebody and two narrowbody lead-in
        // lines), for moving a smaller aircraft to the position made for it.
        std::map<std::string, std::vector<Stand>> gatePositions;
        for (const auto& s : ranked) gatePositions[gateFor(s)].push_back(s);
        for (const auto& s : ranked) {
            refreshFull(s.op == OpType::Cargo);
            const auto seed = Planner::standSeed(s, session);   // differs every run
            double east = 0, north = 0;
            StandLayout::toLocal(s.lat, s.lon, entryCopy.refLat, entryCopy.refLon, east, north);
            const std::string gate = gateFor(s);
            extras.visitorsOnly = visitorKeys.count(standKey(s)) > 0;
            extras.full = s.op == OpType::Cargo ? &cargoFull : &full;
            std::string chosen;

            Stand trial = s;
            bool  done = false, accepted = false;
            auto  lastVerdict = StandLayout::Checker::Verdict::Fits;
            while (!done) {
                StandModel m;
                m.lights = !Planner::litAtNight(seed) ? LightMode::Dark
                         : Planner::logoAtNight(seed) ? LightMode::NavLogo : LightMode::NavOnly;
                ModelSelector::Choice::Reason reason = ModelSelector::Choice::Reason::NoTableMatch;
                bool fromArea = false, fromZone = false;
                if (haveCsl && table && s.op == OpType::Cargo) {
                    auto c = ModelSelector::selectCargo(trial, *db, *table, seed, &extras);
                    m.model = c.model;
                    reason  = c.reason;
                    chosen  = c.airline;
                } else if (haveCsl && table) {
                    auto c = ModelSelector::selectWithTable(trial, *db, *table, seed, &usedSpecials, &extras);
                    m.model  = c.model;
                    reason   = c.reason;
                    chosen   = c.airline;
                    fromArea = c.fromArea;
                    fromZone = c.fromZone;
                } else if (haveCsl) {
                    m.model = ModelSelector::selectForStand(trial, *db, seed, &usedSpecials,
                                                            fleets ? &fleetTypes : nullptr);
                }
                StandLayout::Shape shape = StandLayout::stubShape();
                if (m.model) {
                    const Geo& g   = geoFor(m.model);
                    m.noseGearFwdM = g.noseFwd;
                    shape          = StandLayout::shapeFor(g.ext, g.noseFwd);
                }
                if (haveCsl && !m.model) {
                    lastVerdict = StandLayout::Checker::Verdict::Fits;   // nothing selectable
                } else {
                    // A smaller aircraft than the position is drawn on the
                    // gate's best-fitting position (a CRJ on
                    // CYEG's widebody lead-in 76A, angled in beside gate 74,
                    // instead of 76): the smallest one still at least its size.
                    Stand at = s;
                    const char size = m.model ? m.model->sizeCode : '?';
                    if (size >= 'A' && size <= 'F' && size < s.widthCode) {
                        for (const auto& p : gatePositions[gate]) {
                            if (p.id == s.id || p.widthCode < size || p.widthCode >= at.widthCode) continue;
                            double pe = 0, pn = 0;
                            StandLayout::toLocal(p.lat, p.lon, entryCopy.refLat, entryCopy.refLon, pe, pn);
                            if (std::hypot(pe - east, pn - north) <= kMarsPositionM) at = p;
                        }
                    }
                    double atE = east, atN = north;
                    if (at.id != s.id)
                        StandLayout::toLocal(at.lat, at.lon, entryCopy.refLat, entryCopy.refLon, atE, atN);
                    auto fp = StandLayout::place(shape, atE, atN, at.headingDeg);
                    lastVerdict = layout.check(fp, gate, atE, atN);
                    if (lastVerdict == StandLayout::Checker::Verdict::Fits) {
                        layout.add(fp, gate, atE, atN);
                        plan.push_back(at);
                        m.shape = shape;
                        models.push_back(m);
                        if (m.model && m.model->special) usedSpecials.insert(m.model);
                        if (trial.widthCode != s.widthCode) ++downsized;
                        if (table) ++byReason[static_cast<int>(reason)];
                        if (fromArea) ++areaStands;
                        if (fromZone) ++zoneStands;
                        if (table && m.model && !chosen.empty()) {
                            const bool cargo = s.op == OpType::Cargo;
                            const int count = ++(cargo ? cargoPlacedBy : placedBy)[chosen];
                            if (!cargo) {
                                const auto it = paxShare.find(chosen);
                                const int hard = it != paxShare.end()
                                                     ? ModelSelector::rareAirlineCap(it->second, parkedAtOnce) : -1;
                                if (hard > 0 && count >= hard) capped.insert(chosen);
                            }
                        }
                        if (m.model) ++(s.op == OpType::Cargo ? cargoPlaced : paxPlaced);
                        accepted = true;
                        break;
                    }
                    if (lastVerdict == StandLayout::Checker::Verdict::SameGate) break;  // smaller won't help
                }
                // Try one size class smaller (airliners start at B).
                const char cur = (trial.widthCode >= 'A' && trial.widthCode <= 'F') ? trial.widthCode : 'C';
                if (cur <= 'B') done = true;
                else trial.widthCode = static_cast<char>(cur - 1);
            }
            if (!accepted) {
                if (lastVerdict == StandLayout::Checker::Verdict::SameGate) ++sameGate;
                else if (lastVerdict == StandLayout::Checker::Verdict::Overlap) ++overlap;
                else ++noModel;
            }
        }
        // Logo lights follow the nav switch only in XP-GSI's patched OBJ
        // copies (ObjLightPatch); once per distinct model.
        std::unordered_map<const CslVariant*, std::vector<std::filesystem::path>> parts;
        size_t patchedModels = 0, litStands = 0, navOnly = 0;
        for (auto& m : models) {
            if (m.lights != LightMode::Dark)    ++litStands;
            if (m.lights == LightMode::NavOnly) ++navOnly;
            if (!m.model) continue;
            auto it = parts.find(m.model);
            if (it == parts.end()) {
                std::vector<std::filesystem::path> p;
                bool any = false;
                for (const auto& obj : m.model->objPaths) {
                    p.push_back(ObjLightPatch::patchedCopy(obj, objCache));
                    any = any || p.back() != obj;
                }
                if (any) ++patchedModels;
                it = parts.emplace(m.model, std::move(p)).first;
            }
            m.parts = it->second;
        }
        Log::info("ObjLightPatch: %zu of %zu model(s) patched for a separate logo-light switch; "
                  "%zu of %zu static(s) lit (nav lights day and night; %zu of them without logo lights at night)",
                  patchedModels, parts.size(), litStands, models.size(), navOnly);

        size_t measured = 0;
        for (const auto& [v, g] : geo) if (ObjGeometry::noseGearPlausible(g.ext)) ++measured;
        if (!geo.empty())
            Log::info("StaticRenderer: nose gear measured for %zu of %zu model(s), rest estimated from length",
                      measured, geo.size());
        Log::info("StandLayout: %s — %zu airline stands → %zu usable (%zu share a gate already used, "
                  "%zu no room even for a smaller type, %zu no fitting model; %zu downsized to fit)",
                  airport.icao.c_str(), ranked.size(), plan.size(), sameGate, overlap, noModel, downsized);
        if (table)
            Log::info("ModelSelector: %s — exact livery %d, airline on other type %d, "
                      "generic livery %d, other livery %d, no table match %d, regional in partner paint %d; "
                      "of %zu stand(s), %zu used where airlines actually park nearby "
                      "(%zu parking cluster(s)), %zu what parks at their terminal, the rest the "
                      "airport-wide mix without airlines based at another terminal",
                      airport.icao.c_str(), byReason[0], byReason[1], byReason[2],
                      byReason[3], byReason[4], byReason[5], plan.size(), areaStands, table->clusters.size(),
                      zoneStands);

        std::string type = types ? types->typeOf(airport.icao) : std::string{};
        if (type.empty() && types && !airport.aptId.empty()) type = types->typeOf(airport.aptId);
        const bool  hub  = types && (types->isHub(airport.icao) || (!airport.aptId.empty() && types->isHub(airport.aptId)));
        const float cargoDaily = table ? static_cast<float>(table->cargoDaily) : 0.0f;
        Log::info("Planner: %s planned on the worker in %.0f ms", airport.icao.c_str(), planWatch.ms());
        gWorkQueue.postToMain([icao = airport.icao, type, hub, dailyArrivals, parkedAtOnce, cargoDaily, dataDate,
                               refLat = entryCopy.refLat,
                               refLon = entryCopy.refLon, allStands = std::move(allStands),
                               ranked = std::move(plan),
                               models = std::move(models), db, session, gen]() mutable {
            // Stopped, or a new flight/airport started, while this was parsing.
            if (gen != gPlacementGen || session != gSessionSeed) return;
            gPlacementBusy = false;
            if (!gRunning || gRenderer.hasPlan()) return;
            // The CSL catalogue may have been published while this airport was
            // parsed: plan again against it rather than place stubs.
            if (gCslDb && !gCslDb->empty() && db != gCslDb) {
                Log::info("StaticRenderer: CSL catalogue arrived during planning — re-planning %s",
                          icao.c_str());
                return;   // the flight loop calls placeAirport() again next tick
            }

            gAirport.icao     = icao;
            gAirport.refLat   = refLat;
            gAirport.refLon   = refLon;
            gAirport.type     = type;
            gAirport.dataDate = dataDate;
            gAirport.category = Density::categoryFor(hub, dailyArrivals);
            gAirport.cargo    = static_cast<size_t>(
                std::count_if(ranked.begin(), ranked.end(), [](const Stand& s) { return s.op == OpType::Cargo; }));
            gAirport.eligible = ranked.size() - gAirport.cargo;
            gAirport.remote   = static_cast<size_t>(std::count_if(ranked.begin(), ranked.end(), [](const Stand& s) {
                return s.op != OpType::Cargo && Planner::isRemote(s);
            }));
            gAirport.autoPassenger = Density::autoPassenger(
                parkedAtOnce, Density::weightedStands(gAirport.eligible - gAirport.remote, gAirport.remote));
            gAirport.parkedAtOnce  = parkedAtOnce;
            gAirport.autoCargo     = Density::autoCargo(cargoDaily, gAirport.cargo);
            gAirport.noEligibleStands = ranked.empty();
            Log::info("Planner: %s (%s, %.1f airline arrivals a day%s) — %zu passenger stands, %zu of them remote; "
                      "%zu cargo stands for %.0f cargo arrivals a day; ~%.0f airline aircraft parked at once "
                      "by day%s; automatic density %.0f %% passenger, %.0f %% cargo",
                      icao.c_str(), Density::name(gAirport.category),
                      static_cast<double>(std::max(dailyArrivals, 0.0f)),
                      dailyArrivals < 0.0f ? ", no traffic data" : "", gAirport.eligible, gAirport.remote,
                      gAirport.cargo, static_cast<double>(cargoDaily),
                      static_cast<double>(std::max(parkedAtOnce, 0.0f)),
                      parkedAtOnce < 0.0f ? " (unknown: default density)" : "",
                      static_cast<double>(gAirport.autoPassenger * 100.0f),
                      static_cast<double>(gAirport.autoCargo * 100.0f));
            if (ranked.empty()) {
                Log::info("StaticRenderer: no airline stands at %s — nothing to place", icao.c_str());
                return;
            }

            // Distance culling keeps the plan order (stands are dropped, not reordered).
            // Only on the ground: a plan made on approach (follow the user) is
            // made from tens of km out and would lose every stand.
            const float maxDist = gSettings.maxRenderDistKm;
            if (maxDist > 0.0f && gLatRef && gLonRef && userOnGround()) {
                const double lat = XPLMGetDatad(gLatRef), lon = XPLMGetDatad(gLonRef);
                std::vector<Stand>      keptStands;
                std::vector<StandModel> keptModels;
                for (size_t i = 0; i < ranked.size(); ++i)
                    if (haversineKm(lat, lon, ranked[i].lat, ranked[i].lon) <= static_cast<double>(maxDist)) {
                        keptStands.push_back(std::move(ranked[i]));
                        keptModels.push_back(models[i]);
                    }
                if (keptStands.size() < ranked.size())
                    Log::info("StaticRenderer: culled %zu stands beyond %.0f km",
                              ranked.size() - keptStands.size(), static_cast<double>(maxDist));
                ranked = std::move(keptStands);
                models = std::move(keptModels);
            }

            gCooldownUntil.assign(ranked.size(), -1.0f);
            gGateTraffic.reset(ranked.size());
            std::vector<StandLayout::Shape> shapes;
            for (const auto& m : models) shapes.push_back(m.shape);
            gGateTraffic.setShapes(shapes);
            gArrival.onPlan(std::move(allStands), std::move(shapes), refLat, refLon);
            auto objAbs  = Paths::pluginRoot() / "data" / "models" / "static_stub.obj";
            auto stubRel = std::filesystem::relative(objAbs, Paths::xplaneRoot());
            gRenderer.loadPlan(std::move(ranked), std::move(models), std::move(db),
                               Paths::xplaneRoot(), stubRel.generic_u8string());
            applyTarget();
        });
    });
}

// Passenger target = passenger stands (remote ones weighted, Density.h) ×
// the airport's passenger density; cargo target = cargo stands × its cargo
// density, 0 with cargo switched off. Max aircraft caps both together,
// passenger statics first.
static void applyTarget() {
    if (!gRenderer.hasPlan()) return;
    const float  dp    = Density::get(gSettings, gAirport.icao, Density::Kind::Passenger, gAirport.autoPassenger);
    const float  dc    = Density::get(gSettings, gAirport.icao, Density::Kind::Cargo, gAirport.autoCargo);
    const double count = Density::weightedStands(gAirport.eligible - gAirport.remote, gAirport.remote);
    const std::size_t pax = Density::target(count, gAirport.eligible, dp, gSettings.maxAircraft);
    const int left = gSettings.maxAircraft < 0 ? -1 : std::max(0, gSettings.maxAircraft - static_cast<int>(pax));
    const std::size_t cargo = gSettings.cargoEnabled ? Density::target(gAirport.cargo, dc, left) : 0;
    gRenderer.setTargets(pax, cargo);
}

// ── Other aircraft from X-Plane's TCAS target list (read only) ────────────────
static std::vector<GateOccupancy::Traffic> readTraffic() {
    struct Refs { XPLMDataRef num, over, id, fid, type, x, y, z, psi, wow; };
    static const Refs r{
        XPLMFindDataRef("sim/cockpit2/tcas/indicators/tcas_num_acf"),
        XPLMFindDataRef("sim/operation/override/override_TCAS"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/modeS_id"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/flight_id"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/icao_type"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/x"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/y"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/z"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/psi"),
        XPLMFindDataRef("sim/cockpit2/tcas/targets/position/weight_on_wheels"),
    };
    // Speed from our own position samples per airframe (modeS_id): XPMP2
    // refreshes the TCAS velocity arrays only now and then, so V_msc can be stale.
    struct Last { double lat, lon, t; };
    static std::unordered_map<int, Last> last;
    std::vector<GateOccupancy::Traffic> out;
    if (!r.num || !r.id || !r.x || !r.y || !r.z || !r.psi || !r.wow) return out;
    const double now = XPLMGetElapsedTime();

    // xPilot connected but not feeding TCAS: its aircraft cannot be seen.
    if (!gTcasWarned && xpilotStatus() == 1 && r.over && XPLMGetDatai(r.over) == 0) {
        gTcasWarned = true;
        Log::warn("Traffic: xPilot is connected but does not control TCAS (disabled in xPilot's "
                  "settings, or another plugin holds it) — VATSIM aircraft at gates cannot be "
                  "detected, statics stay");
    }

    // Every slot 1..63 with an airframe ID: tcas_num_acf counts written
    // targets, which "are not necessarily consecutive entries" (DataRefs.txt).
    constexpr int kSlots = 64;
    constexpr int n      = kSlots - 1;                 // slot 0 = user
    float x[kSlots] = {}, y[kSlots] = {}, z[kSlots] = {}, psi[kSlots] = {};
    int   id[kSlots] = {}, wow[kSlots] = {};
    char  fid[kSlots * 8] = {};
    // XPLMGetDatavf / XPLMGetDatavi / XPLMGetDatab (XPLMDataAccess.h): values from slot 1.
    XPLMGetDatavi(r.id,  id,  1, n);
    XPLMGetDatavf(r.x,   x,   1, n);
    XPLMGetDatavf(r.y,   y,   1, n);
    XPLMGetDatavf(r.z,   z,   1, n);
    XPLMGetDatavf(r.psi, psi, 1, n);
    XPLMGetDatavi(r.wow, wow, 1, n);
    char  type[kSlots * 8] = {};
    if (r.fid)  XPLMGetDatab(r.fid,  fid,  8, n * 8);  // 8 bytes per slot (byte[512])
    if (r.type) XPLMGetDatab(r.type, type, 8, n * 8);
    int slotsUsed = 0, onGround = 0;
    for (int i = 0; i < n; ++i) {
        if (id[i] == 0 || (x[i] == 0.0f && y[i] == 0.0f && z[i] == 0.0f)) continue;   // empty slot
        ++slotsUsed;
        GateOccupancy::Traffic t;
        double alt = 0;
        XPLMLocalToWorld(x[i], y[i], z[i], &t.lat, &t.lon, &alt);
        t.headingDeg = psi[i];
        // On the ground: weight_on_wheels reads 0 for xPilot's aircraft (YBBN
        // live test 2026-10-26: 16 targets, none on the ground), so also test
        // the height above the terrain below (XPLMProbeTerrainXYZ, XPLM200).
        if (!gTrafficProbe) gTrafficProbe = XPLMCreateProbe(xplm_ProbeY);
        XPLMProbeInfo_t info{};
        info.structSize = static_cast<int>(sizeof(info));
        const bool hit = XPLMProbeTerrainXYZ(gTrafficProbe, x[i], y[i], z[i], &info) == xplm_ProbeHitTerrain;
        t.heightAglM = hit ? y[i] - info.locationY : 1e9f;
        t.onGround   = wow[i] != 0 || t.heightAglM < kTrafficGroundAglM;
        if (t.onGround) ++onGround;
        t.callsign.assign(fid + i * 8, strnlen(fid + i * 8, 8));
        t.sizeCode = IcaoSize::lookup(std::string(type + i * 8, strnlen(type + i * 8, 8)));
        auto it = last.find(id[i]);
        const bool known = it != last.end() && now - it->second.t > 0.2 && now - it->second.t < 5.0;
        if (known)
            t.speedMs = static_cast<float>(haversineKm(t.lat, t.lon, it->second.lat, it->second.lon) *
                                           1000.0 / (now - it->second.t));
        if (it == last.end() || now - it->second.t > 0.2)
            last[id[i]] = Last{t.lat, t.lon, now};
        if (known) out.push_back(t);   // first sighting: speed unknown until the next tick
    }
    for (auto it = last.begin(); it != last.end();)
        it = (now - it->second.t > 30.0) ? last.erase(it) : std::next(it);

    // Diagnostics every 30 s while anything is in the list (or xPilot is on).
    static double nextReport = 0.0;
    if (now >= nextReport && (slotsUsed > 0 || xpilotStatus() == 1)) {
        nextReport = now + 30.0;
        Log::info("Traffic: %d TCAS target(s), %d on the ground (tcas_num_acf %d, override_TCAS %d, "
                  "xPilot status %d)", slotsUsed, onGround, XPLMGetDatai(r.num),
                  r.over ? XPLMGetDatai(r.over) : -1, xpilotStatus());
        int lines = 0;
        for (const auto& t : out) {
            if (lines >= 12) continue;
            // Nearest planned stand, and how it compares with the occupancy test.
            const Stand* best = nullptr;
            double bestD = 150.0;
            for (const auto& s : gRenderer.stands()) {
                const double d = haversineKm(t.lat, t.lon, s.lat, s.lon) * 1000.0;
                if (d < bestD) { bestD = d; best = &s; }
            }
            if (!best) continue;
            const auto rel = GateOccupancy::relative(*best, t);
            Log::info("Traffic:   %-8s (%c) near stand '%s' (%c): %.0f m ahead / %.0f m side, heading off "
                      "%.0f deg, %.1f m/s, %.1f m above ground%s → %s",
                      t.callsign.empty() ? "?" : t.callsign.c_str(), t.sizeCode,
                      best->id.c_str(), best->widthCode, rel.along, rel.across,
                      static_cast<double>(rel.headingDiff), static_cast<double>(t.speedMs),
                      static_cast<double>(t.heightAglM), t.onGround ? "" : " (airborne)",
                      GateOccupancy::occupies(*best, t) ? "occupies" : "does not occupy");
            ++lines;
        }
    }
    return out;
}

// ── Conflict engine tick ──────────────────────────────────────────────────────
static void conflictTick() {
    if (!gRenderer.hasPlan() || !gLatRef || !gLonRef) return;

    const float now = XPLMGetElapsedTime();
    ConflictEngine::LatLon user{XPLMGetDatad(gLatRef), XPLMGetDatad(gLonRef)};
    const size_t n = gRenderer.planSize();
    if (gCooldownUntil.size() != n) gCooldownUntil.assign(n, -1.0f);

    // Planned stands that may show a static: clear one the user is parked on
    // or pulling into (never a static on the user's aircraft). Only those:
    // taxiing past a stand never clears it (the old 60 m
    // radius cleared statics along the taxiway at KORD).
    const float cool = gSettings.cooldownSec;   // 0 = cleared stands stay empty
    if (gPsiRef && gGsRef && gOnGroundRef && XPLMGetDatai(gOnGroundRef)) {
        GateOccupancy::Traffic me;
        me.lat = user.lat; me.lon = user.lon;
        me.headingDeg = XPLMGetDataf(gPsiRef);
        me.speedMs    = XPLMGetDataf(gGsRef);
        me.onGround   = true;
        for (size_t si = 0; si < n; ++si) {
            if (!gRenderer.isPlanned(si) || gRenderer.isSuppressed(si)) continue;
            const Stand& st = gRenderer.stand(si);
            if (!GateOccupancy::occupies(st, me) && !GateOccupancy::approaching(st, me)) continue;
            const bool had = gRenderer.hasInstance(si);
            gRenderer.setSuppressed(si, true);
            gCooldownUntil[si] = now + cool;
            if (had) Log::info("Conflict: cleared stand '%s' (user parked on or pulling in)", st.id.c_str());
        }
    }

    // Other aircraft (VATSIM through xPilot, or X-Plane AI) parked on, pulling
    // into or pushing back from a stand: emptied, and kept empty unless the
    // restore cooldown is on.
    auto traffic = readTraffic();
    gArrival.setTraffic(traffic);   // gate window status ("live traffic")
    for (size_t si : gGateTraffic.update(gRenderer.stands(), traffic, now)) {
        const bool had = gRenderer.hasInstance(si);
        gRenderer.setSuppressed(si, true);
        gCooldownUntil[si] = now + cool;
        Log::info("Traffic: stand '%s' taken by another aircraft — %s", gRenderer.stand(si).id.c_str(),
                  had ? "static removed" : "no static placed");
    }

    // Restore cooldown on: a stand comes back once its cooldown has run out
    // with nobody on it (the cooldown restarts while someone still is).
    if (cool <= 0.0f) return;
    GateOccupancy::Traffic me;
    me.lat = user.lat; me.lon = user.lon;
    me.headingDeg = gPsiRef ? XPLMGetDataf(gPsiRef) : 0.0f;
    me.onGround = true;
    for (size_t i = 0; i < n; ++i) {
        if (!gRenderer.isSuppressed(i) || gCooldownUntil[i] < 0.0f || gCooldownUntil[i] > now) continue;
        if (gArrival.blocked(i)) continue;
        const Stand& st = gRenderer.stand(i);
        bool taken = GateOccupancy::occupies(st, me);
        for (const auto& t : traffic) taken = taken || (t.onGround && GateOccupancy::occupies(st, t));
        if (taken) { gCooldownUntil[i] = now + cool; continue; }
        gGateTraffic.unclaim(i);
        gRenderer.setSuppressed(i, false);
        gCooldownUntil[i] = -1.0f;
        Log::info("Respawn: stand '%s' restored after cooldown", st.id.c_str());
    }
}

// ── Settings persistence (debounced) ─────────────────────────────────────────
static void markSettingsDirty() { gSettingsDirtyAt = XPLMGetElapsedTime(); }

static void saveSettingsIfDue(bool force) {
    // Track window moves/resizes on the desktop.
    if (gWindow.isCreated()) {
        int l, t, r, b;
        gWindow.getDesktopGeometry(l, t, r, b);
        if (r > l && t > b &&
            (!gSettings.windowGeometrySaved || l != gSettings.windowLeft || t != gSettings.windowTop ||
             r != gSettings.windowRight || b != gSettings.windowBottom)) {
            gSettings.windowLeft = l; gSettings.windowTop = t;
            gSettings.windowRight = r; gSettings.windowBottom = b;
            gSettings.windowGeometrySaved = true;
            if (gSettingsDirtyAt < 0.0) markSettingsDirty();
        }
    }
    if (gGateWin.isCreated()) {
        int l, t, r, b;
        gGateWin.getDesktopGeometry(l, t, r, b);
        if (r > l && t > b &&
            (!gSettings.gateGeometrySaved || l != gSettings.gateLeft || t != gSettings.gateTop ||
             r != gSettings.gateRight || b != gSettings.gateBottom)) {
            gSettings.gateLeft = l; gSettings.gateTop = t;
            gSettings.gateRight = r; gSettings.gateBottom = b;
            gSettings.gateGeometrySaved = true;
            if (gSettingsDirtyAt < 0.0) markSettingsDirty();
        }
    }
    if (gSettingsDirtyAt < 0.0) return;
    if (!force && XPLMGetElapsedTime() - gSettingsDirtyAt < kSaveDebounceSec) return;
    if (Settings::save(Paths::settingsFile(), gSettings)) gSettingsDirtyAt = -1.0;
}

// ── xPilot connection state ───────────────────────────────────────────────────
static int xpilotStatus() {
    if (!gXpilotStatusRef) {
        const double now = XPLMGetElapsedTime();
        if (now < gXpilotNextLookup) return -1;
        gXpilotNextLookup = now + 10.0;
        gXpilotStatusRef = XPLMFindDataRef("xpilot/login/status");
        if (!gXpilotStatusRef) return -1;
        Log::info("XP-GSI: found xPilot dataref xpilot/login/status");
    }
    const XPLMDataTypeID t = XPLMGetDataRefTypes(gXpilotStatusRef);
    if (t & xplmType_Int)    return XPLMGetDatai(gXpilotStatusRef);
    if (t & xplmType_Float)  return static_cast<int>(XPLMGetDataf(gXpilotStatusRef));
    if (t & xplmType_Double) return static_cast<int>(XPLMGetDatad(gXpilotStatusRef));
    return -1;
}

// ── Window model ──────────────────────────────────────────────────────────────
static MainWindow::Model windowModel() {
    MainWindow::Model m;
    const bool hasAirport = !gAirport.icao.empty();
    m.hasAirport = hasAirport;
    m.category   = hasAirport ? gAirport.category : SizeCategory::Standard;
    m.densityAuto        = gAirport.autoPassenger;
    m.density            = Density::get(gSettings, gAirport.icao, Density::Kind::Passenger, m.densityAuto);
    m.densityCustom      = hasAirport && Density::isCustom(gSettings, gAirport.icao, Density::Kind::Passenger);
    m.cargoDensityAuto   = gAirport.autoCargo;
    m.cargoDensity       = Density::get(gSettings, gAirport.icao, Density::Kind::Cargo, m.cargoDensityAuto);
    m.cargoDensityCustom = hasAirport && Density::isCustom(gSettings, gAirport.icao, Density::Kind::Cargo);
    m.cargoStands        = gAirport.cargo;
    m.cargo      = gSettings.cargoEnabled;
    m.running    = gRunning;
    m.mode       = gSettings.startMode;

    UiText::Status st;
    st.running  = gRunning;
    st.icao     = gAirport.icao;
    st.category = hasAirport ? Density::name(gAirport.category) : "";
    st.parked   = gRenderer.instanceCount();
    st.cleared  = gRenderer.clearedCount();
    st.loading  = gRenderer.pendingCount();
    st.waitingForFlight = gRunning && !gRenderer.hasPlan();
    if (!gIndexReady)     st.preparing = "building the airport index (first run or new scenery: up to a minute)";
    else if (!gCslReady)  st.preparing = "scanning CSL models";
    m.statusLine = UiText::statusLine(st);
    m.summary    = UiText::summary(st);
    m.gateAvailable = gArrival.available();

    UiText::Problems p;
    p.xplaneTooOld     = gXplaneVersion < kMinXplaneVersion;
    p.cslScanDone      = gCslReady;
    p.cslModels        = gCslDb ? static_cast<int>(gCslDb->size()) : 0;
    p.noEligibleStands = gRunning && gAirport.noEligibleStands;
    p.airportIcao      = gAirport.icao;
    p.vatsimMode       = gSettings.startMode == StartMode::VatsimConnected;
    p.xpilotMissing    = gFirstTickDone && !gXpilotStatusRef;
    m.banners = UiText::banners(p);

    m.vatsimAvailable = true;   // xpilot/login/status verified (see top of file)
    m.maxAircraft     = gSettings.maxAircraft;
    m.conflictRadiusM = gSettings.conflictRadiusM;
    m.cooldownSec     = gSettings.cooldownSec;
    m.maxRenderDistKm = gSettings.maxRenderDistKm;
    m.sessionSeed     = gSessionSeed;
    m.seedFixed       = gSettings.fixedSeed != 0;
    // "Data 2026.10 · v0.1.0": the active airport's table build month.
    m.footer   = "XP-GSI " XPGSI_VERSION_STR " \xC2\xB7 data " + gDataVersion;
    m.debugLog = gSettings.debugLog;
    return m;
}

// ── Commands and menu ─────────────────────────────────────────────────────────
// Only two commands (Settings > Keyboard / Joystick):
// xpgsi/toggle_window and xpgsi/gate_window.
static int CommandCB(XPLMCommandRef cmd, XPLMCommandPhase phase, void* /*refcon*/) {
    if (phase != xplm_CommandBegin) return 0;   // act on press only
    if (cmd == gCmdToggleWindow)      gWindow.toggleVisible();
    else if (cmd == gCmdGateWindow)   gGateWin.toggle();
    return 0;   // handled: X-Plane need not process it further
}

static void MenuHandlerCB(void* /*menuRef*/, void* itemRef) {
    switch (reinterpret_cast<uintptr_t>(itemRef)) {
        case kMenuOpenWindow: gWindow.setVisible(true); break;
        case kMenuStart:      onNextFlightLoop([] { startRunning(true, "menu"); }); break;
        case kMenuStop:       onNextFlightLoop([] { stopRunning(true, "menu"); }); break;
        case kMenuGate:       gGateWin.open(true); break;
        default: break;
    }
}

// ── Plugin entry points: the exception barrier (core/Guard.h) ────────────────
PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    return Guard::run("XPluginStart", 0, [&] { return pluginStart(outName, outSig, outDesc); });
}
PLUGIN_API void XPluginStop() { Guard::run("XPluginStop", [] { pluginStop(); }); }
PLUGIN_API int XPluginEnable() { return Guard::run("XPluginEnable", 0, [] { return pluginEnable(); }); }
PLUGIN_API void XPluginDisable() { Guard::run("XPluginDisable", [] { pluginDisable(); }); }
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int msg, void* param) {
    Guard::run("XPluginReceiveMessage", [&] { pluginReceiveMessage(from, msg, param); });
}