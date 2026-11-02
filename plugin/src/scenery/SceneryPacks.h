#pragma once
#include <filesystem>
#include <vector>

// Reads Custom Scenery/scenery_packs.ini and returns the ordered list of
// enabled scenery pack root paths (highest priority first).
//
// The global-airports fallback is handled by AptDat, not here.
namespace SceneryPacks {

// Format verified against X-Plane developer documentation:
// developer.x-plane.com/article/x-plane-scenery-library-layering/
//
// Lines:
//   SCENERY_PACK <relative_path>          — enabled pack
//   SCENERY_PACK_DISABLED <relative_path> — disabled pack (skip)
// Paths are relative to xplaneRoot and may use '/' or '\'.
// X-Plane 12 writes "SCENERY_PACK *GLOBAL_AIRPORTS*" where the Global
// Airports (Global Scenery/Global Airports) sit in the order: packs below it
// lose to them. It is returned as a path whose filename is kGlobalAirports.
constexpr const char* kGlobalAirports = "*GLOBAL_AIRPORTS*";
std::vector<std::filesystem::path> loadEnabledPacks(
    const std::filesystem::path& xplaneRoot);

} // namespace SceneryPacks
