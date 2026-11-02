#pragma once
// GateSuggest — the autofill list of the gate window (arrival gate
// reservation, part 2). Names are normalized with the part 1 rules
// (Reservations::normalizeEntry / normalizeStand); here partial matches are
// allowed: names starting with the typed text first, then names containing
// it. Within each group: stands that fit the user's aircraft first, then by
// distance from the user, then by name. An empty query lists every stand.
//
// Pure logic, no XPLM calls.

#include <cstddef>
#include <string>
#include <vector>

namespace GateSuggest {

enum class Status { Free, Static, Traffic, Reserved };
const char* statusName(Status s);   // "free", "static parked", "live traffic", "reserved"

// A stand of size code `standSize` takes an aircraft of `userSize` (A–F).
// Unknown codes on either side count as fitting.
bool fits(char standSize, char userSize);

struct Candidate {
    std::string norm;     // Reservations::normalizeStand(name), prepared once per airport
    char        size = '?';
    double      distM = 0;
};

// Stand indices in display order.
std::vector<std::size_t> rank(const std::string& query, const std::vector<Candidate>& stands,
                              char userSize);

// "850 m" below one nautical mile, else "2.4 nm".
std::string formatDistance(double metres);

} // namespace GateSuggest
