#pragma once
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine/StandState.h"

// apt.dat index builder and airport parser.
//
// Row format references used in this module:
//   Row 1/16/17  — airport header
//   Row 1300     — ramp start
//   Row 1301     — ramp metadata (optional, immediately follows 1300)
// All column layouts verified against the apt.dat 1100 spec:
//   developer.x-plane.com/article/airport-data-apt-dat-file-format-specification/
namespace AptDat {

// ── Index types ───────────────────────────────────────────────────────────────

struct IndexEntry {
    std::filesystem::path file;       // absolute path to the apt.dat
    std::streamoff        offset = 0; // byte offset of the row-1/16/17 line
    double                refLat = 0; // approximate airport position (first 1300 seen)
    double                refLon = 0;
    std::string           sourcePack; // "Global Airports" or pack folder stem
    // The header ID when the airport is indexed under its 1302 icao_code
    // instead ("XVI000D" for VOGA; "UTTT" for Tashkent, since 2024 UZTT, which
    // the shipped data still knows as UTTT); "" otherwise. Data lookups try
    // the key first, then this.
    std::string           aptId;
};

// ICAO → location. First-pack-wins across scenery pack priorities. The key is
// the airport's "1302 icao_code" when it has one, else its header ID.
using AptIndex = std::unordered_map<std::string, IndexEntry>;

// ── Per-airport parse result ──────────────────────────────────────────────────

struct ParsedAirport {
    std::string           icao;         // the 1302 icao_code when the row has one, else the header ID
    std::string           aptId;        // the header ID when it differs ("XVI000D" for VOGA), else ""
    std::string           name;
    std::filesystem::path sourceFile;
    std::string           sourcePack;
    std::string           fingerprint;  // from Fingerprint::compute()
    std::vector<Stand>    stands;
    double                longestRunwayM = 0;   // longest paved land runway (row 100); 0 = none / unknown
};

// ── Public API (all worker-thread safe; no XPLM calls) ───────────────────────

// Build the merged AptIndex by scanning all enabled scenery packs in priority
// order, falling back to the global airports. Results are cached in
// cacheDir/apt_index.json and reused until any scanned file's mtime/size changes.
//
// xpVer: from XPLMGetVersions (determines XP11 vs XP12 global-airports path).
AptIndex buildIndex(
    const std::filesystem::path& xplaneRoot,
    int                          xpVer,
    const std::filesystem::path& cacheDir);

// Seek to entry.offset in entry.file and parse the airport's ramp starts.
// Does NOT rescan the whole apt.dat.
ParsedAirport parseAirport(const IndexEntry& entry);

// Airline-tagged stands that are clearly not passenger stands become "none"
// ("cargo" for cargo / freight names). EgyptAir on Douala's
// (FKKD) "Storage 1–4", tagged airline, among the military tie-downs ~1 km
// from the terminal. Either
//  * the name says what the stand is for: storage, maintenance / MRO, hangar,
//    military / air force, presidential / VIP / government, de-icing,
//    run-up, wash, GA / FBO / business aviation; or
//  * not a gate, more than kRemoteFromGateM from every airline gate, and a
//    military / GA / none stand nearer than any airline gate.
// Parking evidence (engine/StandEvidence) still decides where it exists.
// Returns how many stands were changed.
constexpr double kRemoteFromGateM = 600.0;
std::size_t demoteNonPassenger(std::vector<Stand>& stands);

// The global airports apt.dat (XP11: Custom Scenery/Global Airports, XP12:
// Global Scenery/Global Airports).
std::filesystem::path globalAptPath(const std::filesystem::path& xplaneRoot, int xpVer);

// One airport's entry in one apt.dat (scans the whole file). nullopt if the
// file lacks it.
std::optional<IndexEntry> findAirport(const std::filesystem::path& file, const std::string& packName,
                                      const std::string& icao);

// Scenery packs whose DSF X-Plane failed to load (missing art assets — e.g.
// an X-Plane 12 pack in X-Plane 11), from the text of X-Plane's Log.txt:
//   "E/SCN: DSF canceled due to missing art assets, file=Custom Scenery/<pack>/…"
//   "E/SCN: WARNING: got error on DSF load for scenery file …, Custom Scenery/<pack>/"
// X-Plane then draws the default (Global Airports) layout of that airport, so
// the plugin must place by the Global Airports stands too (live test KATL,
// Nimbus "KATL V3.5 - Atlanta XP12" in X-Plane 11, 2026-10-26).
std::set<std::string> brokenPacks(const std::string& logText);

} // namespace AptDat
