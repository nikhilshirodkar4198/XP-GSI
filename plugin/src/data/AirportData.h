#pragma once
// AirportData — loads data/airports/<ICAO>.json, the per-airport airline and
// type weights built by pipeline/build_tables.py from adsb.lol ground stays.
//
// Schema 1 :
//   { "icao": "KIND", "schema": 1, "source": "...", "samples": 312,
//     "airlines": { "SWA": 0.31, ... },
//     "types":    { "SWA": { "B737": 0.4, "B738": 0.5 }, ... } }
// Optional: "occupancy" (0–1), "airport_type" (OurAirports
// type, e.g. "large_airport"), "daily_arrivals" (airline aircraft parked per
// sampled day; Density category) and "clusters":
//   [ { "lat": 37.6155, "lon": -122.3922, "n": 14,
//       "airlines": { "BAW": 3, "CPA": 2 }, "types": { "BAW": { "A388": 2 } } } ]
// and "terminals" (OpenStreetMap, pipeline/terminals.py):
//   { "zones": [ { "name": "Heathrow Terminal 5", "group": "T5" } ],
//     "anchors": [ [51.4723, -0.4881, 0], ... ] }      // lat, lon, zone index
//
// Pure logic (file IO + JSON), no XPLM calls: load on the worker thread.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Where aircraft actually parked: one ~100 m cell of reconstructed parking
// positions (pipeline/build_tables.py), with how many stays each airline —
// and each airline's types — had there. Soft area weights for model
// selection; never used as a position (stands come from the scenery only).
struct ParkCluster {
    double                                              lat = 0, lon = 0;
    float                                               n = 0;       // stays in the cell
    std::map<std::string, float>                        airlines;    // airline → stays
    std::map<std::string, std::map<std::string, float>> types;       // airline → type → stays
};

// A passenger terminal or concourse from OpenStreetMap (pipeline/terminals.py).
// `group` is the terminal it belongs to ("T5" for "Terminal 5C"), else its name.
struct TerminalZone {
    std::string name;
    std::string group;
};

// A point of a zone: a building outline sample, gate or parking position. A
// position belongs to the zone of its nearest anchor within
// TerminalZones::kAnchorMaxM.
struct ZoneAnchor {
    double lat = 0, lon = 0;
    int    zone = 0;
};

// A parking position from OpenStreetMap (pipeline/terminals.py
// stands_from_osm), added only where the scenery defines far too few stands.
struct OsmStand {
    double      lat = 0, lon = 0;    // nose stop
    float       headingDeg = 0;      // true
    char        size = 'C';          // ICAO code from stand spacing and parked types
    std::string ref;                 // "Bay 12"
    bool        remote = false;      // heading from its neighbours (apron), not a gate
};

// ~10 m cell of reconstructed parking positions ("park_cells",
// pipeline/build_tables.py park_cells): passenger stays by size code B–F and
// every other stay (cargo, GA, business, military). Matched to the scenery's
// stands by engine/StandEvidence.
struct ParkCell {
    double lat = 0, lon = 0;
    int    pax[5] = {};   // B, C, D, E, F
    int    nonPax = 0;
};

struct AirportTable {
    std::string                                         icao;
    int                                                 samples = 0;
    std::string                                         generated;          // build date "2026-10-27", "" = absent
    float                                               occupancy = -1.0f;  // < 0 = absent
    std::string                                         airportType;        // "" = unknown
    float                                               dailyArrivals = -1.0f;  // airline stays / day; < 0 = absent
    // Airline aircraft parked at an average moment ("on_ground": hours on the
    // ground per day / 24, stays capped at 8 h, overnight stays not counted);
    // < 0 = absent (tables built before 2026-10-28).
    float                                               onGround = -1.0f;
    std::map<std::string, float>                        airlines;  // airline → weight
    std::map<std::string, std::map<std::string, float>> types;     // airline → type → weight
    std::vector<ParkCluster>                            clusters;  // optional
    std::vector<TerminalZone>                           zones;     // optional ("terminals")
    std::vector<ZoneAnchor>                             anchors;   // indices into zones
    std::vector<OsmStand>                               osmStands; // optional ("osm_stands")
    // ~100 m spots where mostly cargo / private / military aircraft park
    // ("nonpax_cells"): no passenger static within kNonPaxRadiusM of one.
    std::vector<std::pair<double, double>>              nonPaxCells;
    // Cargo aircraft parked here ("cargo"): the mix for cargo stands.
    std::map<std::string, float>                        cargoAirlines;
    std::map<std::string, std::map<std::string, float>> cargoTypes;
    float                                               cargoDaily = 0;
    std::vector<ParkCell>                               parkCells;   // optional ("park_cells")
};

namespace AirportData {

// A table is trusted only with at least this many ground stays.
constexpr int kMinSamples = 50;

// A stand this close to a cargo / private-jet spot is on that apron
// (pipeline/build_tables.py NONPAX_RADIUS_M).
constexpr double kNonPaxRadiusM = 60.0;
bool nearNonPax(const AirportTable& t, double lat, double lon);


// Parse one table file. nullopt if missing, malformed, or a different schema.
std::optional<AirportTable> load(const std::filesystem::path& file);

// OpenStreetMap stands of an airport without a table (data/osm_stands.json,
// pipeline/build_osm_stands.py: sparse sceneries, mostly X-Plane 11's
// defaults); empty when absent.
std::vector<OsmStand> loadOsmStands(const std::filesystem::path& file, const std::string& icao);

// <dataDir>/airports/<ICAO>.json
std::optional<AirportTable> loadFor(const std::filesystem::path& dataDir, const std::string& icao);

// Airports without parking data (no clusters): terminal zones and the
// airlines that use each (data/terminal_assignments.json, pipeline/
// build_terminals.py — hand-written for the large Wikipedia-only hubs,
// foreign airlines at international terminals elsewhere). Sets the table's
// zones / anchors and adds kAssignedStays of parking evidence per anchor,
// split over the zone's airlines by their table weights, so the terminal
// logic (TerminalZones, ModelSelector::areaMix) keeps each airline at its
// terminals. Returns the zones applied; 0 if the file has no entry or the
// table already has parking clusters. The zones and anchors come from
// `file` (OpenStreetMap, ODbL); each zone's airlines from the same entry or,
// when it has none, from `airlinesFile` (data/terminal_airlines.json, derived
// from Wikipedia airline lists, CC BY-SA: shipped as a separate file).
constexpr float kAssignedStays = 2.0f;
std::size_t applyTerminalAssignments(AirportTable& table, const std::filesystem::path& file, const std::string& icao,
                                     const std::filesystem::path& airlinesFile = {});

} // namespace AirportData
