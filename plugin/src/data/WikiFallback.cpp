#include "data/WikiFallback.h"

#include "csl/IcaoSize.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace WikiFallback {

namespace {

nlohmann::json parseFile(const std::filesystem::path& file) {
    std::ifstream f(file);
    if (!f.is_open()) return nlohmann::json::value_t::discarded;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1)
        return nlohmann::json::value_t::discarded;
    return j;
}

Fleets::Mix readMix(const nlohmann::json& o) {
    Fleets::Mix m;
    if (!o.is_object()) return m;
    for (const auto& [k, w] : o.items())
        if (w.is_number() && w.get<float>() > 0.0f) m[k] = w.get<float>();
    return m;
}

void normalize(std::map<std::string, float>& m) {
    float total = 0.0f;
    for (const auto& [k, w] : m) total += w;
    if (total <= 0.0f) { m.clear(); return; }
    for (auto& [k, w] : m) w /= total;
}

} // namespace

std::optional<WikiEntry> loadEntry(const std::filesystem::path& file, const std::string& icao) {
    const nlohmann::json j = parseFile(file);
    if (j.is_discarded()) return std::nullopt;
    auto ap = j.find("airports");
    if (ap == j.end() || !ap->is_object()) return std::nullopt;
    auto it = ap->find(icao);
    if (it == ap->end() || !it->is_object()) return std::nullopt;
    WikiEntry e;
    e.icao        = icao;
    e.country     = it->value("country", std::string{});
    e.airportType = it->value("airport_type", std::string{});
    if (auto s = it->find("destination_score"); s != it->end() && s->is_number())
        e.destinationScore = s->get<float>();
    if (auto a = it->find("airlines"); a != it->end()) e.airlines = readMix(*a);
    if (e.airlines.empty()) return std::nullopt;
    if (auto g = it->find("on_ground_est"); g != it->end() && g->is_number()) e.onGroundEst = g->get<float>();
    if (auto g = it->find("arrivals_est"); g != it->end() && g->is_number()) e.arrivalsEst = g->get<float>();
    if (auto c = it->find("cargo"); c != it->end() && c->is_object()) {
        if (auto a = c->find("airlines"); a != c->end()) e.cargoAirlines = readMix(*a);
        e.cargoDailyEst = c->value("daily_est", 0.0f);
    }
    if (auto p = it->find("pos"); p != it->end() && p->is_array() && p->size() == 2 && (*p)[0].is_number() &&
        (*p)[1].is_number()) {
        e.lat = (*p)[0].get<double>();
        e.lon = (*p)[1].get<double>();
    }
    return e;
}

std::string nearestWithData(const std::filesystem::path& file, double lat, double lon, const std::string& self,
                            double maxKm, double* distKm) {
    if (!std::isfinite(lat) || !std::isfinite(lon)) return {};
    const nlohmann::json j = parseFile(file);
    if (j.is_discarded()) return {};
    auto ap = j.find("airports");
    if (ap == j.end() || !ap->is_object()) return {};
    std::string best;
    double bestKm = maxKm;
    const double kx = std::cos(lat * 3.14159265358979323846 / 180.0);
    for (const auto& [icao, e] : ap->items()) {
        if (icao == self || !e.is_object()) continue;
        auto p = e.find("pos");
        if (p == e.end() || !p->is_array() || p->size() != 2 || !(*p)[0].is_number() || !(*p)[1].is_number()) continue;
        const double dn = ((*p)[0].get<double>() - lat) * 111.2, de = ((*p)[1].get<double>() - lon) * 111.2 * kx;
        const double km = std::sqrt(dn * dn + de * de);
        if (km < bestKm) { bestKm = km; best = icao; }
    }
    if (distKm && !best.empty()) *distKm = bestKm;
    return best;
}

bool isSmall(const WikiEntry& w) {
    if (w.onGroundEst >= 0.0f) return w.onGroundEst < kSmallOnGround;
    return w.destinationScore >= 0.0f && w.destinationScore < kSmallScore && w.airportType != "large_airport";
}

double typicalRangeKm(const std::string& t) {
    // Typical airline-service ranges, rounded up (manufacturer data): a type
    // is dropped only where it clearly cannot fly the distance.
    static const std::map<std::string, double> kRange{
        {"AT43", 1500}, {"AT45", 1500}, {"AT46", 1500}, {"AT72", 1600}, {"AT73", 1600}, {"AT75", 1600},
        {"AT76", 1600}, {"DH8A", 1700}, {"DH8B", 1700}, {"DH8C", 1700}, {"DH8D", 2100}, {"SF34", 1700},
        {"SB20", 2300}, {"JS31", 1300}, {"JS32", 1300}, {"JS41", 1400}, {"E120", 1500}, {"B190", 1300},
        {"SW4", 1300},  {"D328", 1700}, {"J328", 1900}, {"F50", 2000},  {"CRJ1", 3000}, {"CRJ2", 3000},
        {"CRJ7", 2700}, {"CRJ9", 2900}, {"CRJX", 2800}, {"E135", 3000}, {"E145", 3000}, {"E35L", 3200},
        {"E45X", 3700}, {"E170", 3900}, {"E75S", 3700}, {"E75L", 4000}, {"E175", 4000}, {"E190", 4500},
        {"E195", 4300}, {"E290", 5300}, {"E295", 4800}, {"F70", 3400},  {"F100", 3100}, {"B461", 3000},
        {"B462", 3000}, {"B463", 3000}, {"RJ70", 3000}, {"RJ85", 3000}, {"RJ1H", 3000}, {"SU95", 4600},
        {"AJ27", 3700}, {"C909", 3700}};
    auto it = kRange.find(t);
    return it == kRange.end() ? 0.0 : it->second;
}

std::optional<Fleets> loadFleets(const std::filesystem::path& file) {
    const nlohmann::json j = parseFile(file);
    if (j.is_discarded()) return std::nullopt;
    Fleets f;
    if (auto it = j.find("fleets"); it != j.end() && it->is_object())
        for (const auto& [code, cats] : it->items())
            if (cats.is_object())
                for (const auto& [cat, mix] : cats.items()) {
                    if (cat == "home") {
                        if (mix.is_array() && mix.size() == 2 && mix[0].is_number() && mix[1].is_number())
                            f.home[code] = {mix[0].get<double>(), mix[1].get<double>()};
                        continue;
                    }
                    auto m = readMix(mix);
                    if (!m.empty()) f.types[code][cat] = std::move(m);
                }
    if (auto it = j.find("brands"); it != j.end() && it->is_object())
        for (const auto& [brand, byCountry] : it->items())
            if (byCountry.is_object())
                for (const auto& [country, mix] : byCountry.items()) {
                    auto m = readMix(mix);
                    if (!m.empty()) f.brands[brand][country] = std::move(m);
                }
    if (auto it = j.find("airport_types"); it != j.end() && it->is_object())
        for (const auto& [code, byApt] : it->items())
            if (byApt.is_object())
                for (const auto& [icao, mix] : byApt.items()) {
                    auto m = readMix(mix);
                    if (!m.empty()) f.airportTypes[code][icao] = std::move(m);
                }
    if (auto it = j.find("cargo_fleets"); it != j.end() && it->is_object())
        for (const auto& [code, mix] : it->items()) {
            auto m = readMix(mix);
            if (!m.empty()) f.cargo[code] = std::move(m);
        }
    return f;
}

std::map<std::string, float> operators(const WikiEntry& wiki, const Fleets& fleets) {
    std::map<std::string, float> out;
    for (const auto& [key, w] : wiki.airlines) {
        if (key.rfind("brand:", 0) != 0) {
            out[key] += w;
            continue;
        }
        auto b = fleets.brands.find(key);
        if (b == fleets.brands.end()) continue;
        auto c = b->second.find(wiki.country);
        if (c == b->second.end()) c = b->second.find("*");
        if (c == b->second.end()) continue;
        float total = 0.0f;
        for (const auto& [op, f] : c->second) total += f;
        if (total <= 0.0f) continue;
        for (const auto& [op, f] : c->second) out[op] += w * f / total;
    }
    normalize(out);
    return out;
}

const Fleets::Mix* fleetMixFor(const Fleets& fleets, const std::string& code, const std::string& airportType,
                               double lat, double lon) {
    auto f = fleets.types.find(code);
    if (f == fleets.types.end()) return nullptr;
    auto get = [&](const char* cat) -> const Fleets::Mix* {
        auto c = f->second.find(cat);
        return c != f->second.end() ? &c->second : nullptr;
    };
    if (auto h = fleets.home.find(code); h != fleets.home.end() && std::isfinite(lat) && std::isfinite(lon)) {
        constexpr double kPi = 3.14159265358979323846;
        const double la1 = h->second.first * kPi / 180, lo1 = h->second.second * kPi / 180;
        const double la2 = lat * kPi / 180, lo2 = lon * kPi / 180;
        const double s = std::sin((la2 - la1) / 2) * std::sin((la2 - la1) / 2) +
                         std::cos(la1) * std::cos(la2) * std::sin((lo2 - lo1) / 2) * std::sin((lo2 - lo1) / 2);
        const double km = 2 * 6371.0 * std::asin(std::sqrt(s));
        if (km < kHomeRadiusKm) {
            // The fleet in service (Wikipedia fleet table) where known, else
            // every type adsb.lol saw it with.
            if (auto m = get("fleet")) return m;
            if (auto m = get("all")) return m;
        } else {
            const std::string band = km < 1500.0 ? "d0" : km < 4000.0 ? "d1" : "d2";
            const std::string size = airportType == "large_airport" ? "large_airport" : "medium_airport";
            if (auto m = get((band + "/" + size).c_str())) return m;   // e.g. Air India at a big Indian airport
            if (auto m = get(band.c_str())) return m;
        }
    }
    if (auto m = get(airportType == "large_airport" ? "large_airport" : "medium_airport")) return m;
    return get("all");
}

static bool isWidebody(const std::string& type) {
    const char sz = IcaoSize::lookup(type);
    return sz == 'E' || sz == 'F' || (sz == 'D' && type != "B752" && type != "B753");
}

void applyWidebodyShare(Fleets::Mix& types, const Fleets& fleets, const std::string& code, const std::string& icao) {
    auto f = fleets.types.find(code);
    if (f == fleets.types.end() || types.empty()) return;
    auto ws = f->second.find("widebody_share");
    if (ws == f->second.end()) return;
    auto it = ws->second.find(icao);
    if (it == ws->second.end()) return;
    const float share = std::min(std::max(it->second, 0.0f), 1.0f);
    Fleets::Mix narrow, wide;
    for (const auto& [t, w] : types) (isWidebody(t) ? wide : narrow)[t] += w;
    if (wide.empty()) {   // the mix here has none: the airline's own widebody types
        for (const char* cat : {"fleet", "all"})
            if (auto c = f->second.find(cat); c != f->second.end()) {
                for (const auto& [t, w] : c->second)
                    if (isWidebody(t)) wide[t] += w;
                if (!wide.empty()) break;
            }
    }
    if (wide.empty() || narrow.empty()) return;
    normalize(narrow);
    normalize(wide);
    types.clear();
    for (const auto& [t, w] : narrow) types[t] += (1.0f - share) * w;
    for (const auto& [t, w] : wide) types[t] += share * w;
}

std::optional<AirportTable> combine(std::optional<AirportTable> adsb,
                                    const WikiEntry*            wiki,
                                    const Fleets*               fleets,
                                    float*                      wikiShare,
                                    double                      lat,
                                    double                      lon) {
    if (wikiShare) *wikiShare = 0.0f;
    if (!wiki || (adsb && adsb->samples >= kFullTrustSamples)) return adsb;
    const Fleets noFleets;
    const Fleets& fl = fleets ? *fleets : noFleets;
    const auto ops = operators(*wiki, fl);
    if (ops.empty()) return adsb;

    const float fromAdsb = adsb ? std::clamp(static_cast<float>(adsb->samples) / kFullTrustSamples, 0.0f, 1.0f)
                                : 0.0f;
    AirportTable t;
    if (adsb) {
        t = std::move(*adsb);
    } else {
        t.icao        = wiki->icao;
        t.airportType = wiki->airportType;
    }
    std::map<std::string, float> mix;
    for (const auto& [a, w] : t.airlines) mix[a] += fromAdsb * w;
    for (const auto& [a, w] : ops)        mix[a] += (1.0f - fromAdsb) * w;
    normalize(mix);
    t.airlines = std::move(mix);
    if (t.airportType.empty()) t.airportType = wiki->airportType;

    // Types for airlines the adsb.lol table doesn't have (order: see isSmall).
    const bool small = isSmall(*wiki);
    for (const auto& [a, w] : t.airlines) {
        auto& types = t.types[a];
        if (!types.empty()) continue;
        if (auto h = fl.airportTypes.find(a); h != fl.airportTypes.end()) {
            if (auto m = h->second.find(wiki->icao); m != h->second.end()) {
                types = m->second;     // hand-checked for this airport: as is
                continue;
            }
        }
        const Fleets::Mix* m = nullptr;
        if (small)
            if (auto f = fl.types.find(a); f != fl.types.end())
                if (auto s = f->second.find("small_airport"); s != f->second.end()) m = &s->second;
        if (!m) m = fleetMixFor(fl, a, t.airportType, lat, lon);
        if (m) types = *m;
        applyWidebodyShare(types, fl, a, wiki->icao);
        // Range from the airline's home base.
        if (auto h = fl.home.find(a); h != fl.home.end() && std::isfinite(lat) && std::isfinite(lon)) {
            constexpr double kPi = 3.14159265358979323846;
            const double la1 = h->second.first * kPi / 180, lo1 = h->second.second * kPi / 180;
            const double la2 = lat * kPi / 180, lo2 = lon * kPi / 180;
            const double s = std::sin((la2 - la1) / 2) * std::sin((la2 - la1) / 2) +
                             std::cos(la1) * std::cos(la2) * std::sin((lo2 - lo1) / 2) * std::sin((lo2 - lo1) / 2);
            const double km = 2 * 6371.0 * std::asin(std::sqrt(s));
            for (auto it = types.begin(); it != types.end();) {
                const double r = typicalRangeKm(it->first);
                it = (r > 0.0 && r < km) ? types.erase(it) : std::next(it);
            }
        }
    }
    for (auto it = t.types.begin(); it != t.types.end();)
        it = it->second.empty() ? t.types.erase(it) : std::next(it);
    // No cargo mix from adsb.lol: the article's cargo airlines, with the
    // freighter types adsb.lol saw each fly anywhere.
    if (t.cargoAirlines.empty() && !wiki->cargoAirlines.empty()) {
        for (const auto& [a, w] : wiki->cargoAirlines) {
            auto f = fl.cargo.find(a);
            if (f == fl.cargo.end()) continue;
            t.cargoAirlines[a] = w;
            t.cargoTypes[a]    = f->second;
        }
        normalize(t.cargoAirlines);
        if (!t.cargoAirlines.empty()) t.cargoDaily = wiki->cargoDailyEst;
    }
    if (wikiShare) *wikiShare = 1.0f - fromAdsb;
    return t;
}

} // namespace WikiFallback
