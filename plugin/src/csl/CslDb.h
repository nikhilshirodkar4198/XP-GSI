#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// One CSL model (one OBJ8_AIRCRAFT block) for a given ICAO type.
struct CslVariant {
    // Every OBJ8 line of the block, in file order; all are drawn together at
    // the same position (XPMP2 keeps each OBJ8 as a separate CSLObj in
    // CSLModel::listObj). Absolute paths; never empty for a catalogued model.
    std::vector<std::filesystem::path> objPaths;
    // Metres to raise the model so it stands on its gear: VERT_OFFSET / OFFSET
    // from xsb_aircraft.txt, or derived from the OBJ vertices when absent
    // (XPMP2 Aircraft::GetVertOfs, added to drawInfo.y).
    float                 vertOfsM    = 0.0f;
    char                  sizeCode    = '?';  // 'A'-'F'; populated by IcaoSize::lookup
    std::string           airlineCode;         // ICAO airline code (e.g. "DAL"); empty = generic
    // LIVERY line's livery code (tokens[3]); empty for a plain AIRLINE/ICAO line.
    std::string           livery;
    // An alternate / special scheme: X-CSL numbers them ("LIVERY A388 UAE 2",
    // "WL2", "S3"), while un-numbered codes ("WL", "S", "CRG") are the
    // airline's standard livery on another airframe variant. Specials are
    // placed at most once per airport — real fleets have one such aircraft.
    bool                  special = false;
    // How current the paint is (data/csl_livery_overrides.json, checked
    // against the textures; realistic traffic, newer
    // liveries first, an out-of-use scheme only when nothing newer exists):
    //   Current  — today's scheme (and anything not audited)
    //   Legacy   — an older scheme some aircraft still fly
    //   Outdated — no aircraft flies it any more; used only when the airline
    //              has nothing newer on any type that fits, newest first
    enum class Era : std::uint8_t { Current, Legacy, Outdated };
    Era                   era        = Era::Current;
    int                   schemeYear = 0;   // when the scheme was introduced; 0 = unknown
    // The wrong airline or brand today (US Airways Express, AnadoluJet, a
    // defunct airline's code): never placed.
    bool                  retired = false;
    // A freighter or cargo airline's paint (Alaska Air Cargo, FedEx, a
    // "CRG" livery): never placed at the passenger stands the plugin fills.
    bool                  cargo = false;
    // The airline brand the paint shows when it differs from the operator
    // code the CSL files it under: X-CSL "LIVERY CRJ2 SKW 2" is a SkyWest
    // CRJ in Delta Connection paint → brand "DAL". Empty = unknown / own.
    std::string           brand;
};

// True for a numbered livery code (see CslVariant::special).
inline bool isSpecialLiveryCode(const std::string& code) {
    return !code.empty() && code.back() >= '0' && code.back() <= '9';
}

// All variants for a single ICAO type code (e.g. "B738" → several livery OBJs).
using CslEntry = std::vector<CslVariant>;

// Full catalogue: ICAO type (uppercase) → variants.
// Built by CslScanner::scan() on a worker thread; read-only on the main thread after.
using CslDb = std::unordered_map<std::string, CslEntry>;
