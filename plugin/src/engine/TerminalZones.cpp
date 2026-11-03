#include "engine/TerminalZones.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace TerminalZones {

namespace {

constexpr double kPi = 3.14159265358979323846;

void add(Mix& m, const ParkCluster& c) {
    m.n += c.n;
    for (const auto& [a, n] : c.airlines) m.airlines[a] += n;
    for (const auto& [a, byType] : c.types)
        for (const auto& [t, n] : byType) m.types[a][t] += n;
}

int findRoot(std::vector<int>& parent, int i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

} // namespace

Zones Zones::build(const AirportTable& table, const std::vector<Stand>& stands) {
    Zones z;
    if (table.clusters.empty()) return z;
    z.refLat_ = table.clusters.front().lat;
    z.refLon_ = table.clusters.front().lon;
    const double cosLat = std::cos(z.refLat_ * kPi / 180.0);
    auto toXY = [&](double lat, double lon, double& x, double& y) {
        x = (lon - z.refLon_) * 111320.0 * cosLat;
        y = (lat - z.refLat_) * 110574.0;
    };

    if (!table.zones.empty() && !table.anchors.empty()) {
        z.fromOsm_ = true;
        z.reachM_  = kAnchorMaxM;
        std::map<std::string, int> groupIndex;
        for (const auto& tz : table.zones) {
            z.names_.push_back(tz.name);
            const std::string g = tz.group.empty() ? tz.name : tz.group;
            auto it = groupIndex.emplace(g, static_cast<int>(groupIndex.size())).first;
            z.groupOf_.push_back(it->second);
        }
        for (const auto& a : table.anchors) {
            Anchor an{0, 0, a.zone};
            toXY(a.lat, a.lon, an.x, an.y);
            z.anchors_.push_back(an);
        }
    } else {
        // Groups of neighbouring stands (single linkage).
        const int n = static_cast<int>(stands.size());
        if (n == 0) return z;
        std::vector<double> xs(n), ys(n);
        for (int i = 0; i < n; ++i) toXY(stands[i].lat, stands[i].lon, xs[i], ys[i]);
        std::vector<int> parent(n);
        std::iota(parent.begin(), parent.end(), 0);
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (std::hypot(xs[i] - xs[j], ys[i] - ys[j]) < kStandLinkM)
                    parent[findRoot(parent, i)] = findRoot(parent, j);
        std::map<int, int> zoneOfRoot;
        for (int i = 0; i < n; ++i) {
            const int r = findRoot(parent, i);
            auto it = zoneOfRoot.find(r);
            if (it == zoneOfRoot.end()) {
                it = zoneOfRoot.emplace(r, static_cast<int>(z.names_.size())).first;
                z.names_.push_back("stand group " + std::to_string(it->second + 1));
                z.groupOf_.push_back(it->second);
            }
            z.anchors_.push_back({xs[i], ys[i], it->second});
        }
        z.reachM_ = kClusterToStandM;
        if (z.names_.size() < 2) return {};   // one group says nothing about terminals
    }

    const std::size_t groups = z.groupOf_.empty() ? 0
        : static_cast<std::size_t>(*std::max_element(z.groupOf_.begin(), z.groupOf_.end())) + 1;
    z.zoneMix_.assign(z.names_.size(), Mix{});
    z.groupMix_.assign(groups, Mix{});
    for (const auto& c : table.clusters) {
        const int zone = z.zoneOf(c.lat, c.lon);
        z.clusterZone_.push_back(zone);
        if (zone < 0) continue;
        const int g = z.groupOf_[zone];
        add(z.zoneMix_[zone], c);
        add(z.groupMix_[g], c);
        for (const auto& [a, n] : c.airlines) {
            auto& v = z.airlineByGroup_[a];
            v.resize(groups, 0.0f);
            v[g] += n;
        }
    }
    return z;
}

int Zones::zoneOf(double lat, double lon) const {
    if (anchors_.empty()) return -1;
    const double cosLat = std::cos(refLat_ * kPi / 180.0);
    const double x = (lon - refLon_) * 111320.0 * cosLat;
    const double y = (lat - refLat_) * 110574.0;
    int    best  = -1;
    double bestD = reachM_ * reachM_;
    for (const auto& a : anchors_) {
        const double d = (a.x - x) * (a.x - x) + (a.y - y) * (a.y - y);
        if (d < bestD) { bestD = d; best = a.zone; }
    }
    return best;
}

const Mix* Zones::mixFor(int zone) const {
    if (zone < 0 || zone >= static_cast<int>(zoneMix_.size())) return nullptr;
    if (zoneMix_[zone].n >= kMinZoneEvidence) return &zoneMix_[zone];
    const Mix& g = groupMix_[groupOf_[zone]];
    return g.n >= kMinZoneEvidence ? &g : nullptr;
}

bool Zones::basedElsewhere(const std::string& airline, int zone) const {
    if (zone < 0 || zone >= static_cast<int>(groupOf_.size())) return false;
    auto it = airlineByGroup_.find(airline);
    if (it == airlineByGroup_.end()) return false;
    const float total = std::accumulate(it->second.begin(), it->second.end(), 0.0f);
    if (total < kMinAirlineStays) return false;
    const float here = it->second[groupOf_[zone]];
    return here < kElsewhereShare * total;
}

bool Zones::visitorsWelcome(int zone, const std::map<std::string, float>& visitors) const {
    if (visitors.empty() || zone < 0 || zone >= static_cast<int>(groupOf_.size())) return true;
    // Visitors' share of the group's airline weights.
    auto share = [&](const Mix& m) {
        float v = 0.0f, all = 0.0f;
        for (const auto& [a, n] : m.airlines) {
            all += n;
            if (visitors.count(a)) v += n;
        }
        return all > 0.0f ? v / all : 0.0f;
    };
    bool any = false;
    for (const auto& g : groupMix_)
        if (g.n >= kMinZoneEvidence && share(g) >= kVisitorZoneShare) { any = true; break; }
    if (!any) return true;
    // A group with too little data to say is not judged (KMIA's J concourse).
    const Mix& mine = groupMix_[groupOf_[zone]];
    return mine.n < kMinZoneEvidence || share(mine) >= kVisitorZoneShare;
}

} // namespace TerminalZones
