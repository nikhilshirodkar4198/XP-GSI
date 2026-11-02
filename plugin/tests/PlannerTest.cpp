#include <doctest/doctest.h>

#include "engine/Planner.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <vector>

static std::vector<Stand> gates(std::size_t n) {
    std::vector<Stand> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i].lat = 39.71 + 0.0003 * static_cast<double>(i);
        v[i].lon = -86.29;
    }
    return v;
}

TEST_SUITE("Planner") {

TEST_CASE("rankStands is a permutation; chooseStands(n) is its first n") {
    auto s = gates(30);
    auto rank = Planner::rankStands(s, 42);
    REQUIRE(rank.size() == 30);
    std::set<std::size_t> uniq(rank.begin(), rank.end());
    CHECK(uniq.size() == 30);

    std::vector<std::size_t> first12(rank.begin(), rank.begin() + 12);
    std::sort(first12.begin(), first12.end());
    CHECK(Planner::chooseStands(s, 12, 42) == first12);
}

TEST_CASE("same session seed → same stands; different seeds → different stands") {
    auto s = gates(140);
    auto a = Planner::chooseStands(s, 60, 111);
    auto b = Planner::chooseStands(s, 60, 111);
    CHECK(a == b);
    CHECK(a.size() == 60);

    // Ten runs: every layout differs from the others.
    std::set<std::vector<std::size_t>> layouts;
    for (std::uint32_t seed = 1; seed <= 10; ++seed)
        layouts.insert(Planner::chooseStands(s, 60, seed * 2654435761u));
    CHECK(layouts.size() == 10);
}

TEST_CASE("different seeds overlap partially, not completely or not at all") {
    // "not for all": two runs share some occupied gates, as random 60-of-140 picks do.
    auto s = gates(140);
    auto a = Planner::chooseStands(s, 60, 7);
    auto b = Planner::chooseStands(s, 60, 8);
    std::vector<std::size_t> common;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
    CHECK(common.size() > 5);
    CHECK(common.size() < 55);
}

TEST_CASE("raising the target only adds stands (no reshuffle on density change)") {
    auto s = gates(140);
    auto small = Planner::chooseStands(s, 30, 99);
    auto large = Planner::chooseStands(s, 45, 99);
    CHECK(std::includes(large.begin(), large.end(), small.begin(), small.end()));
}

TEST_CASE("choice does not depend on apt.dat stand order") {
    auto s = gates(50);
    auto r = s;
    std::reverse(r.begin(), r.end());
    auto a = Planner::chooseStands(s, 20, 5);
    auto b = Planner::chooseStands(r, 20, 5);
    std::set<double> latsA, latsB;
    for (auto i : a) latsA.insert(s[i].lat);
    for (auto i : b) latsB.insert(r[i].lat);
    CHECK(latsA == latsB);
}

TEST_CASE("stand seeds differ between sessions, match within one") {
    Stand st; st.lat = 39.7173; st.lon = -86.2944;
    CHECK(Planner::standSeed(st, 1) == Planner::standSeed(st, 1));
    CHECK(Planner::standSeed(st, 1) != Planner::standSeed(st, 2));
}

TEST_CASE("new session seeds are non-zero and vary") {
    std::set<std::uint32_t> seen;
    for (int i = 0; i < 20; ++i) {
        auto s = Planner::newSessionSeed();
        CHECK(s != 0u);
        seen.insert(s);
    }
    CHECK(seen.size() > 15);
}

} // TEST_SUITE
