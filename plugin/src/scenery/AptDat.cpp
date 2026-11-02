#include "AptDat.h"
#include "Fingerprint.h"
#include "SceneryPacks.h"
#include "core/Cancel.h"
#include "core/Log.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cinttypes>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <system_error>

// ── Row format reference ─────────────────────────────────────────────────────
// All column layouts verified against the apt.dat 1100 spec:
//   developer.x-plane.com/article/airport-data-apt-dat-file-format-specification/
//
// Row 1/16/17 (airport header):
//   <code> <elev_ft> <dep1> <dep2> <icao_id> <name...>
//   [0]    [1]       [2]    [3]    [4]        [5+]
//
// Row 1300 (ramp start):
//   1300 <lat> <lon> <hdg> <type> <equip_classes> <name...>
//   [0]  [1]   [2]   [3]   [4]    [5]              [6+]
//   type:   gate | hangar | tie_down | misc
//   equip:  heavy|jets|turboprops|props|helos|fighters|skydive  (| separated) or "all"
//
// Row 1301 (ramp metadata, optional, immediately follows 1300):
//   1301 <size_code> <op_type> [<airline> <airline> ...]
//   [0]  [1]         [2]       [3...]
//   size:     A–F (ICAO Annex 14 reference code letter)
//   op_type:  none | general_aviation | airline | cargo | military
//   airlines: zero or more 3-letter ICAO codes, space-separated, lowercase
// Corrected 2026-10-25: the Phase 1 parser had [1]=airlines [2]=size
// [3]=op and dropped 3-token rows. The layout above is the one in every
// real file checked: all 141 772 1301 rows of XP11 Global Airports
// ([2] ∈ {general_aviation, airline, none, military, cargo}; e.g.
// "1301 B airline grl fxi bff") and Aerosoft EGLL ("1301 C airline baw").

namespace AptDat {
namespace {

// ── Token helpers ─────────────────────────────────────────────────────────────

// Split a line on ASCII whitespace, returning up to maxTokens tokens.
// If more tokens remain, the last token contains the remainder of the line.
std::vector<std::string> tokenise(const std::string& line, int maxTokens = 32) {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string tok;
    while (tokens.size() < static_cast<size_t>(maxTokens - 1) && (ss >> tok))
        tokens.push_back(tok);
    // Remainder of line (handles stand names with spaces)
    std::string rest;
    std::getline(ss >> std::ws, rest);
    if (!rest.empty()) tokens.push_back(rest);
    return tokens;
}

// Return the first whitespace-delimited token from a line.
std::string firstToken(const std::string& line) {
    auto end = line.find_first_of(" \t");
    return (end == std::string::npos) ? line : line.substr(0, end);
}

// ── Parse helpers ─────────────────────────────────────────────────────────────

// Extract ICAO (field [4]) and name (fields [5+]) from an airport header row.
bool parseHeader(const std::vector<std::string>& tok,
                 std::string& icao, std::string& name) {
    if (tok.size() < 5) return false;
    icao = tok[4];
    if (icao.empty()) return false;
    // Normalise to upper-case (some hand-edited files use lower-case)
    for (auto& c : icao) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    name = (tok.size() > 5) ? tok[5] : "";
    return true;
}

// Row 1302 "icao_code <code>" (apt.dat 1100 spec, metadata rows): the
// airport's ICAO code where its ID is not one. Gateway airports without an
// ICAO code at submission keep a placeholder ID: X-Plane 12's Manohar Intl
// (Goa, opened 2023) is "XVI000D" with "1302 icao_code VOGA"
// (a size-only fill of random paint there). Accepted when it is
// four letters or digits starting with a letter.
bool parseIcaoCodeRow(const std::string& line, std::string& out) {
    static const std::string kKey = "1302 icao_code";
    if (line.compare(0, kKey.size(), kKey) != 0) return false;
    auto tok = tokenise(line, 4);
    if (tok.size() < 3 || tok[1] != "icao_code" || tok[2].size() != 4) return false;
    std::string code = tok[2];
    for (auto& c : code) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(code[0]))) return false;
    out = std::move(code);
    return true;
}

// Convert apt.dat ramp type string → StandType.
StandType parseStandType(const std::string& s) {
    if (s == "gate")     return StandType::Gate;
    if (s == "tie_down" || s == "tie-down") return StandType::TieDown;
    if (s == "hangar")   return StandType::Hangar;
    return StandType::Misc;
}

// Convert pipe-separated equipment class token → allowedClasses bitmask.
uint32_t parseEquip(const std::string& s) {
    if (s == "all" || s.empty()) return EquipClass::All;
    uint32_t mask = 0;
    // Walk the pipe-separated list without allocating a sub-string per token.
    const char* p = s.c_str();
    while (*p) {
        const char* sep = std::strchr(p, '|');
        std::string_view tok(p, sep ? (sep - p) : std::strlen(p));
        if      (tok == "heavy")      mask |= EquipClass::Heavy;
        else if (tok == "jets")       mask |= EquipClass::Jets;
        else if (tok == "turboprops") mask |= EquipClass::Turboprops;
        else if (tok == "props")      mask |= EquipClass::Props;
        else if (tok == "helos")      mask |= EquipClass::Helos;
        else if (tok == "fighters")   mask |= EquipClass::Fighters;
        else if (tok == "skydive")    mask |= EquipClass::Skydive;
        if (!sep) break;
        p = sep + 1;
    }
    return mask ? mask : EquipClass::All;
}

// Heuristic: infer ICAO size code from allowedClasses when row 1301 is absent.
// Documented rule: take the largest class present as the determining factor.
//   heavy       → 'E'  (wide-body: B747, B777, A330; wingspan 52–65 m)
//   jets only   → 'C'  (narrowbody: B737, A320; wingspan 24–36 m)
//   turboprops  → 'B'  (regional turboprop; wingspan 15–24 m)
//   props/helos → 'A'  (light GA; wingspan < 15 m)
//   fallback    → 'C'  (assume narrowbody for unknown airline gates)
char inferWidthCode(uint32_t equip) {
    if (equip & EquipClass::Heavy)      return 'E';
    if (equip & EquipClass::Jets)       return 'C';
    if (equip & EquipClass::Turboprops) return 'B';
    if (equip & EquipClass::Props)      return 'A';
    if (equip & EquipClass::Helos)      return 'A';
    return 'C';
}

// Convert op_type string from row 1301 → OpType.
OpType parseOpType(const std::string& s) {
    if (s == "airline")          return OpType::Airline;
    if (s == "cargo")            return OpType::Cargo;
    if (s == "general_aviation") return OpType::GA;
    if (s == "military")         return OpType::Military;
    if (s == "none")             return OpType::None;
    return OpType::Unknown;
}

// Parse a row 1300 line into a Stand (without 1301 metadata).
Stand parse1300(const std::vector<std::string>& tok) {
    Stand s;
    if (tok.size() < 7) return s;
    // Fields: [0]=1300 [1]=lat [2]=lon [3]=hdg [4]=type [5]=equip [6+]=name
    try {
        s.lat        = std::stod(tok[1]);
        s.lon        = std::stod(tok[2]);
        s.headingDeg = std::stof(tok[3]);
    } catch (...) { return s; }
    s.type           = parseStandType(tok[4]);
    s.allowedClasses = parseEquip(tok[5]);
    s.id             = tok[6]; // already the joined remainder from tokenise()
    return s;
}

// Apply row 1301 metadata to an existing Stand (in-place).
void apply1301(const std::vector<std::string>& tok, Stand& s) {
    if (tok.size() < 3) return;
    // Fields: [0]=1301 [1]=size_code [2]=op_type [3...]=airlines
    if (tok[1].size() == 1) {
        char sz = static_cast<char>(std::toupper(static_cast<unsigned char>(tok[1][0])));
        if (sz >= 'A' && sz <= 'F') s.widthCode = sz;
    }
    s.op = parseOpType(tok[2]);
    // Airline codes, upper-cased to match ICAO designators in CSL packages
    // and the airport tables. ('|' also accepted as a separator, defensively.)
    for (size_t i = 3; i < tok.size(); ++i) {
        const char* p = tok[i].c_str();
        while (*p) {
            const char* sep = std::strchr(p, '|');
            std::string code(p, sep ? (sep - p) : std::strlen(p));
            for (auto& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!code.empty() && code != "NONE") s.airlines.push_back(std::move(code));
            if (!sep) break;
            p = sep + 1;
        }
    }
}

// Post-parse defaults: infer widthCode if not set by 1301; a gate with no
// operation type, or "none" (WED's default: no specific use, not "no
// aircraft"), is an airline gate. Nimbus KORD marks 165 of its 173 gates
// "none", FlyTampa EKCH all of them (stand audit, 2026-10-27).
void inferDefaults(Stand& s) {
    if (s.widthCode == '?')
        s.widthCode = inferWidthCode(s.allowedClasses);
    if ((s.op == OpType::Unknown || s.op == OpType::None) && s.type == StandType::Gate)
        s.op = OpType::Airline;
}

// ── Per-file index scan ───────────────────────────────────────────────────────

// First-pass scan: find all airport headers in one apt.dat file and insert them
// into `index` using first-pack-wins semantics.
// Also records refLat/refLon from the first row-1300 seen per airport.
void scanFile(const std::filesystem::path& file,
              const std::string&           packName,
              AptIndex&                    index) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        Log::warn("AptDat: cannot open %s", file.u8string().c_str());
        return;
    }

    std::string    line;
    std::string    curIcao, curId;
    std::streamoff curOffset  = 0;
    double         curLat     = 0.0, curLon = 0.0;
    bool           hasRef     = false;

    auto flushAirport = [&]() {
        if (!curIcao.empty() && index.find(curIcao) == index.end()) {
            index.emplace(curIcao, IndexEntry{file, curOffset, curLat, curLon, packName,
                                              curId != curIcao ? curId : std::string{}});
        }
    };

    while (!Cancel::requested()) {
        // Record offset BEFORE reading the line so we can seek back to it.
        std::streamoff lineStart = in.tellg();
        if (!std::getline(in, line)) {
            flushAirport();
            break;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string code = firstToken(line);
        if (code.empty()) continue;

        if (code == "1" || code == "16" || code == "17") {
            flushAirport();
            auto tok = tokenise(line, 8);
            std::string icao, name;
            if (parseHeader(tok, icao, name)) {
                curIcao  = std::move(icao);
                curId    = curIcao;
                curOffset = lineStart;
                hasRef   = false;
                curLat   = 0.0; curLon = 0.0;
            } else {
                curIcao.clear();
            }
        } else if (code == "1302" && !curIcao.empty()) {
            // Indexed under its real ICAO code (parseIcaoCodeRow); the header
            // ID only when it has none. A pack's own airport of that code
            // still wins over Global Airports (first pack wins).
            parseIcaoCodeRow(line, curIcao);
        } else if (code == "1300" && !curIcao.empty() && !hasRef) {
            auto tok = tokenise(line, 8);
            if (tok.size() >= 3) {
                try {
                    curLat = std::stod(tok[1]);
                    curLon = std::stod(tok[2]);
                    hasRef = true;
                } catch (...) {}
            }
        } else if (code == "99") {
            flushAirport();
            break;
        }
    }
}

// ── Cache helpers ─────────────────────────────────────────────────────────────

struct CachedFile {
    std::filesystem::path  file;
    uintmax_t              sizeBytes;
    int64_t                mtimeUnix; // seconds since epoch
    std::string            packName;
    // airports: ICAO → (offset, refLat, refLon)
    std::unordered_map<std::string,
        std::tuple<std::streamoff, double, double>> airports;
};

// Return file size and a stable mtime tick count, or {0,0} on error.
// Uses file_time_type::time_since_epoch() which is C++17-compatible.
// The value is implementation-defined (not necessarily Unix time) but is
// consistent on the same system, making it suitable for cache invalidation.
static std::pair<uintmax_t, int64_t> fileMeta(const std::filesystem::path& p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    if (ec) return {0, 0};
    auto lwt = std::filesystem::last_write_time(p, ec);
    if (ec) return {0, 0};
    // duration_cast to avoid narrowing; count() gives a stable integer.
    auto ticks = std::chrono::duration_cast<std::chrono::seconds>(
                     lwt.time_since_epoch()).count();
    return {sz, static_cast<int64_t>(ticks)};
}

// Schema 2: the X-Plane root the index was built for. A plugin folder copied
// from another install (XP11 → XP12) brings its cache along; it must not be used.
// Schema 3: airports keyed by their 1302 icao_code (parseIcaoCodeRow).
static constexpr int kCacheSchema = 3;

static AptIndex loadCache(const std::filesystem::path& cacheFile,
                          const std::filesystem::path& xplaneRoot,
                          const std::vector<std::pair<std::filesystem::path,std::string>>& scanList) {
    AptIndex empty;
    std::ifstream in(cacheFile);
    if (!in.is_open()) return empty;

    nlohmann::json j;
    try { j = nlohmann::json::parse(in); }
    catch (...) { return empty; }

    if (j.value("schema", 0) != kCacheSchema) {
        Log::info("AptDat: cache from an older XP-GSI — rebuilding");
        return empty;
    }
    const std::string root = xplaneRoot.lexically_normal().generic_u8string();
    if (j.value("xplane_root", std::string()) != root) {
        Log::info("AptDat: cache built for '%s', not this X-Plane ('%s') — rebuilding",
                  j.value("xplane_root", std::string()).c_str(), root.c_str());
        return empty;
    }
    // Every apt.dat to scan must be in the cache (a pack added since), and no other.
    if (j.value("files", nlohmann::json::array()).size() != scanList.size()) {
        Log::info("AptDat: scenery packs changed — rebuilding the index");
        return empty;
    }

    // Build a map from path→(size,mtime) for the files we intend to scan
    std::unordered_map<std::string, std::pair<uintmax_t,int64_t>> expected;
    for (const auto& [f, _] : scanList) {
        auto key = f.u8string();
        expected[key] = fileMeta(f);
    }

    AptIndex index;
    for (const auto& entry : j.value("files", nlohmann::json::array())) {
        std::string path = entry.value("file", "");
        uintmax_t   size = entry.value("size_bytes", uint64_t(0));
        int64_t     mt   = entry.value("mtime_unix", int64_t(0));
        std::string pack = entry.value("pack", "");

        // Validate file still matches
        auto it = expected.find(path);
        if (it == expected.end()) return empty; // unexpected file in cache
        auto [curSize, curMt] = it->second;
        if (curSize != size || curMt != mt) return empty; // file changed

        for (const auto& apt : entry.value("airports", nlohmann::json::array())) {
            std::string  icao   = apt.value("icao", "");
            std::streamoff off  = apt.value("offset", int64_t(0));
            double         lat  = apt.value("ref_lat", 0.0);
            double         lon  = apt.value("ref_lon", 0.0);
            if (!icao.empty() && index.find(icao) == index.end()) {
                index.emplace(icao, IndexEntry{std::filesystem::u8path(path), off, lat, lon, pack,
                                               apt.value("apt_id", std::string{})});
            }
        }
    }
    Log::info("AptDat: loaded %zu airports from cache", index.size());
    return index;
}

static void saveCache(const std::filesystem::path& cacheFile,
                      const std::filesystem::path& xplaneRoot,
                      const std::vector<std::pair<std::filesystem::path,std::string>>& scanList,
                      const AptIndex& index) {
    std::error_code ec;
    std::filesystem::create_directories(cacheFile.parent_path(), ec);

    // Invert index: group airports back by file
    std::unordered_map<std::string, nlohmann::json> byFile;
    for (const auto& [icao, e] : index) {
        auto key = e.file.u8string();
        byFile[key]["icao"] = icao;  // placeholder — we rebuild below
    }

    // Build file entries preserving scan order
    nlohmann::json files = nlohmann::json::array();
    for (const auto& [file, pack] : scanList) {
        auto [sz, mt] = fileMeta(file);
        nlohmann::json apts = nlohmann::json::array();
        for (const auto& [icao, e] : index) {
            if (e.file == file) {
                nlohmann::json a{
                    {"icao",    icao},
                    {"offset",  static_cast<int64_t>(e.offset)},
                    {"ref_lat", e.refLat},
                    {"ref_lon", e.refLon}
                };
                if (!e.aptId.empty()) a["apt_id"] = e.aptId;
                apts.push_back(std::move(a));
            }
        }
        files.push_back({
            {"file",       file.u8string()},
            {"size_bytes", static_cast<uint64_t>(sz)},
            {"mtime_unix", mt},
            {"pack",       pack},
            {"airports",   std::move(apts)}
        });
    }

    nlohmann::json j{{"schema", kCacheSchema},
                     {"xplane_root", xplaneRoot.lexically_normal().generic_u8string()},
                     {"files", std::move(files)}};
    std::ofstream out(cacheFile);
    if (out.is_open()) out << j.dump(2);
}

} // namespace (anonymous)

// ── Public API ────────────────────────────────────────────────────────────────

std::filesystem::path globalAptPath(const std::filesystem::path& xplaneRoot, int xpVer) {
    return (xpVer >= 12000)
        ? xplaneRoot / "Global Scenery"  / "Global Airports" / "Earth nav data" / "apt.dat"
        : xplaneRoot / "Custom Scenery"  / "Global Airports" / "Earth nav data" / "apt.dat";
}

std::set<std::string> brokenPacks(const std::string& text) {
    std::set<std::string> out;
    const std::string kPrefix = "Custom Scenery/";
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (line.find("DSF canceled") == std::string::npos &&
            line.find("error on DSF load") == std::string::npos)
            continue;
        const std::size_t p = line.find(kPrefix);
        if (p == std::string::npos) continue;
        const std::size_t start = p + kPrefix.size();
        const std::size_t slash = line.find('/', start);
        if (slash != std::string::npos && slash > start) out.insert(line.substr(start, slash - start));
    }
    return out;
}

std::optional<IndexEntry> findAirport(const std::filesystem::path& file, const std::string& packName,
                                      const std::string& icao) {
    AptIndex one;
    scanFile(file, packName, one);   // whole file; fine for a rare fallback on the worker
    auto it = one.find(icao);
    if (it == one.end()) return std::nullopt;
    return it->second;
}

AptIndex buildIndex(const std::filesystem::path& xplaneRoot,
                    int                          xpVer,
                    const std::filesystem::path& cacheDir) {
    // XP11/XP12 global-airports path difference.
    // XP12 path: Global Scenery/Global Airports/Earth nav data/apt.dat
    // XP11 path: Custom Scenery/Global Airports/Earth nav data/apt.dat
    // Source: X-Plane developer documentation and observed install layouts.
    // Verified: community consensus and plugin source (e.g. LiveTraffic, xPilot);
    // both confirmed on live installs (XP11.55; XP12.4.4 on 2026-10-28, whose
    // scenery_packs.ini lists "SCENERY_PACK *GLOBAL_AIRPORTS*").
    const std::filesystem::path globalApt = globalAptPath(xplaneRoot, xpVer);

    // Collect all apt.dat files to scan, in priority order: the packs as
    // scenery_packs.ini lists them, the Global Airports where it puts them
    // (XP12: the *GLOBAL_AIRPORTS* entry; XP11: their own Custom Scenery
    // entry), else last.
    std::vector<std::pair<std::filesystem::path, std::string>> scanList;
    bool globalAdded = false;
    auto addGlobal = [&] {
        if (globalAdded) return;
        globalAdded = true;
        std::error_code ec;
        if (std::filesystem::exists(globalApt, ec))
            scanList.emplace_back(globalApt, "Global Airports");
        else
            Log::warn("AptDat: global airports apt.dat not found at %s",
                      globalApt.u8string().c_str());
    };
    const std::string globalKey = globalApt.lexically_normal().generic_u8string();

    auto packs = SceneryPacks::loadEnabledPacks(xplaneRoot);
    for (const auto& pack : packs) {
        if (pack.filename() == SceneryPacks::kGlobalAirports) { addGlobal(); continue; }
        auto aptDat = pack / "Earth nav data" / "apt.dat";
        if (aptDat.lexically_normal().generic_u8string() == globalKey) { addGlobal(); continue; }
        std::error_code ec;
        if (std::filesystem::exists(aptDat, ec))
            scanList.emplace_back(aptDat, pack.stem().u8string());
    }
    addGlobal();

    // Try cache first
    auto cacheFile = cacheDir / "apt_index.json";
    auto cached    = loadCache(cacheFile, xplaneRoot, scanList);
    if (!cached.empty()) return cached;

    // Full rebuild
    Log::info("AptDat: building index from %zu apt.dat files …", scanList.size());
    AptIndex index;
    for (const auto& [file, pack] : scanList) {
        size_t before = index.size();
        scanFile(file, pack, index);
        if (Cancel::requested()) return {};   // stopping: no partial cache
        Log::debug("AptDat: %s — added %zu airports",
                   pack.c_str(), index.size() - before);
    }
    Log::info("AptDat: index complete — %zu airports total", index.size());

    saveCache(cacheFile, xplaneRoot, scanList, index);
    return index;
}

ParsedAirport parseAirport(const IndexEntry& entry) {
    ParsedAirport result;
    result.sourceFile = entry.file;
    result.sourcePack = entry.sourcePack;

    std::ifstream in(entry.file, std::ios::binary);
    if (!in.is_open()) {
        Log::warn("AptDat: cannot open %s for parsing", entry.file.u8string().c_str());
        return result;
    }

    in.seekg(entry.offset);
    if (!in.good()) {
        Log::warn("AptDat: seek failed in %s at offset %" PRId64,
                  entry.file.u8string().c_str(), static_cast<int64_t>(entry.offset));
        return result;
    }

    std::string line;

    // First line must be the airport header (row 1/16/17)
    if (!std::getline(in, line)) return result;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        // tokenise(line, 6): collect 5 fixed fields, remainder → tok[5] = full name.
        // Using 8 here truncated multi-word names to the first word (Phase 1 bug).
        auto tok = tokenise(line, 6);
        if (!parseHeader(tok, result.icao, result.name)) {
            Log::warn("AptDat: bad header at offset %" PRId64 " in %s",
                      static_cast<int64_t>(entry.offset),
                      entry.file.u8string().c_str());
            return result;
        }
    }

    // Read rows until the next airport header, row 99, or EOF
    int lastStandIdx = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string code = firstToken(line);
        if (code == "1" || code == "16" || code == "17" || code == "99") break;

        if (code == "1302") {
            std::string real;
            if (parseIcaoCodeRow(line, real) && real != result.icao) {
                result.aptId = result.icao;
                result.icao  = std::move(real);
            }
        } else if (code == "1300") {
            // tokenise(line, 7): collect 6 fixed fields, remainder → tok[6] = full name.
            // Using 8 here truncated multi-word stand IDs to the first word (Phase 1 bug).
            auto tok = tokenise(line, 7);
            Stand s = parse1300(tok);
            if (!s.id.empty()) {
                result.stands.push_back(std::move(s));
                lastStandIdx = static_cast<int>(result.stands.size()) - 1;
            } else {
                lastStandIdx = -1;
            }
        } else if (code == "1301" && lastStandIdx >= 0) {
            auto tok = tokenise(line);   // every airline code is its own token
            apply1301(tok, result.stands[lastStandIdx]);
            lastStandIdx = -1; // 1301 consumed
        } else if (code == "100") {
            // Land runway (apt.dat 1100 spec): [2] surface, [9]/[10] end-1
            // lat/lon, [18]/[19] end-2 lat/lon. Length between the ends;
            // unpaved surfaces (3 turf, 4 dirt, 5 gravel, 12 lakebed, 13
            // water, 14 snow/ice, 15 transparent) don't count.
            auto tok = tokenise(line);
            if (tok.size() >= 20) {
                try {
                    const int surface = std::stoi(tok[2]);
                    const bool unpaved = surface == 3 || surface == 4 || surface == 5 ||
                                         (surface >= 12 && surface <= 15);
                    const double la1 = std::stod(tok[9]),  lo1 = std::stod(tok[10]);
                    const double la2 = std::stod(tok[18]), lo2 = std::stod(tok[19]);
                    const double n = (la2 - la1) * 110574.0;
                    const double e = (lo2 - lo1) * 111320.0 * std::cos(la1 * 3.14159265358979323846 / 180.0);
                    if (!unpaved) result.longestRunwayM = std::max(result.longestRunwayM, std::hypot(n, e));
                } catch (...) {}
            }
            lastStandIdx = -1;
        } else {
            lastStandIdx = -1; // anything else breaks 1300/1301 pairing
        }
    }

    // Apply inferred defaults to every stand
    for (auto& s : result.stands) inferDefaults(s);
    if (const std::size_t n = demoteNonPassenger(result.stands))
        Log::info("AptDat: %s — %zu airline-tagged stand(s) are not passenger stands (name or location): left empty",
                  result.icao.c_str(), n);

    result.fingerprint = Fingerprint::compute(result.stands);
    Log::debug("AptDat: parsed %s — %zu stands, fingerprint %s",
               result.icao.c_str(), result.stands.size(), result.fingerprint.c_str());
    return result;
}

std::size_t demoteNonPassenger(std::vector<Stand>& stands) {
    static const std::regex nonPax(
        R"((^|[^A-Z])(STORAGE|MAINT|MAINTENANCE|MRO|HANGAR|MIL|MILITARY|AIR ?FORCE|PRESIDENTIAL|VIP|GOVT|GOVERNMENT|)"
        R"(DE-?ICE|DE-?ICING|RUN-?UP|WASH|GA|FBO|BUSINESS)([^A-Z]|$))",
        std::regex::icase);
    static const std::regex cargo(R"((^|[^A-Z])(CARGO|FREIGHT)([^A-Z]|$))", std::regex::icase);
    std::size_t changed = 0;
    for (auto& s : stands) {
        if (s.op != OpType::Airline) continue;
        if (std::regex_search(s.id, cargo))       { s.op = OpType::Cargo; ++changed; }
        else if (std::regex_search(s.id, nonPax)) { s.op = OpType::None;  ++changed; }
    }

    auto distM = [](const Stand& a, const Stand& b) {
        const double n = (a.lat - b.lat) * 110574.0;
        const double e = (a.lon - b.lon) * 111320.0 * std::cos(a.lat * 3.14159265358979323846 / 180.0);
        return std::hypot(n, e);
    };
    std::vector<const Stand*> gates, other;
    for (const auto& s : stands) {
        if (s.op == OpType::Airline && s.type == StandType::Gate) gates.push_back(&s);
        if (s.op == OpType::Military || s.op == OpType::GA || s.op == OpType::None) other.push_back(&s);
    }
    if (gates.empty() || other.empty()) return changed;
    for (auto& s : stands) {
        if (s.op != OpType::Airline || s.type == StandType::Gate) continue;
        double toGate = 1e18, toOther = 1e18;
        for (const Stand* g : gates) toGate = std::min(toGate, distM(s, *g));
        for (const Stand* o : other) toOther = std::min(toOther, distM(s, *o));
        if (toGate > kRemoteFromGateM && toOther < toGate) { s.op = OpType::None; ++changed; }
    }
    return changed;
}

} // namespace AptDat
