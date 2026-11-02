#include "csl/RelatedTypes.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace {

bool usable(const CslDb& db, const std::string& type) {
    auto it = db.find(type);
    if (it == db.end()) return false;
    for (const auto& v : it->second)
        if (!v.retired) return true;
    return false;
}

} // namespace

std::optional<RelatedTypes> RelatedTypes::load(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f.is_open()) return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return std::nullopt;
    RelatedTypes r;
    if (auto it = j.find("aliases"); it != j.end() && it->is_object())
        for (const auto& [type, same] : it->items())
            if (same.is_string()) r.addAlias(type, same.get<std::string>());
    if (auto it = j.find("groups"); it != j.end() && it->is_array())
        for (const auto& g : *it) {
            if (!g.is_array()) continue;
            std::vector<std::string> group;
            for (const auto& t : g)
                if (t.is_string()) group.push_back(t.get<std::string>());
            r.addGroup(std::move(group));
        }
    return r;
}

void RelatedTypes::addGroup(std::vector<std::string> group) {
    for (std::size_t i = 0; i < group.size(); ++i) where_[group[i]] = {groups_.size(), i};
    groups_.push_back(std::move(group));
}

std::string RelatedTypes::substitute(const std::string& type, const CslDb& db) const {
    if (usable(db, type)) return type;
    if (auto a = aliases_.find(type); a != aliases_.end() && usable(db, a->second)) return a->second;
    auto w = where_.find(type);
    if (w == where_.end()) return {};
    const auto& group = groups_[w->second.first];
    const std::size_t at = w->second.second;
    for (std::size_t d = 1; d < group.size(); ++d) {
        if (at >= d && usable(db, group[at - d])) return group[at - d];   // smaller first
        if (at + d < group.size() && usable(db, group[at + d])) return group[at + d];
    }
    return {};
}
