#pragma once
// GateOccupancy: is another aircraft (VATSIM via xPilot, or X-Plane AI) parked
// at, pushing back from, or pulling into a stand? Pure logic, no XPLM.
//
// An aircraft occupies a stand when it is on the ground, slow, roughly aligned
// with the stand heading, and its reference point lies in a box behind the
// stand point (the stand point is where the nose stops; the reference point of
// a CSL model is near mid-fuselage). Taxiing past on the taxilane fails the
// heading or speed test. Pushback keeps the heading aligned, so an aircraft is
// caught from the moment it sits on the stand until it leaves.
//
// A Tracker turns per-check observations into "claimed" stands: claimed once
// the condition holds long enough, and never released
// (after a VATSIM aircraft pushes back, no static spawns there).

#include <cstddef>
#include <string>
#include <vector>

#include "engine/StandLayout.h"
#include "engine/StandState.h"

namespace GateOccupancy {

struct Traffic {
    double lat = 0, lon = 0;
    float  headingDeg = 0;    // true
    float  speedMs    = 0;
    bool   onGround   = false;
    char   sizeCode   = '?';  // ICAO reference code A–F from its type (IcaoSize), '?' unknown
    float  heightAglM = 0;    // reference point above the terrain (for the log)
    std::string callsign;     // for the log only
};

// Outline of another aircraft around its reference point (f forward, r right),
// from its size code: typical span / length per code, wing near mid-fuselage.
StandLayout::Shape trafficShape(char sizeCode);

// Gap below which another aircraft "touches" a static (metres).
constexpr double kTouchClearanceM = 1.0;

// Does `t` touch the static planned at `stand` (outline `staticShape`, placed
// as the renderer places it)? Only for slow aircraft on the ground.
bool touches(const Stand& stand, const StandLayout::Shape& staticShape, const Traffic& t);

// Where `t` sits relative to `stand`: metres ahead of the stand point (+) /
// behind (-), metres to the side, and heading difference (0–180°).
struct Relative { double along, across; float headingDiff; };
Relative relative(const Stand& stand, const Traffic& t);

constexpr float kMaxSpeedMs      = 5.0f;    // ~10 kt: pulling in / pushing back
constexpr float kParkedSpeedMs   = 0.5f;    // standing still: claim at once
constexpr float kMaxHeadingDiff  = 35.0f;   // degrees from the stand heading
constexpr float kAheadM          = 10.0f;   // reference point beyond the stand point
constexpr float kHalfWidthM      = 12.0f;   // lateral tolerance (pilots' scenery differs)
constexpr float kDwellSec        = 1.0f;    // moving slowly in the box: must hold this long (was 2)
constexpr float kTouchDwellSec   = 2.0f;    // only touching a static (e.g. a wingtip passing)

// Pulling in (statics vanished too late, after the
// incoming aircraft already overlapped them): the stand's centreline from the
// occupancy box back another kApproachExtraM, for an aircraft that has turned
// in (heading within kApproachHeadingDiff), is slow, and is within
// kApproachHalfWidthM of the centreline. A taxi-by fails the heading test, an
// aircraft turning into the next gate the centreline test.
constexpr float kApproachExtraM     = 35.0f;
constexpr float kApproachHalfWidthM = 8.0f;
constexpr float kApproachHeadingDiff = 30.0f;
constexpr float kApproachMaxSpeedMs = 6.0f;
bool approaching(const Stand& stand, const Traffic& t);

// Metres behind the stand point a reference point may sit, by size code.
float depthBehindM(char widthCode);

// Does aircraft `t` occupy `stand` right now?
bool occupies(const Stand& stand, const Traffic& t);

// Gate a stand belongs to: its name without trailing letters after the
// number ("F22B" → "F22", "G57C" → "G57", "305 L" → "305"). Alternative
// parking positions of one gate share it; an aircraft on any of them takes
// the whole gate (within kSameGateM).
std::string gateGroup(const std::string& standName);
constexpr double kSameGateM = 120.0;
// Farthest any test reaches from a stand (touches() stops at 150 m), in degrees of latitude.
constexpr double kReachDegLat = 160.0 / 110574.0;

class Tracker {
public:
    void reset(std::size_t standCount);

    // Outlines of the statics planned per stand (same order as the stands);
    // with them an aircraft that touches a static also claims its stand.
    void setShapes(std::vector<StandLayout::Shape> shapes) { m_shapes = std::move(shapes); }

    // Observe all traffic at time `now` (seconds). Returns the stands newly
    // claimed by this call, including the other positions of the same gate.
    std::vector<std::size_t> update(const std::vector<Stand>& stands,
                                    const std::vector<Traffic>& traffic, double now);

    bool claimed(std::size_t i) const { return i < m_claimed.size() && m_claimed[i]; }
    // The stand may be filled again (restore cooldown on, the traffic gone).
    void unclaim(std::size_t i) { if (i < m_claimed.size()) m_claimed[i] = false; }

private:
    std::vector<bool>        m_claimed;
    std::vector<double>      m_since;     // first time seen occupied (moving), -1 = not
    std::vector<std::string> m_group;     // gateGroup per stand, filled on first update
    std::vector<double>      m_cosLat;    // cos(latitude) per stand, filled with m_group
    std::vector<StandLayout::Shape> m_shapes;
};

} // namespace GateOccupancy
