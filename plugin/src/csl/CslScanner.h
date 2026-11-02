#pragma once
// CslScanner — walks the X-Plane install looking for XPMP2-compatible CSL
// packages (xsb_aircraft.txt) and builds an in-memory CslDb catalogue.
//
// Format verified against the XPMP2 source at github.com/TwinFan/XPMP2
// (src/CSLModels.cpp, functions CSLModelsFindPkgs / CSLModelsReadPkgId /
// CSLModelsConvPackagePath / AcTxtLine_OBJ8 / AcTxtLine_VERT_OFFSET /
// AcTxtLine_MATCHES, master branch, read 2026-10-25).

#include "csl/CslDb.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace CslScanner {

// EXPORT_NAME → folder containing the xsb_aircraft.txt that declared it.
// Global across all scanned packages, exact (case-sensitive) names, first
// declaration wins — as XPMP2's glob.mapCSLPkgs.
using PackageMap = std::unordered_map<std::string, std::filesystem::path>;

// Folders searched (recursively) for xsb_aircraft.txt:
//   <xplaneRoot>/Custom Data         — the whole folder, not only Custom Data/CSL:
//                                      xPilot uses Custom Data/CSL (verified on a
//                                      live XP11 install: xPilot logs
//                                      "XPMPLoadCSLPackage: Loading CSL package
//                                      from <root>\Custom Data\CSL"), but a library
//                                      copied by hand may sit one level up (live
//                                      XP12 install, 2026-10-28: Custom Data/X-CSL,
//                                      0 models found). Custom Data/CIFP is skipped.
//   <xplaneRoot>/Custom Scenery
//   <xplaneRoot>/Resources/plugins
std::vector<std::filesystem::path> searchRoots(const std::filesystem::path& xplaneRoot);

// Scan every search root: first collect all EXPORT_NAMEs, then parse every
// file against that global package map. Safe to call from a worker thread.
CslDb scan(const std::filesystem::path& xplaneRoot);

// Parse a single xsb_aircraft.txt into an existing db, resolving OBJ8 paths
// against `pkgs`. Exposed for unit testing.
void parseFile(const std::filesystem::path& xsbPath, CslDb& db, const PackageMap& pkgs);

// Same, with a package map built from this file's own EXPORT_NAME only.
void parseFile(const std::filesystem::path& xsbPath, CslDb& db);

// Turn an OBJ8 path token ("Pkg:sub/file.obj", "Pkg\file.obj", "Pkg/file.obj")
// into an absolute path, per XPMP2 CSLModelsConvPackagePath. Returns empty if
// the token has no package prefix, the package is unknown, or the file is missing.
std::filesystem::path resolvePackagePath(const std::string& token, const PackageMap& pkgs);

// Vertical offset derived from an OBJ file's VT / VLINE Y coordinates, per
// XPMP2 CSLObj::FetchVertOfsFromObjFile. Returns 0 if the file can't be read.
float vertOfsFromObj(const std::filesystem::path& objPath);

// The texture an OBJ8 file names (its TEXTURE line, relative to the OBJ);
// empty if none.
std::filesystem::path objTexture(const std::filesystem::path& obj);

// What the audit knows about a library's liveries (data/csl_livery_overrides.json,
// built by pipeline/livery_audit/build_overrides.py):
//   { "schema": 2,
//     "defunct_airlines": ["AWE", ...],   // any library: never placed
//     "old_livery_tags":  ["OLD"],        // any library: LIVERY tag → outdated
//     "entries": { "X-CSL/CRJ2/CRJ2_SKW2.obj":
//                    { "status": "current" | "legacy" | "outdated" | "defunct",
//                      "year": 2007, "brand": "DAL", "special": false,
//                      "tex_bytes": 1048576 }, ... } }
// An entry applies to a variant one of whose OBJ paths ends with its path
// (case-insensitive, '/' separators) and — when tex_bytes is given — whose
// texture has exactly that size; otherwise (a newer library version) it is
// skipped and counted as stale. A missing or malformed file changes nothing.
struct OverrideStats {
    std::size_t changed = 0;   // variants whose flags changed
    std::size_t matched = 0;   // entries applied
    std::size_t stale   = 0;   // entries skipped: the texture differs
};
OverrideStats applyLiveryOverrides(CslDb& db, const std::filesystem::path& file);

} // namespace CslScanner
