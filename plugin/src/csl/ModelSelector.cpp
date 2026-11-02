#include "csl/ModelSelector.h"
#include "csl/IcaoSize.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace ModelSelector {

namespace {

static bool allowed(const CslVariant& v, const VariantSet* used);
}  // namespace

bool inService(const CslVariant& v, const std::string& type, const FleetTypes& fleets) {
    if (v.era != CslVariant::Era::Current) return false;   // a legacy or out-of-use scheme
    if (isGenericLivery(v)) return true;
    const auto it = fleets.find(v.airlineCode);
    return it != fleets.end() && it->second.count(type) > 0;
}

namespace {

// Collect variants matching `pred(sizeCode, variant)`, then pick one by seed.
// The size code is looked up once per ICAO type, not per variant. Only
// airliner types are ever candidates (no GA, business or military types at
// airline stands). Returns nullptr if no candidates.
template<typename Pred>
static const CslVariant* pick(const CslDb& db, Pred pred, std::uint32_t seed,
                              const VariantSet* used, const FleetTypes* fleets = nullptr) {
    std::vector<const CslVariant*> candidates;
    for (const auto& [type, variants] : db) {
        if (!IcaoSize::isAirliner(type)) continue;
        const char size = IcaoSize::lookup(type);
        for (const auto& v : variants)
            if (pred(size, v) && allowed(v, used) && (!fleets || inService(v, type, *fleets)))
                candidates.push_back(&v);
    }

    if (candidates.empty()) return nullptr;
    return candidates[seed % candidates.size()];
}

static bool isGeneric(const CslVariant& v) { return isGenericLivery(v); }

// Retired and cargo paint is never used. Specials: each scheme once per
// airport, and at most one special per airline per airport
// (several Frontier specials at one airport).
static bool allowed(const CslVariant& v, const VariantSet* used) {
    if (v.retired || v.cargo) return false;
    if (!v.special || !used) return true;
    for (const CslVariant* u : *used)
        if (u == &v || u->airlineCode == v.airlineCode) return false;
    return true;
}

// Independent 32-bit sub-seeds from one stand seed (splitmix-style mixing).
static std::uint32_t mix(std::uint32_t seed, std::uint32_t salt) {
    std::uint64_t z = (static_cast<std::uint64_t>(seed) << 32 | salt) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<std::uint32_t>(z ^ (z >> 31));
}

// Weighted pick from (key, weight) pairs; empty if nothing has weight.
static std::string weightedPick(const std::vector<std::pair<std::string, float>>& items,
                                std::uint32_t seed) {
    double total = 0.0;
    for (const auto& kv : items) total += kv.second;
    if (total <= 0.0) return {};
    double r = (static_cast<double>(seed) / 4294967296.0) * total;
    for (const auto& kv : items) {
        r -= kv.second;
        if (r < 0.0) return kv.first;
    }
    return items.back().first;
}

// Standard liveries first: a special scheme is one aircraft in a whole fleet,
// so where the airline also has a standard livery for this type a special is
// chosen only 1 time in kSpecialOdds (and, via `used`, once per airport).
// If only specials exist they are used (still once each).
constexpr std::uint32_t kSpecialOdds = 20;   // was 8 (specials rarer)

// Newer paint first (CslVariant::Era): a current livery, else a legacy one
// (an older scheme still flying; also 1 time in kLegacyOdds when both
// exist), else the newest outdated scheme.
constexpr std::uint32_t kLegacyOdds = 4;

template<typename Pred>
static const CslVariant* pickWhere(const CslEntry& entry, std::uint32_t seed, const VariantSet* used,
                                   Pred pred) {
    std::vector<const CslVariant*> cur, leg, outd;
    int newest = 0;
    for (const auto& v : entry) {
        if (!pred(v) || !allowed(v, used)) continue;
        if (v.era == CslVariant::Era::Current)     cur.push_back(&v);
        else if (v.era == CslVariant::Era::Legacy) leg.push_back(&v);
        else { outd.push_back(&v); newest = std::max(newest, v.schemeYear); }
    }
    const std::vector<const CslVariant*>* pool = &outd;
    if (!cur.empty() && !leg.empty()) pool = mix(seed, 12) % kLegacyOdds == 0 ? &leg : &cur;
    else if (!cur.empty())            pool = &cur;
    else if (!leg.empty())            pool = &leg;
    std::vector<const CslVariant*> standard, special;
    for (const CslVariant* v : *pool)
        if (pool != &outd || v->schemeYear == newest) (v->special ? special : standard).push_back(v);
    const bool takeSpecial = !special.empty() &&
                             (standard.empty() || mix(seed, 11) % kSpecialOdds == 0);
    const auto& c = takeSpecial ? special : standard;
    return c.empty() ? nullptr : c[seed % c.size()];
}

static const CslVariant* pickVariant(const CslEntry& entry, std::uint32_t seed,
                                     bool (*pred)(const CslVariant&, const std::string&),
                                     const std::string& airline, const VariantSet* used) {
    return pickWhere(entry, seed, used, [&](const CslVariant& v) { return pred(v, airline); });
}

// Paint that shows the airline: its brand when the audit recorded one (a
// regional's Delta Connection CRJ for a Delta static; an Iberia-filed A330 in
// LEVEL paint is LEVEL, not Iberia), else the code the library files it under.
static bool liveryIs(const CslVariant& v, const std::string& airline) {
    return v.brand.empty() ? v.airlineCode == airline : v.brand == airline;
}
static bool liveryGeneric(const CslVariant& v, const std::string&) { return isGenericLivery(v); }

} // namespace

// A plain livery with no airline. X-CSL's "@xx" codes are government and
// air-force aircraft (retired by the overrides), not generic paint.
bool isGenericLivery(const CslVariant& v) {
    return v.airlineCode.empty();
}

std::map<std::string, std::set<std::string>> loadRetiredTypes(const std::filesystem::path& file) {
    std::map<std::string, std::set<std::string>> out;
    std::ifstream f(file);
    if (!f.is_open()) return out;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) return out;
    if (auto it = j.find("airlines"); it != j.end() && it->is_object())
        for (const auto& [airline, types] : it->items())
            if (types.is_object())
                for (const auto& [type, note] : types.items()) out[airline].insert(type);
    return out;
}

float sizeMatchWeight(char typeSize, char standSize) {
    const int gap = standSize - typeSize;
    if (gap <= 0) return 1.0f;
    if (gap == 1) return 0.5f;
    if (gap == 2) return 0.15f;
    return 0.05f;
}

float listedShare(const std::vector<std::string>& listed, const std::map<std::string, float>& parked) {
    float all = 0.0f, mine = 0.0f;
    for (const auto& [a, w] : parked) {
        all += w;
        if (std::find(listed.begin(), listed.end(), a) != listed.end()) mine += w;
    }
    return all > 0.0f ? mine / all : 1.0f;
}

std::map<std::string, float> longHaulVisitors(const AirportTable& table) {
    std::map<std::string, float> out;
    float total = 0.0f;
    for (const auto& [a, w] : table.airlines) total += w;
    if (total <= 0.0f) return out;
    for (const auto& [a, w] : table.airlines) {
        if (w / total >= kVisitorMaxShare) continue;
        auto it = table.types.find(a);
        if (it == table.types.end()) continue;
        float all = 0.0f, wide = 0.0f;
        for (const auto& [type, tw] : it->second) {
            all += tw;
            const char sz = IcaoSize::lookup(type);
            if (sz == 'E' || sz == 'F') wide += tw;
        }
        if (all > 0.0f && wide >= kVisitorWideShare * all) out[a] = w / total;   // share of the airport
    }
    return out;
}

std::size_t visitorStands(const std::map<std::string, float>& visitors, float dailyArrivals) {
    if (visitors.empty() || dailyArrivals <= 0.0f) return 0;
    float share = 0.0f;
    for (const auto& [a, w] : visitors) share += w;
    const float daily = share * dailyArrivals;
    if (daily < 1.0f) return 0;
    const auto n = static_cast<std::size_t>(std::lround(daily * kVisitorGroundHours / 24.0f));
    return std::min(std::max<std::size_t>(n, 1), kMaxVisitorStands);
}

int shareCap(float share, std::size_t placedSoFar) {
    const double n = static_cast<double>(placedSoFar + 1);   // counting the stand being filled
    return std::max(1, static_cast<int>(std::ceil(std::max(share, 0.0f) * n * kMaxShareFactor)));
}

int rareAirlineCap(float share, float parkedAtOnce) {
    if (!(parkedAtOnce > 0.0f) || !(share > 0.0f)) return -1;
    const float parked = share * parkedAtOnce;
    if (parked >= kRareBelow) return -1;
    return std::max(1, static_cast<int>(std::lround(kMaxShareFactor * parked)));
}

std::size_t clearBlanketLists(std::vector<Stand>& stands) {
    auto keyOf = [](const Stand& s) {
        std::vector<std::string> a = s.airlines;
        std::sort(a.begin(), a.end());
        std::string k;
        for (const auto& x : a) k += x + ' ';
        return k;
    };
    std::map<std::string, std::size_t> uses;
    for (const auto& s : stands)
        if (!s.airlines.empty() && !s.airlinesOverridden) ++uses[keyOf(s)];
    std::size_t cleared = 0;
    for (auto& s : stands) {
        if (s.airlines.empty() || s.airlinesOverridden) continue;
        const std::size_t n = uses[keyOf(s)];
        if (n >= kBlanketMinStands && static_cast<double>(n) >= kBlanketShare * static_cast<double>(stands.size())) {
            s.airlines.clear();
            ++cleared;
        }
    }
    return cleared;
}

std::size_t retireTypes(CslDb& db, const std::map<std::string, std::set<std::string>>& retired) {
    std::size_t n = 0;
    for (const auto& [airline, types] : retired)
        for (const auto& type : types) {
            auto it = db.find(type);
            if (it == db.end()) continue;
            for (auto& v : it->second)
                if (!v.retired && (v.airlineCode == airline || v.brand == airline)) { v.retired = true; ++n; }
        }
    return n;
}

std::size_t fillTypesFromLiveries(AirportTable& table, const CslDb& db, const FleetTypes* fleets) {
    std::size_t filled = 0;
    for (const auto& [airline, w] : table.airlines) {
        auto& types = table.types[airline];
        if (!types.empty()) continue;
        // Only paint of a scheme in use, on a type the airline flies when its
        // fleet is known (an old Aeroflot type at VOGA).
        const auto fleet = fleets ? fleets->find(airline) : FleetTypes::const_iterator{};
        const bool known = fleets && fleet != fleets->end();
        for (const auto& [type, entry] : db) {
            if (!IcaoSize::isAirliner(type)) continue;
            if (known && !fleet->second.count(type)) continue;
            for (const auto& v : entry)
                if (v.airlineCode == airline && !v.retired && v.era == CslVariant::Era::Current) {
                    types[type] = 1.0f;
                    break;
                }
        }
        if (types.empty()) table.types.erase(airline);
        else ++filled;
    }
    return filled;
}

AreaMix areaMix(const Stand& stand, const AirportTable& table, const TerminalZones::Zones* zones) {
    const double kPi = 3.14159265358979323846;
    const double cosLat = std::cos(stand.lat * kPi / 180.0);
    const int zone = zones ? zones->zoneOf(stand.lat, stand.lon) : -1;
    auto mixWithin = [&](double sigma, double cutoff) {
        AreaMix m;
        for (std::size_t i = 0; i < table.clusters.size(); ++i) {
            const auto& c = table.clusters[i];
            if (zone >= 0 && zones->clusterZone(i) != zone) continue;   // another terminal
            const double dn = (c.lat - stand.lat) * 110574.0;
            const double de = (c.lon - stand.lon) * 111320.0 * cosLat;
            const double d  = std::sqrt(dn * dn + de * de);
            if (d > cutoff) continue;
            const float w = static_cast<float>(std::exp(-(d / sigma) * (d / sigma)));
            m.evidence += w * c.n;
            for (const auto& [a, n] : c.airlines) m.airlines[a] += w * n;
            for (const auto& [a, byType] : c.types)
                for (const auto& [t, n] : byType) m.types[a][t] += w * n;
        }
        return m;
    };
    // The stand's own neighbourhood first, the wider area only where that
    // says too little.
    AreaMix nearMix = mixWithin(kAreaNearSigmaM, kAreaNearCutoffM);
    if (nearMix.evidence >= kMinAreaEvidence) return nearMix;
    return mixWithin(kAreaSigmaM, kAreaCutoffM);
}

Choice selectWithTable(const Stand&        stand,
                       const CslDb&        db,
                       const AirportTable& table,
                       std::uint32_t       seed,
                       const VariantSet*   usedSpecials,
                       const Extras*       extras) {
    Choice out;
    const char target = (stand.widthCode >= 'A' && stand.widthCode <= 'F') ? stand.widthCode : 'C';
    const TerminalZones::Zones* zones   = extras ? extras->zones : nullptr;
    const RelatedTypes*         related = extras ? extras->related : nullptr;
    const RegionalBrands*       brands  = extras ? extras->brands : nullptr;

    // A type is usable if we know its size, it fits the stand, and the CSL
    // library has at least one model of it.
    const double runway = extras ? extras->longestRunwayM : 0.0;
    auto fits = [&](const std::string& type) {
        return IcaoSize::isAirliner(type) && IcaoSize::lookup(type) <= target && db.count(type) > 0 &&
               (runway <= 0.0 || IcaoSize::minRunwayM(type) <= runway);
    };
    // A stand override's type list (data/stand_overrides.json), if any.
    auto typeAllowed = [&](const std::string& data, const std::string& drawn) {
        if (stand.allowedTypes.empty()) return true;
        const auto& a = stand.allowedTypes;
        return std::find(a.begin(), a.end(), data) != a.end() || std::find(a.begin(), a.end(), drawn) != a.end();
    };
    // A type the airline has retired (data/retired_types.json).
    auto retiredFor = [&](const std::string& airline, const std::string& type) {
        if (!extras) return false;
        auto it = extras->retiredTypes.find(airline);
        return it != extras->retiredTypes.end() && it->second.count(type) > 0;
    };
    // A data type as the CSL type to draw: itself, else its look-alike.
    auto addType = [&](std::vector<std::pair<std::string, float>>& v, const std::string& airline,
                       const std::string& type, float w) {
        if (retiredFor(airline, type)) return;
        const std::string s = related ? related->substitute(type, db) : (db.count(type) ? type : std::string{});
        if (s.empty() || !fits(s) || !typeAllowed(type, s) || retiredFor(airline, s)) return;
        w *= sizeMatchWeight(IcaoSize::lookup(s), target);
        for (auto& kv : v)
            if (kv.first == s) { kv.second += w; return; }
        v.emplace_back(s, w);
    };
    // What parks around this stand, if the data says enough about it; else
    // what parks at its terminal.
    if (zones && zones->empty()) zones = nullptr;
    const int zone = zones ? zones->zoneOf(stand.lat, stand.lon) : -1;
    const AreaMix area = areaMix(stand, table, zones);
    const bool useArea = area.evidence >= kMinAreaEvidence;
    const TerminalZones::Mix* zoneMix = (!useArea && zones) ? zones->mixFor(zone) : nullptr;
    const std::map<std::string, float>* localAirlines =
        useArea ? &area.airlines : zoneMix ? &zoneMix->airlines : nullptr;
    const std::map<std::string, std::map<std::string, float>>* localTypes =
        useArea ? &area.types : zoneMix ? &zoneMix->types : nullptr;
    out.fromArea = useArea;
    out.fromZone = zoneMix != nullptr;
    // The airline's fitting types: what it parks in this area (or terminal)
    // if known, else its airport-wide mix.
    auto fittingTypes = [&](const std::string& airline) {
        std::vector<std::pair<std::string, float>> v;
        if (localTypes) {
            auto it = localTypes->find(airline);
            if (it != localTypes->end())
                for (const auto& [type, w] : it->second) addType(v, airline, type, w);
            if (!v.empty()) return v;
        }
        auto it = table.types.find(airline);
        if (it != table.types.end())
            for (const auto& [type, w] : it->second) addType(v, airline, type, w);
        return v;
    };
    // How much of what the airline parks (here, else airport-wide) fits this
    // stand, size-weighted: the airline's weight at this stand is its share
    // of aircraft of this size, not of all its flights (at
    // KDFW's widebody stands American, 70 % of the airport but 48 % of its
    // widebodies, left almost no room for Korean, JAL, Emirates …).
    auto sizeFit = [&](const std::string& airline) -> float {
        const std::map<std::string, float>* src = nullptr;
        if (localTypes) {
            auto it = localTypes->find(airline);
            if (it != localTypes->end()) {
                std::vector<std::pair<std::string, float>> v;
                for (const auto& [type, w] : it->second) addType(v, airline, type, w);
                if (!v.empty()) src = &it->second;
            }
        }
        if (!src) {
            auto it = table.types.find(airline);
            if (it == table.types.end()) return 1.0f;
            src = &it->second;
        }
        float raw = 0.0f, fit = 0.0f;
        for (const auto& [type, w] : *src) raw += w;
        for (const auto& [type, w] : fittingTypes(airline)) fit += w;
        return raw > 0.0f ? fit / raw : 1.0f;
    };

    // 1. Airline: stand's own list (1301), weighted by the table where known;
    //    if none of those fly anything that fits here, the airport-wide mix.
    // An airline is drawable when a fitting type has its own paint (or, for a
    // partner-paint airline, is handled below) or a plain generic livery: an
    // airline the CSL library has no paint for is never shown in another
    // airline's livery (Akasa and Air Arabia Abu Dhabi
    // turned into random airlines at VOBL / OMAA).
    // A regional that flies for partners is drawable in a partner's paint
    // (2026-10-27: Horizon at KLAS with no Alaska E175 in the library came
    // out in Royal Jordanian and CityJet paint).
    // A stand whose area fixes the regionals' brand (stand.brand, a hand-made
    // area: SkyWest at KSFO's B gates flies Alaska SkyWest): the regional
    // only in that paint, not plain and not another partner's — without it
    // (X-CSL has no Alaska E175) the area's other airlines take the stand.
    auto drawable = [&](const std::string& airline) {
        const bool regional = brands && brands->flysForPartners(airline);
        auto partners = regional ? brands->partnersFor(airline, extras->brandPresence)
                                 : std::map<std::string, float>{};
        const bool fixedBrand = regional && !stand.brand.empty() && partners.count(stand.brand);
        if (fixedBrand) partners = {{stand.brand, 1.0f}};
        auto paintsFor = [&](const CslVariant& v) {
            if (!regional) return liveryIs(v, airline);
            return partners.count(v.brand.empty() ? v.airlineCode : v.brand) > 0;
        };
        for (const auto& [type, w] : fittingTypes(airline))
            for (const auto& v : db.at(type))
                if (allowed(v, usedSpecials) && (paintsFor(v) || (!fixedBrand && isGenericLivery(v)))) return true;
        return false;
    };
    auto onlyFitting = [&](std::vector<std::pair<std::string, float>> v) {
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const auto& kv) {
                                   return (extras && extras->capped && extras->capped->count(kv.first)) ||
                                          fittingTypes(kv.first).empty() || !drawable(kv.first);
                               }),
                v.end());
        return v;
    };
    std::vector<std::pair<std::string, float>> airlines;
    for (const auto& a : stand.airlines) {
        // A hand-made area's own weights, else the airport table's.
        if (auto w = stand.airlineWeights.find(a); w != stand.airlineWeights.end()) {
            airlines.emplace_back(a, w->second);
            continue;
        }
        auto it = table.airlines.find(a);
        airlines.emplace_back(a, it != table.airlines.end() ? it->second : 0.01f);
    }
    airlines = onlyFitting(std::move(airlines));
    // The scenery's list is a hint; what the data sees parked here wins
    // (kMinListedShare). An override's list always stands.
    if (!airlines.empty() && localAirlines && !stand.airlinesOverridden &&
        listedShare(stand.airlines, *localAirlines) < kMinListedShare)
        airlines.clear();
    // Little data around the stand (remote and maintenance ramps, under
    // kMinAreaEvidence): the list still goes if nothing it names was ever
    // seen parked here, and what was seen comes before the airport-wide mix
    // (Delta and Iberia on United's KSFO maintenance ramps,
    // listed "DAL" by Global Airports).
    const std::map<std::string, float>* nearby =
        localAirlines ? localAirlines : area.evidence > 0.0f ? &area.airlines : nullptr;
    if (!airlines.empty() && !localAirlines && nearby && !stand.airlinesOverridden &&
        listedShare(stand.airlines, *nearby) <= 0.0f)
        airlines.clear();
    // A stand override (data/stand_overrides.json) is strict: only its
    // airlines, else any aircraft of its types in its brand's paint, else
    // nothing — never the airport-wide mix.
    const bool overridden = !stand.allowedTypes.empty() || !stand.brand.empty();
    // A hand-made area (airlines, brand, no type list): none of its airlines
    // fits this stand → it stays empty. "Any aircraft in the brand's paint"
    // drew Horizon's retired Alaska Q400 on a small stand of X-Plane 11's old
    // KSFO Terminal 1 (2026-10-28).
    if (airlines.empty() && stand.airlinesOverridden && stand.allowedTypes.empty()) {
        out.reason = Choice::Reason::NoTableMatch;
        return out;
    }
    if (airlines.empty() && overridden) {
        std::vector<const CslVariant*> cands;
        for (const auto& [type, entry] : db) {
            if (!fits(type) || !typeAllowed(type, type)) continue;
            for (const auto& v : entry)
                if (allowed(v, usedSpecials) && v.era != CslVariant::Era::Outdated &&
                    (stand.brand.empty() || v.brand == stand.brand ||
                     (v.brand.empty() && v.airlineCode == stand.brand)))
                    cands.push_back(&v);
        }
        if (!cands.empty()) {
            out.model   = cands[mix(seed, 8) % cands.size()];
            out.airline = stand.brand.empty() ? out.model->airlineCode : stand.brand;
            out.reason  = Choice::Reason::PartnerBrand;
        }
        return out;
    }
    // A stand kept for long-haul visitors (Extras::visitorsOnly): one of
    // them, if any can be drawn here.
    if (extras && extras->visitorsOnly && extras->visitors) {
        // Only visitors that park at this spot, where the data says who parks
        // here (2026-10-28: Royal Jordanian at United's KORD terminal).
        std::vector<std::pair<std::string, float>> v;
        for (const auto& [a, w] : *extras->visitors) {
            if (localAirlines) {
                auto it = localAirlines->find(a);
                if (it == localAirlines->end() || it->second <= 0.0f) continue;
            }
            v.emplace_back(a, w);
        }
        v = onlyFitting(std::move(v));
        if (!v.empty()) {
            airlines = std::move(v);
            out.fromArea = out.fromZone = false;
        }
    }
    if (airlines.empty() && nearby) {
        // Only airlines that actually park in this part of the airport.
        for (const auto& [a, w] : *nearby) airlines.emplace_back(a, w);
        airlines = onlyFitting(std::move(airlines));
    }
    // Long-haul visitors only where the data puts them (their kept stands or
    // where they park), never from the airport-wide mix (an
    // ANA 777 at KORD's United Terminal 1 when no local airline fit a big stand).
    auto visitor = [&](const std::string& a) {
        return extras && extras->visitors && extras->visitors->count(a) > 0;
    };
    if (airlines.empty()) {
        // Airport-wide, but not an airline that parks at another terminal.
        // Nothing fits: the stand stays empty (no all-airlines fallback).
        for (const auto& [a, w] : table.airlines)
            if (!visitor(a) && (!zones || !zones->basedElsewhere(a, zone))) airlines.emplace_back(a, w);
        airlines = onlyFitting(std::move(airlines));
        out.fromArea = out.fromZone = false;
    }

    // Airlines already showing their share of the stands (kMaxShareFactor):
    // others first — those around the stand; where the data says who parks
    // here and all of them are full, still them (never airlines from another
    // terminal: random airlines at KSFO's Alaska T1 once
    // Alaska was full); without local data, the airport's (not based at
    // another terminal); only if none fits, the full ones.
    // Not where the data says who parks here or a hand-made area says so: the
    // local mix decides there, and rare airlines are held by Extras::capped
    // (Southwest, 2 % of KSFO's ground time but 70 % of
    // Terminal 2's south gates, was "full" after one aircraft and United
    // took its gates).
    if (extras && extras->full && !extras->full->empty() && !localAirlines && !stand.airlinesOverridden) {
        auto notFull = [&](std::vector<std::pair<std::string, float>> v) {
            v.erase(std::remove_if(v.begin(), v.end(), [&](const auto& kv) { return extras->full->count(kv.first) > 0; }),
                    v.end());
            return v;
        };
        auto keep = notFull(airlines);
        if (keep.empty() && !overridden && !extras->visitorsOnly && !nearby) {
            std::vector<std::pair<std::string, float>> wide;
            for (const auto& [a, w] : table.airlines)
                if (!visitor(a) && (!zones || !zones->basedElsewhere(a, zone))) wide.emplace_back(a, w);
            keep = notFull(onlyFitting(std::move(wide)));
            if (!keep.empty()) out.fromArea = out.fromZone = false;
        }
        if (!keep.empty()) airlines = std::move(keep);
    }
    for (auto& kv : airlines) kv.second *= sizeFit(kv.first);
    out.airline = weightedPick(airlines, mix(seed, 1));
    // No airline of the airport fits the stand: it stays empty. A size-only
    // pick from the whole library drew any airline's paint (e.g.
    // a Lufthansa Regional ATR at OOMS, where no Lufthansa ATR ever parks).
    if (out.airline.empty()) {
        out.reason = Choice::Reason::NoTableMatch;
        return out;
    }

    // 2. Type from the airline's mix.
    const auto types = fittingTypes(out.airline);
    out.type = weightedPick(types, mix(seed, 2));
    const CslEntry& entry = db.at(out.type);

    // 3. Model. A regional that flies for partners: a partner's paint, on
    //    the chosen type or else another of its types; never its own.
    if (brands && brands->flysForPartners(out.airline)) {
        const std::string op = out.airline;
        auto partners = brands->partnersFor(op, extras->brandPresence);
        // Which partner here: the stand's own brand (override), else a partner
        // the scenery lists for this gate, else the partners' mainline
        // aircraft parked around it; only then airport-wide presence (KLAX:
        // SkyWest at gates 52 is American Eagle, at Terminal 8 United Express).
        {
            std::map<std::string, float> here;
            const auto fixed = extras->airportPartners.find(op);
            if (!stand.brand.empty() && partners.count(stand.brand)) {
                here[stand.brand] = 1.0f;
            } else if (fixed != extras->airportPartners.end()) {
                for (const auto& [p, w] : fixed->second)
                    if (partners.count(p)) here[p] = w;
            } else {
                for (const auto& a : stand.airlines)
                    if (partners.count(a)) here[a] = 1.0f;
                if (here.empty() && localAirlines)
                    for (const auto& [a, w] : *localAirlines)
                        if (partners.count(a) && w > 0.0f) here[a] = w;
            }
            if (!here.empty()) partners = std::move(here);
        }
        auto paints = [](const CslVariant& v, const std::string& partner) {
            return v.brand == partner || (v.brand.empty() && v.airlineCode == partner);
        };
        // allowAbsent: also a partner that does not serve this airport (when
        // no type has paint of one that does).
        auto partnerOn = [&](const std::string& type, std::uint32_t s, bool allowAbsent) -> const CslVariant* {
            const CslEntry& e = db.at(type);
            std::vector<std::pair<std::string, float>> avail;
            for (const auto& [p, w] : partners)
                for (const auto& v : e)
                    if (allowed(v, usedSpecials) && paints(v, p)) { avail.emplace_back(p, w); break; }
            std::string p = weightedPick(avail, mix(s, 5));
            if (p.empty() && allowAbsent && !avail.empty()) p = avail[mix(s, 6) % avail.size()].first;
            if (p.empty()) return nullptr;
            // The regional's own aircraft in that paint first, else anyone's.
            if (auto m = pickWhere(e, mix(s, 3), usedSpecials,
                                   [&](const CslVariant& v) { return v.airlineCode == op && v.brand == p; }))
                return m;
            return pickWhere(e, mix(s, 3), usedSpecials, [&](const CslVariant& v) { return paints(v, p); });
        };
        // The chosen type, then the regional's other types; first only
        // partners present here, then any partner.
        for (const bool allowAbsent : {false, true}) {
            if ((out.model = partnerOn(out.type, seed, allowAbsent))) {
                out.reason = Choice::Reason::PartnerBrand;
                return out;
            }
            for (std::size_t k = 0; k < types.size(); ++k) {
                const auto& t = types[(mix(seed, 7) + k) % types.size()].first;
                if (t != out.type && (out.model = partnerOn(t, seed, allowAbsent))) {
                    out.type   = t;
                    out.reason = Choice::Reason::PartnerBrand;
                    return out;
                }
            }
        }
        // No partner paint on anything that fits: a plain livery of the type,
        // else nothing — never another airline's paint.
        out.model  = pickWhere(entry, mix(seed, 3), usedSpecials,
                               [&](const CslVariant& v) { return isGenericLivery(v); });
        out.reason = Choice::Reason::GenericLivery;
        return out;
    }

    // The newest paint the airline has on a type: 0 current, 1 legacy,
    // 2 outdated, 3 none.
    auto bestEra = [&](const std::string& type) {
        int best = 3;
        for (const auto& v : db.at(type))
            if (liveryIs(v, out.airline) && allowed(v, usedSpecials)) best = std::min(best, static_cast<int>(v.era));
        return best;
    };
    const int outdated = static_cast<int>(CslVariant::Era::Outdated);
    if (bestEra(out.type) < outdated &&
        (out.model = pickVariant(entry, mix(seed, 3), liveryIs, out.airline, usedSpecials))) {
        out.reason = Choice::Reason::ExactLivery;
        return out;
    }
    // The airline's livery on another of its fitting types beats a wrong
    // livery, and its newer paint there beats an out-of-use scheme here.
    std::vector<std::pair<std::string, float>> withLivery, newer;
    for (const auto& [type, w] : types) {
        const int e = bestEra(type);
        if (e < 3)        withLivery.emplace_back(type, w);
        if (e < outdated) newer.emplace_back(type, w);
    }
    if (!newer.empty()) {
        out.type   = weightedPick(newer, mix(seed, 4));
        out.model  = pickVariant(db.at(out.type), mix(seed, 3), liveryIs, out.airline, usedSpecials);
        out.reason = Choice::Reason::AirlineOtherType;
        return out;
    }
    // Nothing newer anywhere: the newest out-of-use scheme of this type, else
    // of another type ("if there is no other option, use
    // the newest livery available").
    if ((out.model = pickVariant(entry, mix(seed, 3), liveryIs, out.airline, usedSpecials))) {
        out.reason = Choice::Reason::ExactLivery;
        return out;
    }
    if (!withLivery.empty()) {
        out.type   = weightedPick(withLivery, mix(seed, 4));
        out.model  = pickVariant(db.at(out.type), mix(seed, 3), liveryIs, out.airline, usedSpecials);
        out.reason = Choice::Reason::AirlineOtherType;
        return out;
    }
    if ((out.model = pickVariant(entry, mix(seed, 3), liveryGeneric, out.airline, usedSpecials))) {
        out.reason = Choice::Reason::GenericLivery;
        return out;
    }
    // A plain livery on another of the airline's fitting types.
    for (std::size_t k = 0; k < types.size(); ++k) {
        const auto& t = types[(mix(seed, 9) + k) % types.size()].first;
        if ((out.model = pickVariant(db.at(t), mix(seed, 3), liveryGeneric, out.airline, usedSpecials))) {
            out.type   = t;
            out.reason = Choice::Reason::GenericLivery;
            return out;
        }
    }
    // Nothing in this airline's paint or a plain one: no aircraft rather than
    // another airline's livery (the stand is tried one size smaller, else
    // stays empty).
    out.model  = nullptr;
    out.reason = Choice::Reason::AnyLivery;
    return out;
}

Choice selectCargo(const Stand&        stand,
                   const CslDb&        db,
                   const AirportTable& table,
                   std::uint32_t       seed,
                   const Extras*       extras) {
    Choice out;
    const char   target  = (stand.widthCode >= 'A' && stand.widthCode <= 'F') ? stand.widthCode : 'D';
    const double runway  = extras ? extras->longestRunwayM : 0.0;
    const RelatedTypes* related = extras ? extras->related : nullptr;
    auto fits = [&](const std::string& type) {
        return IcaoSize::isAirliner(type) && IcaoSize::lookup(type) <= target && db.count(type) > 0 &&
               (runway <= 0.0 || IcaoSize::minRunwayM(type) <= runway);
    };
    auto paints = [](const CslVariant& v, const std::string& airline) {
        return !v.retired && v.cargo && (v.brand.empty() ? v.airlineCode == airline : v.brand == airline);
    };
    // The airline's types with its cargo paint: those seen here first, else
    // any type it has cargo paint for.
    auto cargoTypes = [&](const std::string& airline) {
        std::vector<std::pair<std::string, float>> v;
        auto add = [&](const std::string& type, float w) {
            const std::string s = related ? related->substitute(type, db) : type;
            if (s.empty() || !fits(s)) return;
            for (const auto& var : db.at(s))
                if (paints(var, airline)) {
                    v.emplace_back(s, w * sizeMatchWeight(IcaoSize::lookup(s), target));
                    return;
                }
        };
        if (auto it = table.cargoTypes.find(airline); it != table.cargoTypes.end())
            for (const auto& [type, w] : it->second) add(type, w);
        if (v.empty())
            for (const auto& [type, entry] : db) {
                if (!fits(type)) continue;
                for (const auto& var : entry)
                    if (paints(var, airline)) {
                        v.emplace_back(type, sizeMatchWeight(IcaoSize::lookup(type), target));
                        break;
                    }
            }
        return v;
    };
    std::vector<std::pair<std::string, float>> airlines;
    for (const auto& [a, w] : table.cargoAirlines)
        if (!cargoTypes(a).empty()) airlines.emplace_back(a, w);
    if (extras && extras->full && !extras->full->empty()) {   // see selectWithTable
        std::vector<std::pair<std::string, float>> keep;
        for (const auto& kv : airlines)
            if (!extras->full->count(kv.first)) keep.push_back(kv);
        if (!keep.empty()) airlines = std::move(keep);
    }
    out.airline = weightedPick(airlines, mix(seed, 21));
    if (out.airline.empty()) return out;
    const auto types = cargoTypes(out.airline);
    out.type = weightedPick(types, mix(seed, 22));
    std::vector<const CslVariant*> cands;
    for (const auto& v : db.at(out.type))
        if (paints(v, out.airline)) cands.push_back(&v);
    if (!cands.empty()) {
        out.model  = cands[mix(seed, 23) % cands.size()];
        out.reason = Choice::Reason::ExactLivery;
    }
    return out;
}

const CslVariant* selectForStand(const Stand&      stand,
                                 const CslDb&      db,
                                 std::uint32_t     seed,
                                 const VariantSet* usedSpecials,
                                 const FleetTypes* fleets) {
    if (db.empty()) return nullptr;

    char target = (stand.widthCode >= 'A' && stand.widthCode <= 'F')
                  ? stand.widthCode : 'C';

    const auto& airlines = stand.airlines;
    auto servesStand = [&](const CslVariant& v) {
        return !v.airlineCode.empty()
            && std::find(airlines.begin(), airlines.end(), v.airlineCode) != airlines.end();
    };

    // 1. Airline + exact size.
    if (!airlines.empty()) {
        if (auto p = pick(db, [&](char size, const CslVariant& v) {
                return size == target && servesStand(v);
            }, seed, usedSpecials, fleets)) return p;
    }

    // 2. Airline, any size that still fits the stand.
    if (!airlines.empty()) {
        if (auto p = pick(db, [&](char size, const CslVariant& v) {
                return size <= target && servesStand(v);
            }, seed, usedSpecials, fleets)) return p;
    }

    // 3. Generic exact size.
    if (auto p = pick(db, [&](char size, const CslVariant& v) {
            return size == target && isGeneric(v);
        }, seed, usedSpecials, fleets)) return p;

    // 4. Any exact size (includes airline-specific if no generics).
    if (auto p = pick(db, [&](char size, const CslVariant& /*v*/) {
            return size == target;
        }, seed, usedSpecials, fleets)) return p;

    // 5. Smaller sizes only, nearest first. Never larger than the stand
    //    (never return a model whose wingspan exceeds the stand's
    //    size code) — nothing that fits → nullptr → the stand stays empty.
    for (int want = target - 1; want >= 'A'; --want) {
        if (auto p = pick(db, [&](char size, const CslVariant& v) {
                return size == static_cast<char>(want) && isGeneric(v);
            }, seed, usedSpecials, fleets)) return p;
        if (auto p = pick(db, [&](char size, const CslVariant& /*v*/) {
                return size == static_cast<char>(want);
            }, seed, usedSpecials, fleets)) return p;
    }
    return nullptr;
}

std::uint32_t positionSeed(const Stand& s) {
    std::uint64_t la = 0, lo = 0;
    static_assert(sizeof(double) == 8, "");
    std::memcpy(&la, &s.lat, 8);
    std::memcpy(&lo, &s.lon, 8);
    std::uint64_t h = 14695981039346656037ULL;
    for (int i = 0; i < 8; ++i) { h ^= (la >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; }
    for (int i = 0; i < 8; ++i) { h ^= (lo >> (i * 8)) & 0xFFu; h *= 1099511628211ULL; }
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

} // namespace ModelSelector
