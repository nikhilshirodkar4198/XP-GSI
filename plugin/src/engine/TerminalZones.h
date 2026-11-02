#pragma once
// TerminalZones — which terminal a stand belongs to, and which airlines park
// at each terminal, so a static only shows an airline at a terminal it
// actually uses.
//
// Zones come from the table's OpenStreetMap terminals (AirportTable::zones /
// anchors, pipeline/terminals.py) when it has them. Otherwise the airport's
// airline stands are grouped: stands closer than kStandLinkM to a neighbour
// share a group (one pier or terminal front), and each group is a zone.
//
// A zone's mix is every adsb.lol parking cluster (AirportTable::clusters)
// that falls in it. A position falls in the zone of its nearest anchor within
// the zone set's reach (kAnchorMaxM for OpenStreetMap, the same rule the
// pipeline uses; kClusterToStandM to the nearest stand for stand groups).
//
// Pure logic, no XPLM calls.

#include "data/AirportData.h"
#include "engine/StandState.h"

#include <map>
#include <string>
#include <vector>

namespace TerminalZones {

constexpr double kAnchorMaxM      = 400.0;  // pipeline/terminals.py ANCHOR_MAX_M
constexpr double kStandLinkM      = 100.0;
constexpr double kClusterToStandM = 150.0;
// A zone (else its group) needs this many parked stays before its own mix is
// used for a stand.
constexpr float  kMinZoneEvidence = 5.0f;
// An airline with at least kMinAirlineStays zoned stays, of which less than
// kElsewhereShare are in a stand's terminal group, is based elsewhere and is
// not placed at that stand from the airport-wide mix.
constexpr float  kMinAirlineStays = 5.0f;
constexpr float  kElsewhereShare  = 0.10f;

struct Mix {
    float                                               n = 0;   // parked stays
    std::map<std::string, float>                        airlines;
    std::map<std::string, std::map<std::string, float>> types;
};

class Zones {
public:
    // Zones for this airport: the table's OpenStreetMap terminals if any, else
    // groups of `stands` (the airline stands). Empty when the table has no
    // parking clusters (nothing to say where airlines park).
    static Zones build(const AirportTable& table, const std::vector<Stand>& stands);

    bool        empty() const { return names_.empty(); }
    bool        fromOsm() const { return fromOsm_; }
    std::size_t zoneCount() const { return names_.size(); }
    const std::string& zoneName(int zone) const { return names_[zone]; }

    // Zone of a position; -1 if no anchor is within reach.
    int zoneOf(double lat, double lon) const;
    // Zone of table cluster i (as passed to build); -1 if outside every zone.
    int clusterZone(std::size_t i) const { return i < clusterZone_.size() ? clusterZone_[i] : -1; }

    // The zone's mix with at least kMinZoneEvidence stays, else its group's,
    // else nullptr.
    const Mix* mixFor(int zone) const;
    // See kElsewhereShare.
    bool basedElsewhere(const std::string& airline, int zone) const;

private:
    struct Anchor { double x, y; int zone; };   // metres east / north of ref
    bool                     fromOsm_ = false;
    double                   refLat_ = 0, refLon_ = 0, reachM_ = 0;
    std::vector<std::string> names_;
    std::vector<int>         groupOf_;          // zone → group
    std::vector<Anchor>      anchors_;
    std::vector<int>         clusterZone_;
    std::vector<Mix>         zoneMix_, groupMix_;
    std::map<std::string, std::vector<float>> airlineByGroup_;  // airline → stays per group
};

} // namespace TerminalZones
