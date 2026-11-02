#pragma once
// ObjGeometry — measure a CSL model's OBJ geometry to find its nose gear.
//
// Needed because a ramp start (apt.dat row 1300) is where the aircraft's
// nose gear stops, not where the model origin goes: X-Plane's apt.dat spec
// only defines the point as the "Start or end point for aircraft" with the
// "Heading (true) of airplane positioned at this location", and CSL metadata
// carries no gear position. Placing the OBJ origin on the point put whole
// airframes forward of the stand, against the terminal (live test, KIND).
//
// OBJ8 axes (X-Plane OBJ convention): +X right, +Y up, -Z forward. Checked on
// the X-CSL library: nose tips at -Z, tail fins at +Z for every type sampled.
//
// Gear geometry is the triangles inside ANIM blocks driven by
// libxplanemp/controls/gear_ratio (X-CSL: "ANIM_hide -1.0 0.0 …/gear_ratio"
// around the gear-down meshes). The nose gear is the centreline part of it in
// the front half of the airframe; its lowest vertices are the wheel contact.
//
// Pure logic, no XPLM calls: run on the worker thread.

#include <filesystem>
#include <vector>

namespace ObjGeometry {

struct Extents {
    bool  valid = false;      // at least one VT vertex read
    float minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    bool  noseGearFound = false;
    float noseGearZ     = 0;  // nose-wheel contact, OBJ Z
    float noseGearY     = 0;  // nose-wheel contact, OBJ Y (bottom of the tyre)
    // Outer-wing chord band: Z range of vertices with |X| ≥ kOuterWingFraction
    // of the half-span (wings only — the horizontal tail and engines sit
    // further in). Used for the parking footprint (engine/StandLayout).
    bool  wingFound = false;
    float wingMinZ  = 0, wingMaxZ = 0;
    // Inner wing and engines: Z range of vertices with kInnerWingFraction ≤
    // |X| / half-span < kOuterWingFraction, ahead of the tail. On a swept wing
    // it lies well forward of the outer band; a neighbour's wingtip next to
    // it went unchecked (an A320 on LOWW F11 inside a
    // Qatar 787's wing on F13-H).
    bool  innerWingFound = false;
    float innerWingMinZ  = 0, innerWingMaxZ = 0;
};

constexpr float kOuterWingFraction   = 0.45f;
constexpr float kInnerWingFraction   = 0.15f;
constexpr float kTailExcludeFraction = 0.22f;   // aft part of the length: horizontal tail

constexpr float kCentrelineM     = 1.0f;  // |X| of nose-gear geometry
constexpr float kContactBandM    = 0.3f;  // vertices this close to the lowest = contact patch
// Fallback when no gear animation is found: the nose gear sits about this
// fraction of the length behind the nose tip (A320 ≈ 5.1 m of 37.6 m).
constexpr float kNoseGearFractionOfLength = 0.13f;
// A measured nose gear outside this band (fraction of length behind the nose
// tip) is treated as a mis-detection and the fallback is used. Real airliners
// sit around 0.06 (CRJ, Q400) to 0.14 (A320).
constexpr float kNoseGearMinFraction = 0.03f;
constexpr float kNoseGearMaxFraction = 0.30f;

// Parse every OBJ part of one model (VT / IDX / IDX10 / TRIS / ANIM_*).
Extents measure(const std::vector<std::filesystem::path>& objParts);

// True if the measured nose gear is found and inside the plausible band.
bool noseGearPlausible(const Extents& e);

// How far (metres) the nose gear lies forward of the model origin; place the
// origin this far behind the stand point along the stand heading so the nose
// gear lands on the point. 0 when the geometry could not be read.
float noseGearForwardM(const Extents& e);

} // namespace ObjGeometry
