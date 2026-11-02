#pragma once
// WikiFallback — airline mixes for airports the adsb.lol tables cover thinly
// or not at all. Built by pipeline/build_wiki.py into two files that are kept
// apart because their licenses differ, and combined here at run time:
//
//   data/wiki_airlines.json   (CC BY-SA 4.0, from Wikipedia)
//     { "schema": 1, "airports": { "KAVL": { "country": "US",
//         "airport_type": "medium_airport",
//         "airlines": { "AAL": 0.3, "brand:American Eagle (airline brand)": 0.4 } } } }
//     Weights come from the number of destinations each airline serves there.
//
//   data/airline_fleets.json  (ODbL 1.0, from adsb.lol)
//     { "schema": 1,
//       "fleets": { "AAL": { "all": { "A321": 0.4, ... }, "large_airport": {...},
//                            "medium_airport": {...} } },
//       "brands": { "brand:American Eagle (airline brand)": { "*": { "ENY": 0.4, ... },
//                                                              "US": { ... } } } }
//     The types each operator was seen parked with, and how a brand's
//     flying splits among its operators per country ("*" = everywhere).
//
// Pure logic (file IO + JSON), no XPLM calls: load on the worker thread.

#include "data/AirportData.h"

#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

struct WikiEntry {
    std::string                  icao;
    std::string                  country;      // ISO 3166-1 alpha-2
    std::string                  airportType;  // OurAirports type
    std::map<std::string, float> airlines;     // ICAO code or "brand:<article>" → weight
    // Year-round destinations + ¼ seasonal, over every listed airline: about
    // the airline arrivals a day (Density category). < 0 = absent.
    float                        destinationScore = -1.0f;
    // Airline aircraft on the ground at an average moment, from the article
    // infobox's aircraft movements (else annual passengers), calibrated on
    // the adsb.lol tables' "on_ground" ("on_ground_est"; build_wiki.py,
    // 2026-10-28: ZGGG 550,512 movements). < 0 = absent.
    float                        onGroundEst = -1.0f;
    // Airline arrivals a day from the infobox ("arrivals_est"; Density). < 0 = absent.
    float                        arrivalsEst = -1.0f;
    // Cargo airlines of the article's "Cargo" list ("cargo": {"airlines",
    // "daily_est"}): cargo stands at airports without adsb.lol cargo data.
    std::map<std::string, float> cargoAirlines;
    float                        cargoDailyEst = 0.0f;
    double                       lat = std::nan(""), lon = std::nan("");   // "pos" (OurAirports)
};

struct Fleets {
    using Mix = std::map<std::string, float>;
    // code → category → type → weight. Categories: "all", "large_airport",
    // "medium_airport", and by distance from the airline's home base "d0"
    // (< 1,500 km), "d1" (< 4,000 km), "d2" (farther).
    std::map<std::string, std::map<std::string, Mix>> types;
    std::map<std::string, std::map<std::string, Mix>> brands;  // brand → country → operator → weight
    std::map<std::string, std::pair<double, double>>  home;    // code → home base lat, lon
    std::map<std::string, Mix>                        cargo;   // "cargo_fleets": operator → freighter types
    // "airport_types": hand-checked types of one airline at one airport
    // (pipeline/fleet_handmade.json; ARG at SAAV: E190).
    std::map<std::string, std::map<std::string, Mix>> airportTypes;   // code → icao → types
};


namespace WikiFallback {

// From this many adsb.lol stays on, an airport's own table is used alone;
// below it the table is blended with the Wikipedia list in proportion
// (samples / kFullTrustSamples of the mix from adsb.lol).
constexpr int kFullTrustSamples = 500;

// The fleet mix an airline flies to an airport at (lat, lon): its
// distance-band mix from its home base when known (KLM at Delhi: its
// long-haul 777s / 787s, not its European 737s), split by airport size
// ("d0/large_airport": Air India's 777s at big Indian airports, not at small
// ones), at its home base itself (within kHomeRadiusKm: its main hub,
// pipeline/build_wiki.py) its whole fleet — "fleet", the aircraft in service
// from its Wikipedia fleet table, else "all" — else by airport size, else
// everything. nullptr if none. The radius was 300 km, which gave a hub's
// neighbours (Hongqiao, Hangzhou next to Pudong) the hub's widebodies; 10 km
// predicts the types parked at adsb.lol airports slightly better (2026-10-27,
// leave-one-airport-out over 3,980 airline-airport pairs: median 69.9 → 70.2 %).
constexpr double kHomeRadiusKm = 10.0;
const Fleets::Mix* fleetMixFor(const Fleets& fleets, const std::string& code, const std::string& airportType,
                               double lat, double lon);

// Airports near an airline's hub where it flies widebodies on short routes
// adsb.lol cannot see (Chinese trunk routes, Hanoi–Ho Chi Minh City, Saudi
// domestic; pipeline/fleet_handmade.json "widebody_share", estimates): the
// widebody share of `types` there is set to the given value, with the
// airline's own widebody types if the mix has none. Unchanged elsewhere.
void applyWidebodyShare(Fleets::Mix& types, const Fleets& fleets, const std::string& code, const std::string& icao);

// Where combine() takes an airline's types from, in order (2026-10-28):
//   1. hand-checked types for this airline at this airport (Fleets::airportTypes);
//   2. at a small airport (isSmall), its "small_airport" mix: what it flies to
//      airports under 15 airline arrivals a day (Delta 717s, American A319s);
//   3. fleetMixFor.
// Then types that cannot fly the distance from the airline's home base
// (typicalRangeKm) are dropped (an IndiGo ATR 72 at
// Tashkent, 3,300 km from Bengaluru; its fleet's 1,500–4,000 km band holds
// ATR sectors flown from other bases). Nothing left → no types (the airline
// cannot be drawn there).
// A small airport: its estimated aircraft on the ground (onGroundEst) under
// kSmallOnGround, else (unknown) a route score under kSmallScore and not a
// large airport.
constexpr float kSmallOnGround = 1.0f;
constexpr float kSmallScore    = 12.0f;
bool isSmall(const WikiEntry& wiki);
// A typical maximum range for regional types (turboprops ~1,500 km, regional
// jets 2,600–4,800 km); 0 = not limited (mainline jets).
double typicalRangeKm(const std::string& icaoType);

// One airport's entry of wiki_airlines.json (or its alias). nullopt if the
// file or the airport is missing, or the schema differs.
std::optional<WikiEntry> loadEntry(const std::filesystem::path& file, const std::string& icao);

// An airport with no data of its own (YSWS, opened 2026): the nearest airport
// of wiki_airlines.json within maxKm of (lat, lon), not `self`; "" if none.
constexpr double kNeighbourKm = 150.0;
std::string nearestWithData(const std::filesystem::path& file, double lat, double lon, const std::string& self,
                            double maxKm = kNeighbourKm, double* distKm = nullptr);

// The whole of airline_fleets.json. nullopt if missing or malformed.
std::optional<Fleets> loadFleets(const std::filesystem::path& file);

// The entry's airlines as operator codes: each brand's weight split by the
// brand's split for the entry's country (else "*"). Normalized to sum 1;
// brands with no known operators are dropped.
std::map<std::string, float> operators(const WikiEntry& wiki, const Fleets& fleets);

// The mix to plan with. `adsb` must already have passed the kMinSamples
// check (or be nullopt). Returns `adsb` unchanged when it has at least
// kFullTrustSamples stays or there is no usable Wikipedia entry; otherwise
// the blend described above, with every airline lacking a type list given
// the operator's fleet mix for the airport's size (else its overall mix).
// `wikiShare` (optional) receives the Wikipedia part of the airline mix, 0–1.
// lat / lon: the airport's position (for fleetMixFor); NaN = unknown.
std::optional<AirportTable> combine(std::optional<AirportTable> adsb,
                                    const WikiEntry*            wiki,
                                    const Fleets*               fleets,
                                    float*                      wikiShare = nullptr,
                                    double                      lat = std::nan(""),
                                    double                      lon = std::nan(""));

} // namespace WikiFallback
