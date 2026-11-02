#include "engine/Planner.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cmath>
#include <numeric>
#include <random>

#include "csl/ModelSelector.h"

namespace Planner {

namespace {

// splitmix64 finalizer: decorrelates the position hash from the session seed.
std::uint32_t mix(std::uint32_t a, std::uint32_t b) {
    std::uint64_t z = (static_cast<std::uint64_t>(a) << 32 | b) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<std::uint32_t>(z ^ (z >> 31));
}

} // namespace

std::uint32_t newSessionSeed() {
    std::random_device rd;
    const auto t = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uint32_t s = mix(rd(), static_cast<std::uint32_t>(t ^ (t >> 32)));
    return s ? s : 1u;   // 0 means "random" in settings; never hand it out
}

std::uint32_t standSeed(const Stand& stand, std::uint32_t sessionSeed) {
    return mix(ModelSelector::positionSeed(stand), sessionSeed);
}

bool litAtNight(std::uint32_t standSeed) {
    // Own stream (remixed), so it does not correlate with model choice or fill order.
    return mix(standSeed, 0x5A17C0DEu) < static_cast<std::uint32_t>(kLitShare * 4294967296.0);
}

bool logoAtNight(std::uint32_t standSeed) {
    return mix(standSeed, 0x10C0F00Du) < static_cast<std::uint32_t>(kLogoShareOfLit * 4294967296.0);
}

bool isRemote(const Stand& stand) { return stand.type == StandType::TieDown; }

double remoteWeight(std::size_t gates, std::size_t remote) {
    if (gates + remote == 0) return kRemoteWeight;
    const double share = static_cast<double>(gates) / static_cast<double>(gates + remote);
    const double t     = std::clamp((kGateShareFull - share) / kGateShareFull, 0.0, 1.0);
    return kRemoteWeight + (1.0 - kRemoteWeight) * t;
}

double usageWeight(int stays, int busy) {
    if (busy <= 0) return 1.0;
    return std::clamp(std::sqrt(static_cast<double>(std::max(stays, 0)) / busy), kMinUsageWeight, 1.0);
}

std::vector<std::size_t> rankStands(const std::vector<Stand>& stands, std::uint32_t sessionSeed,
                                    const std::vector<int>* paxStays) {
    std::vector<std::size_t> idx(stands.size());
    std::iota(idx.begin(), idx.end(), 0);
    const auto remote = static_cast<std::size_t>(std::count_if(stands.begin(), stands.end(), isRemote));
    const double w = remoteWeight(stands.size() - remote, remote);
    // The stays of a busy stand, if the coverage is good enough to judge use.
    int busy = 0;
    if (paxStays && paxStays->size() == stands.size() && !stands.empty()) {
        std::vector<int> used;
        for (int n : *paxStays) if (n > 0) used.push_back(n);
        if (static_cast<double>(used.size()) >= kMinCoveredShare * static_cast<double>(stands.size())) {
            std::sort(used.begin(), used.end());
            busy = used[static_cast<std::size_t>(kBusyQuantile * static_cast<double>(used.size() - 1))];
        }
    }
    std::vector<double> rank(stands.size());
    for (std::size_t i = 0; i < stands.size(); ++i) {
        const double u = (static_cast<double>(mix(standSeed(stands[i], sessionSeed), 0xA5A5A5A5u)) + 0.5) /
                         4294967296.0;                                   // (0, 1)
        const double use = busy > 0 ? usageWeight((*paxStays)[i], busy) : 1.0;
        rank[i] = -std::log(u) / ((isRemote(stands[i]) ? w : 1.0) * use);
    }
    // Ties (identical positions) fall back to index order for determinism.
    std::stable_sort(idx.begin(), idx.end(),
                     [&](std::size_t a, std::size_t b) { return rank[a] < rank[b]; });
    return idx;
}

std::vector<std::size_t> chooseStands(const std::vector<Stand>& stands, std::size_t target,
                                      std::uint32_t sessionSeed) {
    auto idx = rankStands(stands, sessionSeed);
    idx.resize(std::min(target, idx.size()));
    std::sort(idx.begin(), idx.end());
    return idx;
}

} // namespace Planner
