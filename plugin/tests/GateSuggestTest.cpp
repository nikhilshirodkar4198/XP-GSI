// Gate window autofill ranking (arrival gate reservation, part 2).
#include <doctest/doctest.h>

#include "engine/GateSuggest.h"
#include "engine/Reservations.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using GateSuggest::Candidate;

static Candidate c(const char* name, char size, double dist) {
    return Candidate{Reservations::normalizeStand(name), size, dist};
}

static std::vector<std::string> names(const std::vector<std::size_t>& order, const std::vector<Candidate>& s) {
    std::vector<std::string> out;
    for (std::size_t i : order) out.push_back(s[i].norm);
    return out;
}

TEST_SUITE("GateSuggest") {

TEST_CASE("names starting with the text come before names containing it") {
    const std::vector<Candidate> s{c("A12", 'C', 100), c("B12", 'C', 50), c("B120", 'C', 900),
                                   c("C1", 'C', 10), c("Gate B1", 'C', 400)};
    // "B1": prefix B12, B120, B1 (Gate B1 → "B1"); contains: none. A12 contains "12", not "B1".
    auto order = GateSuggest::rank("b1", s, 'C');
    CHECK(names(order, s) == std::vector<std::string>{"B12", "B1", "B120"});
    // "12": prefix none... A12 and B12 and B120 contain it.
    order = GateSuggest::rank("12", s, 'C');
    CHECK(names(order, s) == std::vector<std::string>{"B12", "A12", "B120"});
}

TEST_CASE("within a group: stands that fit first, then by distance") {
    const std::vector<Candidate> s{c("D1", 'B', 10),    // too small for a C, nearest
                                   c("D2", 'E', 300),
                                   c("D3", 'C', 200),
                                   c("D4", '?', 250)};  // unknown size counts as fitting
    auto order = GateSuggest::rank("D", s, 'C');
    CHECK(names(order, s) == std::vector<std::string>{"D3", "D4", "D2", "D1"});
    // An unknown user size: everything fits, pure distance.
    order = GateSuggest::rank("D", s, '?');
    CHECK(names(order, s) == std::vector<std::string>{"D1", "D3", "D4", "D2"});
}

TEST_CASE("an empty box lists every stand") {
    const std::vector<Candidate> s{c("X9", 'C', 30), c("A1", 'C', 20), c("Stand 5", 'C', 10)};
    CHECK(GateSuggest::rank("", s, 'C').size() == 3);
    CHECK(names(GateSuggest::rank("  ", s, 'C'), s) == std::vector<std::string>{"5", "A1", "X9"});
}

TEST_CASE("normalized matching: separators and scenery prefixes are ignored") {
    const std::vector<Candidate> s{c("Gate B-12", 'C', 10), c("STAND B 14", 'C', 20)};
    CHECK(GateSuggest::rank("b-1", s, 'C').size() == 2);
    CHECK(GateSuggest::rank("gate", s, 'C').empty());   // the prefix is stripped from names
}

TEST_CASE("fits and formatting") {
    CHECK(GateSuggest::fits('E', 'C'));
    CHECK(GateSuggest::fits('C', 'C'));
    CHECK_FALSE(GateSuggest::fits('B', 'C'));
    CHECK(GateSuggest::fits('?', 'F'));
    CHECK(GateSuggest::formatDistance(850.4) == "850 m");
    CHECK(GateSuggest::formatDistance(1852.0 * 2.44) == "2.4 nm");
    CHECK(std::string(GateSuggest::statusName(GateSuggest::Status::Reserved)) == "reserved");
}

TEST_CASE("400 stands: a re-rank per keystroke stays well under a millisecond") {
    std::vector<Candidate> s;
    const char codes[] = {'B', 'C', 'C', 'D', 'E', 'F'};
    for (int i = 0; i < 400; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "%c%d", "ABCDEFGH"[i % 8], i);
        s.push_back(c(name, codes[i % 6], (i * 37) % 2500));
    }
    const char* typed[] = {"", "B", "B1", "B12", "1", "12", "C", "H3"};
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t total = 0;
    const int rounds = 50;
    for (int r = 0; r < rounds; ++r)
        for (const char* q : typed) total += GateSuggest::rank(q, s, 'C').size();
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() /
                      (rounds * 8);
    MESSAGE("average re-rank of 400 stands: " << us << " us");
    CHECK(total > 0);
    CHECK(us < 1000.0);
}

} // TEST_SUITE
