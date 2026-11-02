#pragma once
// Planner — how many stands to fill and which ones, plus the
// per-run variation seed.
//
// Variation: every
// placement — each new flight, airport or Start — gets a new seed, so the
// occupied gates and the airline/type/livery at each one differ every time.
// Within one placement the seed is fixed, so the density slider and conflict
// respawns don't reshuffle. All choices stay weighted by the data.
//
// Pure logic, no XPLM calls.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/StandState.h"

namespace Planner {

// A fresh, unpredictable 32-bit seed (std::random_device mixed with the clock).
std::uint32_t newSessionSeed();

// Seed for one stand in this run: its position hash mixed with the session
// seed. Independent of apt.dat stand ordering.
std::uint32_t standSeed(const Stand& stand, std::uint32_t sessionSeed);

// Whether a stand's static shows nav + logo lights at night: about one in
// four (kLitShare), from its stand seed, so the lit set changes every run.
constexpr double kLitShare = 0.25;
bool litAtNight(std::uint32_t standSeed);

// Of the lit statics, the share that also shows logo lights at night; the
// rest show nav lights only (different company SOPs).
constexpr double kLogoShareOfLit = 0.80;
bool logoAtNight(std::uint32_t standSeed);

// How many stands to fill: Density::target (eligible × the airport's
// passenger or cargo density, capped by max aircraft).

// Which stands to fill: indices of the `target` stands with the lowest
// per-run rank. Deterministic for (stand positions, sessionSeed); a different
// seed gives a different subset. Raising the target only adds stands (the
// chosen set for N is a prefix of the set for N+1), so density changes never
// reshuffle the aircraft already placed.
std::vector<std::size_t> chooseStands(const std::vector<Stand>& stands, std::size_t target,
                                      std::uint32_t sessionSeed);

// All stand indices in fill order (lowest rank first): chooseStands(n) is the
// first n of these. The renderer keeps this order and adds/removes from the
// end as the density slider moves.
//
// Remote stands (apt.dat "tie_down") are filled more sparsely than gates
// (at RJTT): the order is a weighted random permutation
// (Efraimidis–Spirakis, key = −ln u / w) with weight kRemoteWeight for a
// tie-down and 1 for any other stand, so at a given density a remote stand
// is about a third as likely to be taken as a gate. Other types ("misc")
// count as gates: some sceneries type their contact gates that way (KFAT).
// At airports where gates are the minority the remote stands are where the
// traffic is: below kGateShareFull of the stands being
// gates the weight rises linearly, to 1 when there are no gates at all.
constexpr double kRemoteWeight  = 0.35;
constexpr double kGateShareFull = 0.5;
bool   isRemote(const Stand& stand);
double remoteWeight(std::size_t gates, std::size_t remote);
// With `paxStays` (passenger stays parked at each stand, StandEvidence::
// paxStaysPerStand), a stand is also weighted by how much it is really used
// (too many aircraft on KSMF's remote stands, which the
// scenery types as gates): × sqrt(stays / kBusyStays-quantile stand), within
// [kMinUsageWeight, 1]. Only where ground coverage is good — at least
// kMinCoveredShare of the stands have any stays — else as without.
constexpr double kBusyQuantile    = 0.75;
constexpr double kMinUsageWeight  = 0.08;
constexpr double kMinCoveredShare = 0.5;
double usageWeight(int stays, int busyStays);
std::vector<std::size_t> rankStands(const std::vector<Stand>& stands, std::uint32_t sessionSeed,
                                    const std::vector<int>* paxStays = nullptr);

} // namespace Planner
