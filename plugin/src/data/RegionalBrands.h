#pragma once
// RegionalBrands — regional airlines that fly only in their partners' paint
// (data/regional_brands.json): a SkyWest static is drawn as United Express,
// Delta Connection, American Eagle or Alaska, never in SkyWest's own scheme
//. Which partner depends on the airport: the brands
// Wikipedia lists there, plus a quarter of each partner's own share of the
// airport mix; every partner equally if neither says anything.
//
// Pure logic (file IO + JSON), no XPLM calls.

#include "data/AirportData.h"
#include "data/WikiFallback.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

class RegionalBrands {
public:
    static std::optional<RegionalBrands> load(const std::filesystem::path& file);

    void addOperator(const std::string& op, std::vector<std::string> partners) { operators_[op] = std::move(partners); }
    void addArticle(const std::string& wikiKey, const std::string& partner) { articles_[wikiKey] = partner; }

    bool flysForPartners(const std::string& op) const { return operators_.count(op) > 0; }

    // Partner code → how much that brand serves this airport (unnormalized).
    std::map<std::string, float> presence(const WikiEntry* wiki, const AirportTable& table) const;

    // The operator's partners weighted by `presence` (equal when none of them
    // is present). Empty if the operator is not a regional.
    std::map<std::string, float> partnersFor(const std::string& op,
                                             const std::map<std::string, float>& presence) const;

    // Hand-set partner weights for operators at one airport ("airport_partners"):
    // where an operator flies almost entirely for one partner though others
    // park around it (2026-10-27: SkyWest at KSLC is Delta Connection, at KDEN
    // United Express). Operator → partner → weight; empty when none.
    std::map<std::string, std::map<std::string, float>> partnersAt(const std::string& icao) const {
        auto it = airportPartners_.find(icao);
        return it != airportPartners_.end() ? it->second : std::map<std::string, std::map<std::string, float>>{};
    }
    void setAirportPartner(const std::string& icao, const std::string& op, const std::string& partner, float w) {
        airportPartners_[icao][op][partner] = w;
    }

private:
    std::map<std::string, std::vector<std::string>> operators_;
    std::map<std::string, std::string>              articles_;
    std::map<std::string, std::map<std::string, std::map<std::string, float>>> airportPartners_;
};
