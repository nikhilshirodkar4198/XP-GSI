#include "data/AirportTypes.h"

#include <fstream>

#include <nlohmann/json.hpp>

bool AirportTypes::load(const std::filesystem::path& file) {
    m_types.clear();
    std::ifstream f(file);
    if (!f.is_open()) return false;

    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return false;

    auto it = j.find("airports");
    if (it == j.end() || !it->is_object()) return false;
    for (const auto& [icao, entry] : it->items())
        if (entry.is_object())
            m_types.emplace(icao, entry.value("type", std::string{}));
    return true;
}

bool AirportTypes::loadHubs(const std::filesystem::path& file) {
    m_hubs.clear();
    std::ifstream f(file);
    if (!f.is_open()) return false;

    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return false;

    auto it = j.find("airports");
    if (it == j.end() || !it->is_object()) return false;
    for (const auto& [icao, note] : it->items()) m_hubs.insert(icao);
    return true;
}

std::string AirportTypes::typeOf(const std::string& icao) const {
    auto it = m_types.find(icao);
    return it != m_types.end() ? it->second : std::string{};
}
