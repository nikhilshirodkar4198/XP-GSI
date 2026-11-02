#pragma once
// StandEvidence — corrects the scenery's stands from where aircraft really
// parked (AirportTable::parkCells, 30 days of adsb.lol positions). Stand
// audit of the 114 busiest airports with ground coverage (2026-10-27,
// pipeline/audit_stands.py):
//   * stands marked too small: Global Airports KDFW 57, KIAD 53, LEMD 48;
//     add-on KMSP A2–A7 "B" where CRJ-900s / E175s (C) park every day
//   * remote stands marked cargo or GA where airliners park (MMMY W01 / F15,
//     VTBS 511–515), and stands with no use at all ("none": RJCC, LIML)
// Each parking cell goes to its nearest stand (any use) within kMatchM — an
// ADS-B position is the GPS antenna's last fix, a median ~20 m from the
// stand point. Positions of a multi-position gate (StandLayout::gateKeys)
// count together: a widebody on the centre stand is close to the side ones.
//   * use: a stand not marked airline (cargo, GA, none, unknown; not
//     military, hangar or misc) becomes an airline stand with at least
//     kMinPaxStays passenger stays that are at least kMinPaxShare of all
//   * size: an airline stand (a gate's largest position) where larger
//     aircraft parked at least kMinBigger times and kMinBiggerShare of its
//     passenger stays takes the largest size seen at least kMinSizeShare of
//     the time. Never smaller. The layout check still keeps every static
//     clear of its neighbours (smaller types where the room is not there).
//   * not passenger: an airline stand where at least kMinNonPaxStays stays
//     were non-passenger and at least kNonPaxDominance × the passenger ones
//     gets no static (op None; airliners on the stands
//     south of KSMF Terminal B, typed airline by Global Airports, where
//     feeder freighters and private jets park). Not a cargo stand: the cells
//     do not tell cargo from business jets or maintenance repositioning
//     (KSFO's United maintenance ramps, 15 such stands).
// Stands with an override (data/stand_overrides.json) are left alone.
// Pure logic, no XPLM calls.

#include <cstddef>
#include <vector>

#include "data/AirportData.h"
#include "engine/StandState.h"

namespace StandEvidence {

constexpr double kMatchM          = 50.0;
constexpr int    kMinPaxStays     = 8;
constexpr double kMinPaxShare     = 0.7;
constexpr int    kMinBigger       = 4;
constexpr double kMinBiggerShare  = 0.3;
constexpr double kMinSizeShare    = 0.1;
constexpr int    kMinNonPaxStays  = 5;
constexpr double kNonPaxDominance = 2.0;

struct Evidence {
    int pax[5] = {};   // B..F
    int nonPax = 0;
    int paxTotal() const { return pax[0] + pax[1] + pax[2] + pax[3] + pax[4]; }
};

// Per stand: the parking cells nearest to it within kMatchM.
std::vector<Evidence> collect(const std::vector<Stand>& stands, const std::vector<ParkCell>& cells);

// The size the evidence asks for: `current`, or larger.
char sizeFor(const Evidence& e, char current);

// Passenger airliners clearly use it.
bool passengerStand(const Evidence& e);
// Mostly other aircraft (cargo, GA, business, military) use it.
bool nonPassengerStand(const Evidence& e);

// Passenger stays at each stand, a multi-position gate's counted together
// (Planner::rankStands fill order by use).
std::vector<int> paxStaysPerStand(const std::vector<Stand>& stands, const std::vector<ParkCell>& cells);

struct Result {
    std::size_t toAirline = 0;   // stands now airline stands
    std::size_t upsized   = 0;   // airline stands given a larger size
    std::size_t notAirline = 0;  // airline stands where mostly other aircraft park: no static
};

// Corrects `stands` in place (op, widthCode).
Result apply(std::vector<Stand>& stands, const std::vector<ParkCell>& cells);

} // namespace StandEvidence
