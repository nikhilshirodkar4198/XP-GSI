#pragma once
// Reservations — arrival gate reservation, the pure
// part: gate-name matching, the armed window, the reservation set, which
// statics a reservation clears and when, and the CDU scratchpad stability rule.
// The XPLM glue (datarefs, CDU reading, notification, published dataref and
// command) lives in main.cpp / ui/Toast. Pure logic, no XPLM calls.

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "engine/StandLayout.h"
#include "engine/StandState.h"

namespace Reservations {

// ── Gate names ────────────────────────────────────────────────────────────────
// Uppercase; spaces, '-', '.', '_' removed.
std::string normalizeEntry(const std::string& text);
// The same, then one leading "PARKING", "STAND", "GATE", "STD" or "GT" removed
// (only if something is left after it).
std::string normalizeStand(const std::string& standName);
// Indices of every stand whose normalized name equals the normalized entry
// (exact only; several when two terminals share a number). Empty entry → none.
std::vector<std::size_t> match(const std::string& entry, const std::vector<std::string>& standNames);

// ── Armed window ──────────────────────────────────────────────────────────────
// Armed at touchdown after ≥ kMinAirborneSec airborne (a reload on the ground
// never arms). Disarmed when parked (< 1 kt with the parking brake set for
// kParkedSec), when every engine is off, when airborne again for
// kTakeoffSec (a bounced landing does not count), or on reset() (new flight).
constexpr double kMinAirborneSec = 60.0;
constexpr double kParkedSec      = 10.0;
constexpr double kTakeoffSec     = 10.0;
constexpr float  kParkedSpeedMs  = 0.5144f;   // 1 kt
constexpr float  kParkBrakeSet   = 0.9f;

struct Sample {
    double now = 0;
    bool   onGround = true;
    float  groundSpeedMs = 0;
    float  parkBrake = 0;        // 0..1
    bool   anyEngineRunning = true;
};

enum class Event { None, Armed, Parked, EnginesOff, TookOff };

class ArmState {
public:
    Event update(const Sample& s);
    void  reset();                       // new flight loaded
    bool  armed() const { return m_armed; }

private:
    bool   m_armed = false;
    double m_airborneSince = -1;         // first airborne sample, -1 on the ground
    double m_parkedSince   = -1;
};

const char* eventName(Event e);

// ── Reservation set ───────────────────────────────────────────────────────────
// Stands are indices into the active airport's full stand list.
class Set {
public:
    // Adds every index not yet reserved; returns how many were new.
    std::size_t add(const std::vector<std::size_t>& stands);
    bool        release(std::size_t stand);          // false if it was not reserved
    std::size_t releaseAllExcept(std::size_t keep);  // returns how many were released
    std::size_t releaseAll();
    bool        contains(std::size_t stand) const { return m_stands.count(stand) != 0; }
    const std::set<std::size_t>& stands() const { return m_stands; }
    bool        empty() const { return m_stands.empty(); }

private:
    std::set<std::size_t> m_stands;
};

// Which planned statics a reservation keeps away (the
// reserved stand itself, the other positions of its gate, and any static the
// user's aircraft would touch once parked there). `userShape` is the user's
// outline with its nose on the stand point (userShapeForSize).
std::vector<bool> blockedPlanStands(const std::vector<Stand>& reserved,
                                    const std::vector<Stand>& plan,
                                    const std::vector<StandLayout::Shape>& planShapes,
                                    const StandLayout::Shape& userShape);
StandLayout::Shape userShapeForSize(char sizeCode);

// Clear a blocked static now? Out of the camera's view, behind the user's
// aircraft, or the user within kClearBeforeM: yes. Otherwise wait.
constexpr double kClearBeforeM = 150.0;
inline bool clearNow(bool inCameraView, bool behindUser, double userDistM) {
    return !inCameraView || behindUser || userDistM < kClearBeforeM;
}

// Is a point at `dirX/Y/Z` (camera → target, local metres: +X east, +Y up,
// +Z south) within a camera looking along heading/pitch (degrees, heading
// clockwise from true north) with the given full fields of view? `radiusM`
// widens the test by the target's size.
bool inView(double dirX, double dirY, double dirZ, double headingDeg, double pitchDeg,
            double hfovDeg, double vfovDeg, double radiusM);

// ── CDU scratchpad stability ──────────────────────────────────────────────────
// Feed the scratchpad text every poll; returns the text once it has stayed
// exactly the same for kStableSec and has not been returned before in this
// armed session (each distinct entry is processed once). Empty text never fires.
constexpr double kStableSec = 2.0;

class StableText {
public:
    // Returns "" when nothing is due.
    std::string feed(const std::string& text, double now);
    void reset();

private:
    std::string           m_text;
    double                m_since = -1;
    std::set<std::string> m_done;
};

// A CDU gate entry is "G<stand>" ("G/<stand>" also accepted; user,
// 2026-10-28: the prefix keeps other scratchpad entries from reserving a
// stand). True with `stand` set when it is.
bool cduGateEntry(const std::string& text, std::string& stand);

// Printable, trimmed text from a raw CDU/dataref byte buffer (stops at NUL).
std::string cleanCduText(const char* bytes, std::size_t n);

} // namespace Reservations
