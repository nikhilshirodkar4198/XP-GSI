#pragma once
// ModelSelector: pick the best CSL model from a CslDb for a given Stand.
//
// Selection priority (each step collected then indexed by seed). Only
// airliner types (IcaoSize::isAirliner) and never larger than the stand:
//   1. Airline + exact size match
//   2. Airline match, smaller size
//   3. Generic (no airline) exact size match
//   4. Any exact size
//   5. Smaller sizes, nearest first
//   6. nullptr → nothing fits; the stand stays empty
//
// Pure logic, no XPLM calls: safe on a worker thread. It walks the whole
// catalogue per stand, so run it off the main thread for real CSL libraries.

#include "csl/CslDb.h"
#include "data/AirportData.h"
#include "engine/StandState.h"
#include "engine/TerminalZones.h"
#include "csl/RelatedTypes.h"
#include "data/RegionalBrands.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

namespace ModelSelector {

// Special-livery variants already placed at the current airport.
using VariantSet = std::unordered_set<const CslVariant*>;

// Airline → the types it flies today (data/airline_fleets.json: the types
// adsb.lol saw it fly in 2026, with each airframe under the airline whose
// paint it wears, and its Wikipedia fleet table).
using FleetTypes = std::map<std::string, std::set<std::string>>;
// A livery that may be drawn without an airport table: a scheme in use
// (Era::Current), and plain paint or an airline that flies the type today
// (a 747 and an old Aeroflot type at VOGA's gates).
bool inService(const CslVariant& v, const std::string& type, const FleetTypes& fleets);

// Result of table-driven selection, with the step that produced it.
struct Choice {
    const CslVariant* model = nullptr;
    std::string       airline;     // chosen operator (may differ from model->airlineCode)
    std::string       type;        // chosen ICAO type
    enum class Reason {
        ExactLivery,       // airline + type, airline's own livery
        AirlineOtherType,  // airline's livery on another of its (fitting) types
        GenericLivery,     // chosen type, generic / standard livery
        AnyLivery,         // no paint of the airline or plain paint: no model (never another airline's)
        NoTableMatch,      // no airline of the table fits: no model, the stand stays empty
        PartnerBrand,      // a regional (SkyWest, Republic, …) in its partner's paint
    } reason = Reason::NoTableMatch;
    bool fromArea = false;  // airline/type came from where aircraft park near this stand
    bool fromZone = false;  // ... else from what parks at the stand's terminal (TerminalZones)
};

// Parking evidence around one stand (AirportTable::clusters), weighted by
// distance: w = exp(-(d / kAreaSigmaM)²), nothing beyond kAreaCutoffM.
// With at least kMinAreaEvidence weighted stays nearby, the stand takes its
// airlines and types only from what actually parks there, so a terminal's
// airlines stay at that terminal (live test: Southwest at SMF Terminal A and
// at SFO's international gates). Otherwise the airport-wide mix is used.
constexpr double kAreaSigmaM      = 150.0;
constexpr double kAreaCutoffM     = 450.0;
constexpr float  kMinAreaEvidence = 3.0f;
// First the stand's own neighbourhood (σ 60 m, nothing beyond 180 m), the
// 150 / 450 m area only where that holds under kMinAreaEvidence
// (at KSFO's Terminal 2 south gates, Southwest's, the wide area
// also counted United's aircraft at the gates 100–200 m north).
constexpr double kAreaNearSigmaM  = 60.0;
constexpr double kAreaNearCutoffM = 180.0;

struct AreaMix {
    float                                               evidence = 0;   // Σ w · n
    std::map<std::string, float>                        airlines;
    std::map<std::string, std::map<std::string, float>> types;
};
// With `zones`, only clusters in the stand's own terminal zone count (when
// the stand is in one), so evidence never leaks across to the next terminal.
AreaMix areaMix(const Stand& stand, const AirportTable& table,
                const TerminalZones::Zones* zones = nullptr);

// Optional inputs of selectWithTable, all per airport.
struct Extras {
    const TerminalZones::Zones*  zones   = nullptr;  // which terminal a stand serves
    const RelatedTypes*          related = nullptr;  // look-alike types for ones the CSL lacks
    const RegionalBrands*        brands  = nullptr;  // regionals that fly in partner paint
    std::map<std::string, float> brandPresence;      // RegionalBrands::presence at this airport
    // RegionalBrands::partnersAt this airport: operator → partner weights that
    // replace the partners parked around the stand (a stand override's brand
    // still wins).
    std::map<std::string, std::map<std::string, float>> airportPartners;
    // Airlines that already show their share of the stands (kMaxShareFactor):
    // not chosen again while another airline fits.
    const std::set<std::string>* full = nullptr;
    // Airlines that already show as many aircraft as are really parked here
    // at once (rareAirlineCap): never chosen again, not even when every
    // other airline is full or on a stand kept for visitors.
    const std::set<std::string>* capped = nullptr;
    // data/retired_types.json: airline → types it no longer flies (never
    // chosen or drawn for it, also not as a look-alike).
    std::map<std::string, std::set<std::string>> retiredTypes;
    // Longest paved runway (AptDat): types needing more (IcaoSize::minRunwayM)
    // are never chosen. 0 = unknown, no limit.
    double longestRunwayM = 0;
    // Long-haul visitors (longHaulVisitors) and whether this stand is one
    // kept for them: then only they are chosen, if any can be drawn here.
    const std::map<std::string, float>* visitors = nullptr;
    bool                                visitorsOnly = false;
};

// Long-haul visitors: airlines under kVisitorMaxShare of the airport's
// traffic whose aircraft here are at least kVisitorWideShare widebodies
// (size E or F) — at KDFW Korean, Turkish, JAL, Emirates, Lufthansa, Air
// France, EVA, Qatar, Finnair; not American. Their weights from the table.
constexpr float kVisitorMaxShare  = 0.05f;
constexpr float kVisitorWideShare = 0.5f;
std::map<std::string, float> longHaulVisitors(const AirportTable& table);

// Widebody stands kept for long-haul visitors ("at least
// one international flight"): the visitors on the ground at an average
// moment — their arrivals a day × kVisitorGroundHours / 24 — rounded, at
// least 1 from one arrival a day, at most kMaxVisitorStands; 0 without.
constexpr float       kVisitorGroundHours = 4.0f;
// A stand is kept for visitors only where they really park: at least this
// much parking evidence of them around it (ModelSelector::areaMix); and on
// such a stand only visitors parked there are chosen. Without any such stand
// nothing is kept.
constexpr float       kVisitorMinEvidence = 2.0f;

// An airline shows at most kMaxShareFactor × its share of the airport's mix ×
// the stands (at least one). The mix is weighted by hours on the ground
// (pipeline/build_tables.py), so its share is the airline's share of the
// aircraft parked at a moment; drawing stand by stand can otherwise put a
// rare airline on several (three Etihad 787s at KORD, four
// Singapore 787s at VTBS where 0.6 are on the ground at once).
constexpr float       kMaxShareFactor = 1.5f;
// The share cap counts the stands placed so far (the plan is filled in order
// and shown as a prefix), not all stands of the plan: at RJBB the old count
// over 73 planned stands allowed Finnair three (ceil(1.5 × 2.5 % × 73)) of
// the ~24 shown.
int shareCap(float share, std::size_t placedSoFar);
// A rare airline — fewer than kRareBelow of its aircraft parked at once
// (share × Density::parkedAtOnce) — shows at most max(1, round(kMaxShareFactor
// × that)) aircraft (three Finnair A350s at RJBB, where its
// one daily A350 means about 0.6 parked at once); −1 = no hard cap.
constexpr float       kRareBelow = 2.0f;
int rareAirlineCap(float share, float parkedAtOnce);
constexpr std::size_t kMaxVisitorStands   = 4;
std::size_t visitorStands(const std::map<std::string, float>& visitors, float dailyArrivals);

// Weight of a type at a stand by how well its size matches (e.g.
// Etihad A321s on widebody stands at OMAA): same code 1, one smaller 0.5, two
// smaller 0.15, three or more 0.05. Larger never fits.
float sizeMatchWeight(char typeSize, char standSize);

// A scenery's airline list for a stand (row 1301) is outdated or wrong when
// the airlines it names make up less than kMinListedShare of what the data
// sees parked around the stand (or at its terminal); the list is then ignored
// (Global Airports KLAS lists "ual dal aal" on Southwest's
// concourses). Without local data a list stands: a gate listing one small
// airline is usually right. listedShare: their share of `parked` (1 when
// nothing is parked).
constexpr float kMinListedShare = 0.25f;
float listedShare(const std::vector<std::string>& listed, const std::map<std::string, float>& parked);

// A list on at least kBlanketShare of an airport's airline stands (and at
// least kBlanketMinStands of them) says nothing about any one gate — X-Plane
// 12's Global Airports put one 13-airline European list on 81 EKCH stands and
// "AIC IGO" on 142 of VIDP's 163 — and is cleared from those stands (not
// from overridden ones). Returns the stands cleared.
constexpr double      kBlanketShare     = 0.5;
constexpr std::size_t kBlanketMinStands = 10;
std::size_t clearBlanketLists(std::vector<Stand>& airlineStands);

// data/retired_types.json → airline → retired types; empty if missing.
std::map<std::string, std::set<std::string>> loadRetiredTypes(const std::filesystem::path& file);

// Marks the airline's models of its retired types retired in the catalogue
// (so no path, not even size-only selection, draws them). Returns how many.
std::size_t retireTypes(CslDb& db, const std::map<std::string, std::set<std::string>>& retired);

// Airport-data driven selection (airline per stand → type →
// model). Airline, in order:
//   1. the stand's 1301 airlines (weighted by the table when listed there);
//   2. what parks around the stand (areaMix);
//   3. what parks at the stand's terminal (zones->mixFor);
//   4. the table's airport-wide weights, without airlines based at another
//      terminal (zones->basedElsewhere) unless that leaves none.
// Type = that airline's type weights from the same source, each type the CSL
// library lacks replaced by its look-alike (related), filtered to types that
// fit the stand's size code. Model: a regional that flies for partners
// (brands) takes a partner's paint — partner by brand presence at the
// airport, then a livery showing that brand (the regional's own first), or
// the partner's own livery on that type — and never a livery of its own;
// otherwise exact livery → the airline's livery on another of its types →
// generic livery of the type → any livery of the type. Retired paint is never
// used. When the table offers no airline that fits, no model (never a
// size-only pick of any airline's paint). Deterministic for a given seed.
Choice selectWithTable(const Stand&        stand,
                       const CslDb&        db,
                       const AirportTable& table,
                       std::uint32_t       seed,
                       const VariantSet*   usedSpecials = nullptr,
                       const Extras*       extras = nullptr);

// Airlines in the table without a type list (one listed by Wikipedia that the
// adsb.lol data never saw parked): every airliner type the CSL library has
// that airline's livery for, equally weighted. Without types an airline is
// never chosen. Returns how many airlines were given types.
// Only liveries of a scheme in use, and — when `fleets` knows the airline —
// only types it flies today.
std::size_t fillTypesFromLiveries(AirportTable& table, const CslDb& db, const FleetTypes* fleets = nullptr);

// A cargo stand (the scenery's 1301 "cargo" stands): a cargo
// airline from the table's cargo mix (cargoAirlines / cargoTypes), on a type
// that fits the stand and runway, and only cargo paint (CslVariant::cargo) of
// that airline — FedEx paint for FedEx, Etihad Cargo for Etihad's 777Fs. An
// airline the CSL library has no such paint for is skipped; nothing → no model.
Choice selectCargo(const Stand&        stand,
                   const CslDb&        db,
                   const AirportTable& table,
                   std::uint32_t       seed,
                   const Extras*       extras = nullptr);

// A livery with no airline code. (X-CSL's "@SN", "@ID", … are government and
// air-force aircraft, retired by the overrides' state_code_prefixes, user
// 2026-10-27 — not generic paint.)
bool isGenericLivery(const CslVariant& v);

// Returns the best-matched variant (pointer into db), or nullptr if db is empty.
// `seed` indexes into the candidate list within each priority bucket, giving
// deterministic per-stand variety when multiple variants qualify.
// usedSpecials: special-livery variants already placed at this airport; they
// are never chosen again (one aircraft per special scheme). May be null.
// fleets: only liveries inService (no airport data: VOGA before its 1302
// icao_code was read); nullptr = any.
const CslVariant* selectForStand(const Stand&      stand,
                                 const CslDb&      db,
                                 std::uint32_t     seed = 0,
                                 const VariantSet* usedSpecials = nullptr,
                                 const FleetTypes* fleets = nullptr);

// FNV-1a hash over the bit patterns of the stand's lat and lon. A stable
// per-stand seed that doesn't change when apt.dat stand ordering changes.
std::uint32_t positionSeed(const Stand& stand);

} // namespace ModelSelector
