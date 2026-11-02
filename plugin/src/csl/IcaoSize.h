#pragma once
// IcaoSize: map ICAO aircraft type code → apt.dat size code ('A'–'F').
//
// Size codes follow ICAO Annex 14 Vol I, Table 1-1 (reference code letter),
// which in X-Plane's apt.dat 1100 spec is based on wingspan:
//   A  < 15 m      (light piston / turboprop GA)
//   B  15–24 m     (regional turboprop, light jet)
//   C  24–36 m     (single-aisle narrowbody)
//   D  36–52 m     (medium widebody / large narrowbody)
//   E  52–65 m     (large widebody)
//   F  65–80 m     (super heavy)
// Source: developer.x-plane.com apt.dat 1100 spec (row 1301 field [2]).

#include <string>
#include <unordered_map>

namespace IcaoSize {

// Returns the size code for a known ICAO type, or '?' if unknown.
inline char lookup(const std::string& icaoType) {
    // Verified wingspans from manufacturer data / Jane's All the World's Aircraft.
    static const std::unordered_map<std::string, char> kTable = {

        // ── A  < 15 m ──────────────────────────────────────────────────────
        {"C172", 'A'}, {"C182", 'A'}, {"C152", 'A'}, {"C150", 'A'},
        {"PA28", 'A'}, {"PA32", 'A'}, {"PA44", 'A'}, {"BE35", 'A'},
        {"BE36", 'A'}, {"BE58", 'A'}, {"C206", 'A'}, {"C208", 'A'},
        {"TBM9", 'A'}, {"TBM8", 'A'}, {"TBM7", 'A'}, {"PC6T", 'A'},
        {"SR20", 'A'}, {"SR22", 'A'}, {"DR40", 'A'}, {"P28A", 'A'},

        // ── B  15–24 m ─────────────────────────────────────────────────────
        {"DH8A", 'B'}, {"DH8B", 'B'}, {"DH8C", 'B'}, {"DH8D", 'B'},
        {"AT43", 'B'}, {"AT44", 'B'}, {"AT45", 'B'},
        {"AT72", 'B'}, {"AT73", 'B'}, {"AT75", 'B'}, {"AT76", 'B'},
        {"SF34", 'B'}, {"J328", 'B'}, {"PC12", 'B'}, {"F50",  'B'},
        {"F27",  'B'}, {"S340", 'B'}, {"E110", 'B'},
        {"CRJ1", 'B'}, {"CRJ2", 'B'},   // CRJ-100/200: 21.2 m → B
        {"SW4",  'B'},

        // ── C  24–36 m ─────────────────────────────────────────────────────
        // Boeing narrowbody
        {"B721", 'C'}, {"B722", 'C'},
        {"B731", 'C'}, {"B732", 'C'}, {"B733", 'C'}, {"B734", 'C'},
        {"B735", 'C'}, {"B736", 'C'}, {"B737", 'C'}, {"B738", 'C'},
        {"B739", 'C'},
        {"B37M", 'C'}, {"B38M", 'C'}, {"B39M", 'C'}, {"B3XM", 'C'}, // 737 MAX
        {"B712", 'C'}, {"B717", 'C'},
        // Airbus narrowbody
        {"A318", 'C'}, {"A319", 'C'}, {"A320", 'C'}, {"A321", 'C'},
        {"A19N", 'C'}, {"A20N", 'C'}, {"A21N", 'C'},                 // neo
        // Douglas
        {"DC91", 'C'}, {"DC92", 'C'}, {"DC93", 'C'}, {"DC94", 'C'},
        {"DC95", 'C'}, {"MD81", 'C'}, {"MD82", 'C'}, {"MD83", 'C'},
        {"MD87", 'C'}, {"MD88", 'C'}, {"MD90", 'C'},
        // Regional jets (larger)
        {"CRJ7", 'C'}, {"CRJ9", 'C'}, {"CRJX", 'C'},
        // Airbus A220 (ex-Bombardier CSeries): 35.1 m
        {"BCS1", 'C'}, {"BCS3", 'C'},
        {"E170", 'C'}, {"E175", 'C'}, {"E190", 'C'}, {"E195", 'C'},
        {"E75L", 'C'}, {"E75S", 'C'},                                 // E2
        // Fokker / misc
        {"F100", 'C'}, {"F70",  'C'}, {"F28",  'C'},
        // Tupolev / Soviet
        {"T134", 'C'}, {"T154", 'C'}, {"T204", 'C'}, {"T214", 'C'},

        // ── D  36–52 m ─────────────────────────────────────────────────────
        {"B752", 'D'}, {"B753", 'D'},        // B757: 38.1 m
        {"B762", 'D'}, {"B763", 'D'},        // B767-200/300: 47.6 m
        {"B764", 'D'},                        // B767-400: 51.9 m (< 52 m → D)
        {"A306", 'D'}, {"A30B", 'D'},        // A300: 44.8 m
        {"A310", 'D'},                        // A310: 43.9 m
        {"DC10", 'D'},                        // DC-10: 47.3 m
        {"IL62", 'D'}, {"IL86", 'D'},
        {"L101", 'D'},                        // L-1011: 47.3 m

        // ── E  52–65 m ─────────────────────────────────────────────────────
        {"B741", 'E'}, {"B742", 'E'}, {"B743", 'E'}, {"B744", 'E'},  // 747-100–400: 64.4 m
        {"B74S", 'E'}, {"B74D", 'E'},
        {"B772", 'E'}, {"B773", 'E'}, {"B77L", 'E'}, {"B77W", 'E'},  // B777-200/300: 60.9 m
        {"B788", 'E'}, {"B789", 'E'}, {"B78X", 'E'},                  // B787: 60.1 m
        {"A332", 'E'}, {"A333", 'E'}, {"A338", 'E'}, {"A339", 'E'},  // A330: 60.3 m
        {"A342", 'E'}, {"A343", 'E'}, {"A345", 'E'}, {"A346", 'E'},  // A340: up to 63.5 m
        {"A359", 'E'}, {"A35K", 'E'},                                  // A350: 64.75 m
        {"MD11", 'E'},                                                  // MD-11: 51.7 m → borderline D/E
        {"IL96", 'E'},

        // ── F  65–80 m ─────────────────────────────────────────────────────
        {"B748", 'F'}, {"B74F", 'F'},   // 747-8: 68.5 m
        {"B779", 'F'},                   // 777-9: 71.8 m
        {"A388", 'F'},                   // A380: 79.75 m
        {"AN12", 'F'}, {"AN22", 'F'},
    };

    auto it = kTable.find(icaoType);
    return (it != kTable.end()) ? it->second : '?';
}

// True for types that belong at an airline passenger gate: a known size of
// B or larger, minus the few non-airliners the table holds (single-engine
// turboprop, military freighters). Light GA (size A), business jets and
// military types are not in the table (or are size A) and never qualify,
// so a Caravan or a Cessna can never be parked at an airline stand.
inline bool isAirliner(const std::string& icaoType) {
    static const std::unordered_map<std::string, bool> kNotAirliner = {
        {"PC12", true}, {"AN12", true}, {"AN22", true},
    };
    const char sz = lookup(icaoType);
    return sz >= 'B' && sz <= 'F' && kNotAirliner.count(icaoType) == 0;
}

// Shortest paved runway (metres) on which an airline would plausibly base or
// operate the type (an IndiGo A321 at Kolhapur, whose
// X-Plane runway is 1,368 m and which only sees ATRs). Deliberately lenient,
// so real short-field operations stay possible (A220 / E-jets at London City
// 1,508 m, 737-800s at John Wayne 1,737 m); it only rules out the clearly
// impossible.
inline double minRunwayM(const std::string& icaoType) {
    static const std::unordered_map<std::string, double> kOverride = {
        // Turboprops
        {"AT43", 1000}, {"AT45", 1000}, {"AT46", 1000}, {"AT72", 1000}, {"AT73", 1000}, {"AT75", 1000},
        {"AT76", 1000}, {"DH8A", 1000}, {"DH8B", 1000}, {"DH8C", 1000}, {"DH8D", 1000}, {"SF34", 1000},
        {"SB20", 1000}, {"JS31", 1000}, {"JS32", 1000}, {"JS41", 1000}, {"E120", 1000}, {"D328", 1000},
        {"F50", 1000}, {"DHC6", 600}, {"B190", 800}, {"L410", 700},
        // Regional jets, A220, small E-jets
        {"CRJ1", 1400}, {"CRJ2", 1400}, {"CRJ7", 1400}, {"CRJ9", 1400}, {"CRJX", 1400},
        {"E135", 1400}, {"E145", 1400}, {"E45X", 1400}, {"E170", 1400}, {"E175", 1400}, {"E75S", 1400},
        {"E75L", 1400}, {"E190", 1400}, {"E195", 1400}, {"E290", 1400}, {"E295", 1400},
        {"BCS1", 1400}, {"BCS3", 1400}, {"RJ70", 1400}, {"RJ85", 1400}, {"RJ1H", 1400},
        {"B461", 1400}, {"B462", 1400}, {"B463", 1400}, {"F70", 1400}, {"F100", 1400},
        // Long narrowbodies
        {"A321", 1800}, {"A21N", 1800}, {"B739", 1800}, {"B39M", 1800}, {"B3XM", 1800}, {"B753", 2000},
    };
    if (auto it = kOverride.find(icaoType); it != kOverride.end()) return it->second;
    switch (lookup(icaoType)) {
        case 'A': return 600;
        case 'B': return 1000;
        case 'C': return 1600;
        case 'D': return 2000;
        case 'E': return 2300;
        case 'F': return 2700;
        default:  return 1600;
    }
}

} // namespace IcaoSize
