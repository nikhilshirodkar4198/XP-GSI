#include "renderer/CslDatarefs.h"

#include <cstddef>
#include <cstring>
#include <vector>

#include <XPLMDataAccess.h>

#include "core/Log.h"
#include "csl/ObjLightPatch.h"

namespace {

// Verbatim from XPMP2 src/Aircraft.cpp DR_NAMES (same order as enum DR_VALS).
const char* kNames[] = {
    "libxplanemp/controls/gear_ratio",            // V_CONTROLS_GEAR_RATIO = 0
    "libxplanemp/controls/nws_ratio",
    "libxplanemp/controls/flap_ratio",
    "libxplanemp/controls/spoiler_ratio",
    "libxplanemp/controls/speed_brake_ratio",
    "libxplanemp/controls/slat_ratio",
    "libxplanemp/controls/wing_sweep_ratio",
    "libxplanemp/controls/thrust_ratio",
    "libxplanemp/controls/yoke_pitch_ratio",
    "libxplanemp/controls/yoke_heading_ratio",
    "libxplanemp/controls/yoke_roll_ratio",
    "libxplanemp/controls/thrust_revers",

    "libxplanemp/controls/taxi_lites_on",
    "libxplanemp/controls/landing_lites_on",
    "libxplanemp/controls/beacon_lites_on",
    "libxplanemp/controls/strobe_lites_on",
    "libxplanemp/controls/nav_lites_on",

    "libxplanemp/gear/nose_gear_deflection_mtr",
    "libxplanemp/gear/tire_vertical_deflection_mtr",
    "libxplanemp/gear/tire_rotation_angle_deg",
    "libxplanemp/gear/tire_rotation_speed_rpm",
    "libxplanemp/gear/tire_rotation_speed_rad_sec",

    "libxplanemp/engines/engine_rotation_angle_deg",
    "libxplanemp/engines/engine_rotation_speed_rpm",
    "libxplanemp/engines/engine_rotation_speed_rad_sec",
    "libxplanemp/engines/prop_rotation_angle_deg",
    "libxplanemp/engines/prop_rotation_speed_rpm",
    "libxplanemp/engines/prop_rotation_speed_rad_sec",
    "libxplanemp/engines/thrust_reverser_deploy_ratio",

    "libxplanemp/engines/engine_rotation_angle_deg1",
    "libxplanemp/engines/engine_rotation_angle_deg2",
    "libxplanemp/engines/engine_rotation_angle_deg3",
    "libxplanemp/engines/engine_rotation_angle_deg4",
    "libxplanemp/engines/engine_rotation_speed_rpm1",
    "libxplanemp/engines/engine_rotation_speed_rpm2",
    "libxplanemp/engines/engine_rotation_speed_rpm3",
    "libxplanemp/engines/engine_rotation_speed_rpm4",
    "libxplanemp/engines/engine_rotation_speed_rad_sec1",
    "libxplanemp/engines/engine_rotation_speed_rad_sec2",
    "libxplanemp/engines/engine_rotation_speed_rad_sec3",
    "libxplanemp/engines/engine_rotation_speed_rad_sec4",

    "libxplanemp/misc/touch_down",

    // XP-GSI's own (not XPMP2's): logo lights of the patched OBJ copies
    // (csl/ObjLightPatch), so they can be off while nav lights are on.
    ObjLightPatch::kLogoDataref,

    nullptr  // terminator required by XPLMCreateInstance
};

constexpr std::size_t kCount = sizeof(kNames) / sizeof(kNames[0]) - 1;
static_assert(kCount == 42 + 1, "must match XPMP2 DR_VALS::V_COUNT, plus the logo switch");
constexpr std::size_t kLogoIndex = 42;

// Cold and dark. XPMP2's Aircraft constructor zero-fills its value array
// (all lights off, engines/props stopped). Gear ratio is the "gear deploy
// ratio" (XPMPAircraft.h, mirrored to sim/cockpit2/tcas/targets/position/
// gear_deploy), so 1.0 = gear fully extended.
struct Values {
    float v[kCount] = {};
    Values(bool nav, bool logo) {
        v[0] = 1.0f;                       // V_CONTROLS_GEAR_RATIO
        if (nav)  v[16] = 1.0f;            // V_CONTROLS_NAV_LITES_ON (DR_VALS index 16; checked below)
        if (logo) v[kLogoIndex] = 1.0f;
    }
};
constexpr std::size_t kNavIndex = 16;
const Values kColdAndDark(false, false);
const Values kNavLights(true, false);
const Values kNavLogoLights(true, true);

std::vector<XPLMDataRef> sOwned;   // datarefs we registered ourselves
bool                     sEnsured = false;

// Formal read accessor; X-Plane takes instance values from XPLMInstanceSetPosition.
float readZero(void* /*refcon*/) { return 0.0f; }

} // namespace

namespace CslDatarefs {

const char** ensureRegistered() {
    if (!sEnsured) {
        sEnsured = true;
        if (std::strcmp(kNames[kNavIndex], "libxplanemp/controls/nav_lites_on") != 0)
            Log::error("CslDatarefs: nav_lites_on is not at index %zu — static lights will be wrong",
                       kNavIndex);
        size_t found = 0;
        for (std::size_t i = 0; i < kCount; ++i) {
            // XPLMFindDataRef (XPLM200): non-null if any plugin (e.g. xPilot) owns it.
            if (XPLMFindDataRef(kNames[i])) { ++found; continue; }
            // XPLMRegisterDataAccessor (XPLMDataAccess.h): read-only float.
            XPLMDataRef ref = XPLMRegisterDataAccessor(
                kNames[i], xplmType_Float, 0,
                nullptr, nullptr, readZero, nullptr, nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr, nullptr);
            if (ref) sOwned.push_back(ref);
        }
        Log::info("CslDatarefs: %zu of %zu libxplanemp datarefs already registered, %zu registered by XP-GSI",
                  found, kCount, sOwned.size());
    }
    return kNames;
}

const float* coldAndDarkValues() { return kColdAndDark.v; }
const float* navLightsValues()   { return kNavLights.v; }
const float* navLogoValues()     { return kNavLogoLights.v; }

void shutdown() {
    for (XPLMDataRef ref : sOwned)
        XPLMUnregisterDataAccessor(ref);   // XPLMDataAccess.h
    sOwned.clear();
    sEnsured = false;
}

} // namespace CslDatarefs
