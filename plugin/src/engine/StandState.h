#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ── Enumerations ─────────────────────────────────────────────────────────────

enum class OpType    { Airline, Cargo, GA, Military, None, Unknown };
enum class StandType { Gate, TieDown, Hangar, Misc };
enum class StandState {
    Empty,    // available to be filled
    Planned,  // chosen by the planner, not yet loading
    Loading,  // XPLMLoadObjectAsync in flight
    Placed,   // instance exists on the stand
    Clearing, // conflict detected; about to destroy instance
    Cooldown, // recently cleared; blocked from refilling
    Disabled  // profile-disabled or no eligible models
};

// ── Equipment class bitmask ───────────────────────────────────────────────────
// Mirrors the pipe-separated token list in apt.dat row 1300, field 5.
// Verified against the apt.dat 1100 spec:
// developer.x-plane.com/article/airport-data-apt-dat-file-format-specification/
namespace EquipClass {
    static constexpr uint32_t Heavy      = 1u << 0; // "heavy"
    static constexpr uint32_t Jets       = 1u << 1; // "jets"
    static constexpr uint32_t Turboprops = 1u << 2; // "turboprops"
    static constexpr uint32_t Props      = 1u << 3; // "props"
    static constexpr uint32_t Helos      = 1u << 4; // "helos"
    static constexpr uint32_t Fighters   = 1u << 5; // "fighters"
    static constexpr uint32_t Skydive    = 1u << 6; // "skydive"
    static constexpr uint32_t All        = 0x7Fu;
} // namespace EquipClass

// ── Stand ─────────────────────────────────────────────────────────────────────

struct Stand {
    std::string id;              // ramp start name, e.g. "Gate B12" (from row 1300)
    double      lat  = 0.0;     // decimal degrees (row 1300, field 1)
    double      lon  = 0.0;     // decimal degrees (row 1300, field 2)
    double      elevM = 0.0;    // terrain-probed at spawn time (Phase 2)
    float       headingDeg = 0.f;

    StandType   type = StandType::Misc;
    char        widthCode = '?'; // 'A'..'F' — from row 1301 or inferred
    uint32_t    allowedClasses = EquipClass::All;
    OpType      op = OpType::Unknown;
    std::vector<std::string> airlines; // pipe-split from row 1301, or profile
    // data/stand_overrides.json (data/StandOverrides): only these ICAO types
    // (empty = any), and the brand a regional's paint must show here
    // (empty = by airport presence).
    std::vector<std::string> allowedTypes;
    std::string              brand;
    bool                     airlinesOverridden = false;   // `airlines` from the overrides, not the scenery
    // The overrides' weight for each of `airlines` (hand-made area mixes,
    // KSFO 2026-10-28); empty = the airport table's weights.
    std::map<std::string, float> airlineWeights;

    // Profile-tunable position offsets (Phase 8)
    float offsetAlongM   = 0.f;
    float offsetLateralM = 0.f;

    // State machine (Phase 5)
    StandState  state = StandState::Empty;
    double      stateSinceSec = 0.0;
    int         conflictHits  = 0;
    int         clearHits     = 0;
    std::string lastClearReason;
    int         occupant = -1; // index into StaticAircraft pool
};
