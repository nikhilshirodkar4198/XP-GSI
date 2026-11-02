#pragma once
// AirportTypes — OurAirports size type per ICAO, from the shipped
// data/airports_eligible.json (pipeline/build_eligibility.py, public domain).
//
//   { "schema": 1, "source": "...",
//     "airports": { "KLGA": { "type": "large_airport", "scheduled": true }, ... } }
//
// Only medium/large airports and seaplane bases are listed; anything else is
// a small field (Density::categoryFor("") → Small).
//
// Pure logic (file IO + JSON), no XPLM calls: load on the worker thread.

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

class AirportTypes {
public:
    // Returns false (and stays empty) if the file is missing or malformed.
    bool load(const std::filesystem::path& file);

    // "large_airport", "medium_airport", "seaplane_base", or "" if not listed.
    std::string typeOf(const std::string& icao) const;

    std::size_t size() const { return m_types.size(); }

    // Major hubs (data/major_hubs.json, schema 1: "airports" maps ICAO → note):
    // these default to the Hub density (engine/Density.h). False if missing.
    bool loadHubs(const std::filesystem::path& file);
    bool isHub(const std::string& icao) const { return m_hubs.count(icao) != 0; }
    std::size_t hubCount() const { return m_hubs.size(); }

private:
    std::unordered_map<std::string, std::string> m_types;
    std::unordered_set<std::string>              m_hubs;
};
