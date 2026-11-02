#pragma once
// RelatedTypes — the look-alike type to draw when the CSL library lacks the
// type the data names (data/related_types.json): adsb.lol reports SkyWest's
// E175s as E75L and its CRJ900s as CRJ9, X-CSL has E175 and CRJ2 only.
//
// Order: the type itself if the library has it; else its alias (the same
// airframe under another designator); else the nearest member of its family
// group that the library has, the smaller one on a tie; else "".
//
// Pure logic, no XPLM calls.

#include "csl/CslDb.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

class RelatedTypes {
public:
    static std::optional<RelatedTypes> load(const std::filesystem::path& file);

    void addAlias(const std::string& type, const std::string& same) { aliases_[type] = same; }
    void addGroup(std::vector<std::string> group);

    std::string substitute(const std::string& type, const CslDb& db) const;

private:
    std::map<std::string, std::string>                       aliases_;
    std::vector<std::vector<std::string>>                    groups_;
    std::map<std::string, std::pair<std::size_t, std::size_t>> where_;   // type → (group, index)
};
