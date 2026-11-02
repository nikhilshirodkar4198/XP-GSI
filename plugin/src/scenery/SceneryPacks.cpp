#include "SceneryPacks.h"
#include <algorithm>
#include <fstream>
#include <string>  // std::getline — not pulled in transitively by MSVC's <fstream>

namespace SceneryPacks {

std::vector<std::filesystem::path> loadEnabledPacks(const std::filesystem::path& xplaneRoot) {
    // Path: <xplane_root>/Custom Scenery/scenery_packs.ini
    // Verified against X-Plane developer docs (layering article).
    auto iniPath = xplaneRoot / "Custom Scenery" / "scenery_packs.ini";

    std::vector<std::filesystem::path> packs;
    std::ifstream in(iniPath);
    if (!in.is_open()) return packs;

    constexpr std::string_view kEnabled  = "SCENERY_PACK ";
    constexpr std::string_view kDisabled = "SCENERY_PACK_DISABLED ";

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip disabled packs (prefix check must come first — it is longer)
        if (line.rfind(kDisabled, 0) == 0) continue;

        if (line.rfind(kEnabled, 0) == 0) {
            std::string rel = line.substr(kEnabled.size());
            // Trim trailing whitespace and path separators
            while (!rel.empty() && (std::isspace(static_cast<unsigned char>(rel.back()))
                                    || rel.back() == '/' || rel.back() == '\\'))
                rel.pop_back();
            // Normalise backslashes so std::filesystem::path works correctly
            std::replace(rel.begin(), rel.end(), '\\', '/');
            packs.push_back(xplaneRoot / rel);
        }
    }
    return packs;
}

} // namespace SceneryPacks
