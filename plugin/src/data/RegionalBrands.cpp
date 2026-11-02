#include "data/RegionalBrands.h"

#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

std::optional<RegionalBrands> RegionalBrands::load(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f.is_open()) return std::nullopt;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return std::nullopt;
    RegionalBrands r;
    if (auto it = j.find("operators"); it != j.end() && it->is_object())
        for (const auto& [op, list] : it->items()) {
            if (!list.is_array()) continue;
            std::vector<std::string> partners;
            for (const auto& p : list)
                if (p.is_string()) partners.push_back(p.get<std::string>());
            if (!partners.empty()) r.addOperator(op, std::move(partners));
        }
    if (auto it = j.find("brand_articles"); it != j.end() && it->is_object())
        for (const auto& [key, code] : it->items())
            if (code.is_string()) r.addArticle(key, code.get<std::string>());
    if (auto it = j.find("airport_partners"); it != j.end() && it->is_object())
        for (const auto& [icao, ops] : it->items()) {
            if (!ops.is_object()) continue;
            for (const auto& [op, weights] : ops.items()) {
                if (!weights.is_object()) continue;
                for (const auto& [p, w] : weights.items())
                    if (w.is_number() && w.get<float>() > 0.0f) r.setAirportPartner(icao, op, p, w.get<float>());
            }
        }
    return r;
}

std::map<std::string, float> RegionalBrands::presence(const WikiEntry* wiki, const AirportTable& table) const {
    std::map<std::string, float> out;
    if (wiki)
        for (const auto& [key, w] : wiki->airlines)
            if (auto a = articles_.find(key); a != articles_.end()) out[a->second] += w;
    std::set<std::string> partners;
    for (const auto& [op, list] : operators_) partners.insert(list.begin(), list.end());
    for (const auto& p : partners)
        if (auto t = table.airlines.find(p); t != table.airlines.end()) out[p] += 0.25f * t->second;
    return out;
}

std::map<std::string, float> RegionalBrands::partnersFor(const std::string& op,
                                                         const std::map<std::string, float>& presence) const {
    std::map<std::string, float> out;
    auto it = operators_.find(op);
    if (it == operators_.end()) return out;
    float total = 0;
    for (const auto& p : it->second) {
        auto pr = presence.find(p);
        const float w = pr != presence.end() ? pr->second : 0.0f;
        out[p] = w;
        total += w;
    }
    if (total <= 0.0f)
        for (auto& [p, w] : out) w = 1.0f;
    return out;
}
