// CslScanner: parse XPMP2-compatible xsb_aircraft.txt files.
//
// Format source: github.com/TwinFan/XPMP2 src/CSLModels.cpp (master, read 2026-10-25)
//
// xsb_aircraft.txt directives handled:
//   EXPORT_NAME <pkg>                — declares a package; its folder is the one
//                                      containing this xsb_aircraft.txt
//                                      (CSLModelsFindPkgs / CSLModelsReadPkgId)
//   OBJ8_AIRCRAFT <shortId>          — starts a new model block
//   OBJ8 <type> <YES/NO> <path> ...  — one object of the model; tokens[3] is the
//                                      path, type and YES/NO are ignored. Every
//                                      OBJ8 line is kept (AcTxtLine_OBJ8)
//   VERT_OFFSET <m>                  — tokens[1] (AcTxtLine_VERT_OFFSET)
//   OFFSET <x> <y> <z>               — PilotEdge form; tokens[3] (AcTxtLine_OFFSET)
//   ICAO     <icaoType>              — ICAO type match; tokens[1]
//   AIRLINE  <icaoType> <airline>    — airline match; tokens[1] is the ICAO type
//   LIVERY   <icaoType> <a> <l>      — livery match; tokens[1] is the ICAO type
//   MATCHES  <icaoType> <a> <l>      — generic match; tokens[1] is the ICAO type
//
// OBJ path format (CSLModelsConvPackagePath):
//   The package name is everything before the first ':', '/' or '\'. It is
//   looked up among all EXPORT_NAMEs (exact match; a leading "__" is part of
//   the name). In the remainder every ':', '\' and '/' is a path separator.
//   Unknown package, no prefix, or a missing file → the OBJ8 line is dropped.

#include "csl/CslScanner.h"
#include "core/Log.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;

static std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) tokens.push_back(std::move(tok));
    return tokens;
}

static void toUpperInPlace(std::string& s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

// std::stof without exceptions; returns false if the token is not a number.
static bool parseFloat(const std::string& tok, float& out) {
    char* end = nullptr;
    float v = std::strtof(tok.c_str(), &end);
    if (end == tok.c_str()) return false;
    out = v;
    return true;
}

// State for one OBJ8_AIRCRAFT block being accumulated.
struct PendingModel {
    bool                  started = false; // an OBJ8_AIRCRAFT line opened this block
    std::string           icaoType;        // uppercase; empty = not yet set
    std::string           airlineCode;     // uppercase; empty = generic (ICAO/MATCHES) or not yet set
    std::string           livery;          // LIVERY tokens[3], upper-cased; empty if none
    std::vector<fs::path> objPaths;        // every resolvable OBJ8 line, in order
    bool                  hasVertOfs = false;
    float                 vertOfsM   = 0.0f;
};

static void finalize(PendingModel& m, CslDb& db, size_t& count) {
    if (m.started && !m.icaoType.empty() && !m.objPaths.empty()) {
        CslVariant v;
        v.objPaths    = std::move(m.objPaths);
        v.airlineCode = m.airlineCode;
        v.livery      = m.livery;
        v.special     = isSpecialLiveryCode(m.livery);
        if (m.hasVertOfs) {
            v.vertOfsM = m.vertOfsM;
        } else {
            // No VERT_OFFSET: XPMP2 reads it from the OBJ files and keeps the
            // largest value over all objects (CSLModel::FetchVertOfsFromObjFile).
            float best = 0.0f;
            bool  any  = false;
            for (const auto& p : v.objPaths) {
                float o = CslScanner::vertOfsFromObj(p);
                if (!any || o > best) { best = o; any = true; }
            }
            v.vertOfsM = best;
        }
        db[m.icaoType].push_back(std::move(v));
        ++count;
    }
    m = {};
}

// Collect all xsb_aircraft.txt paths under `root` recursively. Top-level
// folders named in `skip` are not entered (Custom Data/CIFP: ~15,000 navdata
// files, never a CSL package).
static size_t collectXsbFiles(const fs::path& root, const std::vector<std::string>& skip,
                              std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return 0;
    const size_t before = out.size();
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        // Its own error code: one unreadable entry must not end the whole walk.
        std::error_code eec;
        if (it.depth() == 0 && entry.is_directory(eec) &&
            std::find(skip.begin(), skip.end(), entry.path().filename().u8string()) != skip.end()) {
            it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(eec)) continue;
        if (entry.path().filename() == "xsb_aircraft.txt")
            out.push_back(entry.path());
    }
    return out.size() - before;
}

// Read EXPORT_NAME lines from one file into `pkgs` (first declaration wins).
// XPMP2 CSLModelsReadPkgId: a line of exactly two tokens, "EXPORT_NAME <name>".
static void readPackageIds(const fs::path& xsbPath, CslScanner::PackageMap& pkgs) {
    std::ifstream f(xsbPath);
    if (!f.is_open()) return;
    const fs::path pkgDir = xsbPath.parent_path();
    std::string line;
    while (std::getline(f, line)) {
        auto tokens = tokenize(line);
        if (tokens.size() == 2 && tokens[0] == "EXPORT_NAME") {
            auto [it, inserted] = pkgs.emplace(tokens[1], pkgDir);
            if (!inserted && it->second != pkgDir)
                Log::warn("CslScanner: duplicate package '%s' in '%s' — keeping '%s'",
                          tokens[1].c_str(), pkgDir.u8string().c_str(),
                          it->second.u8string().c_str());
        }
    }
}

} // namespace

namespace CslScanner {

std::vector<std::filesystem::path> searchRoots(const std::filesystem::path& xplaneRoot) {
    return {
        xplaneRoot / "Custom Data",
        xplaneRoot / "Custom Scenery",
        xplaneRoot / "Resources" / "plugins",
    };
}

std::filesystem::path resolvePackagePath(const std::string& token, const PackageMap& pkgs) {
    // XPMP2 CSLModelsConvPackagePath: package = text before the first ':', '/' or '\'.
    auto pos = token.find_first_of(":/\\");
    if (pos == std::string::npos || pos == 0 || pos == token.size() - 1)
        return {};

    auto it = pkgs.find(token.substr(0, pos));
    if (it == pkgs.end())
        return {};

    // All remaining ':', '\' and '/' are directory separators.
    std::string rel = token.substr(pos + 1);
    for (char& c : rel)
        if (c == ':' || c == '\\') c = '/';

    fs::path full = it->second / fs::u8path(rel);
    std::error_code ec;
    if (!fs::is_regular_file(full, ec))   // XPMP2 also verifies the file exists
        return {};
    return full;
}

float vertOfsFromObj(const std::filesystem::path& objPath) {
    // XPMP2 CSLObj::FetchVertOfsFromObjFile: scan VT and VLINE lines (at least
    // 20 chars, 'V' then 'T' or 'L'), take the Y coordinate (tokens[2]);
    // result = min < 0 ? -min : -max.
    std::ifstream f(objPath);
    if (!f.is_open()) return 0.0f;

    float minY = 0.0f, maxY = 0.0f;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() < 20 || line[0] != 'V' || (line[1] != 'T' && line[1] != 'L'))
            continue;
        auto tokens = tokenize(line);
        float y = 0.0f;
        if (tokens.size() < 3 || !parseFloat(tokens[2], y))
            continue;
        if (y < minY)      minY = y;
        else if (y > maxY) maxY = y;
    }
    return minY < 0.0f ? -minY : -maxY;
}

void parseFile(const std::filesystem::path& xsbPath, CslDb& db, const PackageMap& pkgs) {
    std::ifstream f(xsbPath);
    if (!f.is_open()) {
        Log::warn("CslScanner: cannot open '%s'", xsbPath.u8string().c_str());
        return;
    }

    PendingModel current{};
    size_t       added   = 0;
    size_t       dropped = 0;   // OBJ8 lines whose path could not be resolved

    std::string line;
    while (std::getline(f, line)) {
        auto tokens = tokenize(line);
        if (tokens.empty() || tokens[0].empty() || tokens[0][0] == '#')
            continue;

        const std::string& cmd = tokens[0];

        if (cmd == "OBJ8_AIRCRAFT") {
            // Finalize the model accumulated so far, then start a fresh one.
            finalize(current, db, added);
            current.started = true;

        } else if (cmd == "OBJ8") {
            // OBJ8 <type> <YES/NO> <path> [tex [lit_tex]] — tokens[3] is the path.
            if (tokens.size() >= 4 && current.started) {
                auto p = resolvePackagePath(tokens[3], pkgs);
                if (p.empty()) ++dropped;
                else           current.objPaths.push_back(std::move(p));
            }

        } else if (cmd == "VERT_OFFSET") {
            float v = 0.0f;
            if (tokens.size() >= 2 && parseFloat(tokens[1], v)) {
                current.vertOfsM   = v;
                current.hasVertOfs = true;
            }

        } else if (cmd == "OFFSET") {
            float v = 0.0f;
            if (tokens.size() >= 4 && parseFloat(tokens[3], v)) {
                current.vertOfsM   = v;
                current.hasVertOfs = true;
            }

        } else if (cmd == "ICAO" || cmd == "MATCHES") {
            // ICAO/MATCHES: tokens[1] = ICAO type; no airline code.
            // Verified: XPMP2 AcTxtLine_MATCHES, size >= 2.
            if (tokens.size() >= 2 && current.icaoType.empty()) {
                current.icaoType = tokens[1];
                toUpperInPlace(current.icaoType);
            }
        } else if (cmd == "AIRLINE" || cmd == "LIVERY") {
            // AIRLINE/LIVERY: tokens[1] = ICAO type, tokens[2] = airline code.
            // Verified: XPMP2 AcTxtLine_MATCHES, same token layout for AIRLINE.
            if (tokens.size() >= 2 && current.icaoType.empty()) {
                current.icaoType = tokens[1];
                toUpperInPlace(current.icaoType);
            }
            if (tokens.size() >= 3 && current.airlineCode.empty()) {
                current.airlineCode = tokens[2];
                toUpperInPlace(current.airlineCode);
                if (current.airlineCode == "-") current.airlineCode.clear(); // wildcard
            }
            // LIVERY <type> <airline> <livery>: the livery code (XPMP2 AcTxtLine_MATCHES
            // reads the same tokens). Numbered codes mark special schemes.
            if (cmd == "LIVERY" && tokens.size() >= 4 && current.livery.empty()) {
                current.livery = tokens[3];
                toUpperInPlace(current.livery);
                if (current.livery == "-") current.livery.clear();
            }
        }
        // EXPORT_NAME (read in the package pass), DEPENDENCY, OBJECT, AIRCRAFT — not needed
    }

    // Finalize the last model in the file (no trailing OBJ8_AIRCRAFT to trigger it).
    finalize(current, db, added);

    if (dropped > 0)
        Log::warn("CslScanner: '%s' — %zu OBJ8 line(s) skipped (unknown package or missing file)",
                  xsbPath.u8string().c_str(), dropped);
    Log::debug("CslScanner: '%s' — %zu model(s) added",
               xsbPath.u8string().c_str(), added);
}

void parseFile(const std::filesystem::path& xsbPath, CslDb& db) {
    PackageMap pkgs;
    readPackageIds(xsbPath, pkgs);
    parseFile(xsbPath, db, pkgs);
}

CslDb scan(const std::filesystem::path& xplaneRoot) {
    CslDb db;

    std::vector<fs::path> xsbFiles;
    const std::vector<std::string> skip{"CIFP"};
    for (const auto& root : searchRoots(xplaneRoot)) {
        const size_t n = collectXsbFiles(root, skip, xsbFiles);
        Log::info("CslScanner: %zu xsb_aircraft.txt file(s) under '%s'", n, root.u8string().c_str());
    }

    if (xsbFiles.empty()) {
        Log::warn("CslScanner: no xsb_aircraft.txt found under '%s' (searched Custom Data, "
                  "Custom Scenery and Resources/plugins) — statics will be placeholders",
                  xplaneRoot.u8string().c_str());
        return db;
    }

    // Directory iteration order is unspecified; sort so "first EXPORT_NAME
    // wins" is deterministic between runs.
    std::sort(xsbFiles.begin(), xsbFiles.end());

    Log::info("CslScanner: found %zu xsb_aircraft.txt file(s) — parsing …",
              xsbFiles.size());

    // Pass 1: all packages, so cross-package references resolve (XPMP2 does the
    // same: CSLModelsFindPkgs registers every package before any file is processed).
    PackageMap pkgs;
    for (const auto& xsb : xsbFiles)
        readPackageIds(xsb, pkgs);

    // Pass 2: models.
    for (const auto& xsb : xsbFiles)
        parseFile(xsb, db, pkgs);

    size_t totalVariants = 0, totalObjs = 0;
    for (const auto& [icao, entry] : db) {
        totalVariants += entry.size();
        for (const auto& v : entry) totalObjs += v.objPaths.size();
    }

    Log::info("CslScanner: catalogue ready — %zu packages, %zu ICAO types, %zu models, %zu OBJs",
              pkgs.size(), db.size(), totalVariants, totalObjs);

    return db;
}

std::filesystem::path objTexture(const std::filesystem::path& obj) {
    std::ifstream f(obj);
    std::string line;
    for (int n = 0; n < 40 && std::getline(f, line); ++n) {   // the header: TEXTURE comes early
        if (line.rfind("TEXTURE ", 0) != 0) continue;
        std::string t = line.substr(8);
        while (!t.empty() && (t.back() == '\r' || t.back() == ' ' || t.back() == '\t')) t.pop_back();
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
        if (t.empty()) return {};
        auto p = obj.parent_path() / t;
        // X-Plane loads the .png of the same name when the .dds is missing (and
        // the other way round): libraries ship as a DDS or a PNG pack.
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            auto alt = p;
            alt.replace_extension(p.extension() == ".dds" || p.extension() == ".DDS" ? ".png" : ".dds");
            if (std::filesystem::exists(alt, ec)) return alt;
        }
        return p;
    }
    return {};
}

OverrideStats applyLiveryOverrides(CslDb& db, const std::filesystem::path& file) {
    OverrideStats st;
    std::ifstream f(file);
    if (!f.is_open()) return st;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 2) return st;

    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    };
    struct Rule {
        std::string            suffix;      // "/x-csl/crj2/crj2_jia.obj"
        const nlohmann::json*  e;
    };
    std::vector<Rule> rules;
    if (auto it = j.find("entries"); it != j.end() && it->is_object())
        for (const auto& [p, e] : it->items())
            if (e.is_object()) rules.push_back({"/" + lower(p), &e});
    std::vector<std::string> defunctAirlines, oldTags, cargoAirlines, cargoTags, cargoNameParts;
    auto strings = [&](const char* key, std::vector<std::string>& out) {
        if (auto it = j.find(key); it != j.end() && it->is_array())
            for (const auto& c : *it)
                if (c.is_string()) out.push_back(c.get<std::string>());
    };
    std::vector<std::string> statePrefixes;
    strings("state_code_prefixes", statePrefixes);
    strings("cargo_airlines", cargoAirlines);
    strings("cargo_livery_tags", cargoTags);
    strings("cargo_name_parts", cargoNameParts);
    for (auto& p : cargoNameParts) p = lower(p);
    if (auto it = j.find("defunct_airlines"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            if (c.is_string()) defunctAirlines.push_back(c.get<std::string>());
    if (auto it = j.find("old_livery_tags"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            if (c.is_string()) oldTags.push_back(c.get<std::string>());

    auto endsWith = [](const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    for (auto& [type, entry] : db)
        for (auto& v : entry) {
            const CslVariant before = v;
            // Rules for any library: a defunct airline's code, a livery the
            // library itself tags as old.
            if (std::find(defunctAirlines.begin(), defunctAirlines.end(), v.airlineCode) != defunctAirlines.end())
                v.retired = true;
            if (std::find(oldTags.begin(), oldTags.end(), v.livery) != oldTags.end())
                v.era = CslVariant::Era::Outdated;
            // Government / military aircraft filed under a state code ("@ID").
            for (const auto& p : statePrefixes)
                if (!p.empty() && v.airlineCode.rfind(p, 0) == 0) v.retired = true;
            // Cargo: a cargo airline's code, a cargo livery tag ("CRG",
            // "CARGO", "BCF" …, also as part of "WLCRG"), or an OBJ file name
            // that says so.
            if (std::find(cargoAirlines.begin(), cargoAirlines.end(), v.airlineCode) != cargoAirlines.end())
                v.cargo = true;
            for (const auto& t : cargoTags)
                if (!t.empty() && (v.livery == t || (t.size() >= 3 && v.livery.find(t) != std::string::npos)))
                    v.cargo = true;
            for (const auto& obj : v.objPaths) {
                const std::string objName = lower(obj.filename().u8string());
                for (const auto& p : cargoNameParts)
                    if (objName.find(p) != std::string::npos) v.cargo = true;
            }
            // Audited liveries, matched by path inside the package and, when
            // recorded, by texture size (a repainted file in a newer library
            // version is left alone).
            for (const auto& obj : v.objPaths) {
                const std::string path = lower(obj.generic_u8string());
                for (const auto& r : rules) {
                    if (!endsWith(path, r.suffix)) continue;
                    const auto& e = *r.e;
                    if (auto b = e.find("tex_bytes"); b != e.end() && b->is_number_integer()) {
                        // Compared only for the same texture format the audit
                        // measured (a DDS-pack user's file can't match a PNG size).
                        std::error_code ec;
                        const auto tex = objTexture(obj);
                        const std::string ext = lower(tex.extension().u8string());
                        if (ext == e.value("tex_ext", ext)) {
                            const auto size = tex.empty() ? 0 : std::filesystem::file_size(tex, ec);
                            if (ec || size != b->get<std::uintmax_t>()) { ++st.stale; continue; }
                        }
                    }
                    const std::string status = e.value("status", std::string("current"));
                    v.retired = status == "defunct";
                    v.era = status == "legacy"   ? CslVariant::Era::Legacy
                          : status == "outdated" ? CslVariant::Era::Outdated
                                                 : CslVariant::Era::Current;
                    v.schemeYear = e.value("year", 0);
                    if (auto b = e.find("brand"); b != e.end() && b->is_string()) v.brand = b->get<std::string>();
                    if (auto s = e.find("special"); s != e.end() && s->is_boolean()) v.special = s->get<bool>();
                    ++st.matched;
                }
            }
            if (v.retired != before.retired || v.era != before.era || v.brand != before.brand ||
                v.cargo != before.cargo ||
                v.special != before.special || v.schemeYear != before.schemeYear)
                ++st.changed;
        }
    return st;
}

} // namespace CslScanner
