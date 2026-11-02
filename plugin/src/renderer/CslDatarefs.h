#pragma once
// CslDatarefs — the instance dataref list CSL models animate on, with
// cold-and-dark values for parked statics.
//
// Names and order verified against XPMP2 (github.com/TwinFan/XPMP2, master,
// read 2026-10-25): src/Aircraft.cpp DR_NAMES, indexed by enum DR_VALS in
// inc/XPMPAircraft.h. XPMP2 passes exactly this list to XPLMCreateInstance.
//
// XPLMInstance.h: "If you use any custom datarefs in your object, they must be
// registered before the object is loaded." xPilot (via XPMP2 AcInit) registers
// these names itself; when a name is not yet registered by anyone we register
// a read-only stub accessor (value comes from XPLMInstanceSetPosition anyway)
// and unregister it on shutdown. Names owned by other plugins are never touched.
// One extra name is XP-GSI's own: xpgsi/controls/logo_lites_on (logo lights
// of the patched OBJ copies), registered and unregistered the same way.
//
// Main thread only (XPLM calls).

namespace CslDatarefs {

// Make sure every name is registered; returns the null-terminated name list
// for XPLMCreateInstance. Call before loading any CSL object.
const char** ensureRegistered();

// One float per name, in list order, for XPLMInstanceSetPosition:
// gear fully down, everything else (lights, engines, props, surfaces) 0.
const float* coldAndDarkValues();

// The same with nav_lites_on = 1: nav lights only (logo lights off).
const float* navLightsValues();

// Nav lights and, in XP-GSI's patched OBJ copies (csl/ObjLightPatch), the
// logo lights (xpgsi/controls/logo_lites_on = 1).
const float* navLogoValues();

// Unregister the datarefs this plugin registered. Call from XPluginStop
// after every instance has been destroyed.
void shutdown();

} // namespace CslDatarefs
