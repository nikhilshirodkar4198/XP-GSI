#pragma once
// FollowUser — when to move the statics to another airport:
// XP-GSI used to fill only the airport nearest to where the
// flight was loaded, so the arrival airport stayed empty. Below 10,000 ft MSL
// (or on the ground) the active airport follows the nearest medium / large
// airport; above it a plan left ~50 nm behind is dropped.
//
// Pure logic, no XPLM calls.

namespace FollowUser {

constexpr double kCeilingFt   = 10000.0;
constexpr double kMarginKm    = 5.0;    // the new airport must be this much closer
constexpr double kCleanupKm   = 93.0;   // ~50 nm
constexpr double kMinDwellSec = 60.0;   // no switching back and forth

enum class Action { None, Switch, Cleanup };

struct Input {
    bool   lowEnough      = true;   // on the ground or below kCeilingFt
    bool   onGround       = false;
    bool   hasPlan        = false;
    double activeKm       = 0;      // user → active airport
    bool   haveNearest    = false;  // a nearest airline airport was found
    bool   nearestIsActive = true;
    double nearestKm      = 0;
    double sinceSwitchSec = 1e9;
};

inline Action decide(const Input& in) {
    if (!in.lowEnough)
        return (in.hasPlan && in.activeKm > kCleanupKm) ? Action::Cleanup : Action::None;
    if (in.onGround || in.sinceSwitchSec < kMinDwellSec) return Action::None;
    if (!in.haveNearest || in.nearestIsActive) return Action::None;
    return (in.nearestKm + kMarginKm < in.activeKm) ? Action::Switch : Action::None;
}

} // namespace FollowUser
