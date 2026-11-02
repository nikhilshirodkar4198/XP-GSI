#include "engine/GateOccupancy.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "engine/StandLayout.h"

namespace GateOccupancy {

float depthBehindM(char widthCode) {
    switch (widthCode) {
        case 'A': return 20.0f;
        case 'B': return 30.0f;
        case 'C': return 40.0f;
        case 'D': return 50.0f;
        case 'E': return 60.0f;
        case 'F': return 70.0f;
        default:  return 40.0f;
    }
}

Relative relative(const Stand& stand, const Traffic& t) {
    float dh = std::fmod(std::fabs(t.headingDeg - stand.headingDeg), 360.0f);
    if (dh > 180.0f) dh = 360.0f - dh;

    double east = 0, north = 0;   // aircraft relative to the stand point
    StandLayout::toLocal(t.lat, t.lon, stand.lat, stand.lon, east, north);
    const double h = stand.headingDeg * 3.14159265358979323846 / 180.0;
    return Relative{east * std::sin(h) + north * std::cos(h),    // + = ahead of the nose
                    east * std::cos(h) - north * std::sin(h), dh};
}

bool occupies(const Stand& stand, const Traffic& t) {
    if (!t.onGround || t.speedMs > kMaxSpeedMs) return false;
    const Relative r = relative(stand, t);
    return r.headingDiff <= kMaxHeadingDiff && r.along <= kAheadM &&
           r.along >= -depthBehindM(stand.widthCode) && std::fabs(r.across) <= kHalfWidthM;
}

bool approaching(const Stand& stand, const Traffic& t) {
    if (!t.onGround || t.speedMs > kApproachMaxSpeedMs) return false;
    const Relative r = relative(stand, t);
    const double depth = depthBehindM(stand.widthCode);
    return r.headingDiff <= kApproachHeadingDiff && std::fabs(r.across) <= kApproachHalfWidthM &&
           r.along < kAheadM && r.along >= -(depth + kApproachExtraM);
}

std::string gateGroup(const std::string& name) {
    std::string k;
    for (char c : name) k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
    size_t n = k.size();
    while (n > 0 && std::isalpha(static_cast<unsigned char>(k[n - 1]))) --n;
    if (n == k.size()) return k;                           // ends in a digit already
    size_t m = n;
    if (m > 0 && k[m - 1] == ' ') --m;                     // "305 L"
    if (m > 0 && std::isdigit(static_cast<unsigned char>(k[m - 1]))) return k.substr(0, m);
    return k;                                              // no number before the letters
}

StandLayout::Shape trafficShape(char code) {
    // Typical wingspan / length per ICAO code (A320 ~36 × 38 m, B77W ~65 × 74 m).
    double span = 36.0, length = 40.0;
    switch (code) {
        case 'A': span = 11.0; length =  9.0; break;
        case 'B': span = 20.0; length = 20.0; break;
        case 'C': span = 36.0; length = 40.0; break;
        case 'D': span = 48.0; length = 50.0; break;
        case 'E': span = 62.0; length = 66.0; break;
        case 'F': span = 80.0; length = 73.0; break;
        default:  break;
    }
    StandLayout::Shape s;
    s.fMin = -0.5 * length; s.fMax = 0.5 * length;
    s.halfWidth = std::min(std::max(0.055 * length, 1.5), 3.5);
    s.hasWing  = true;
    s.wingFMin = -0.15 * length; s.wingFMax = 0.05 * length;
    s.wingRMin = -0.5 * span;    s.wingRMax = 0.5 * span;
    s.hasTail  = true;
    s.tailFMin = s.fMin; s.tailFMax = s.fMin + StandLayout::kTailLengthFraction * length;
    s.tailHalfSpan = StandLayout::kTailSpanFraction * 0.5 * span;
    return s;
}

bool touches(const Stand& stand, const StandLayout::Shape& staticShape, const Traffic& t) {
    if (!t.onGround || t.speedMs > kMaxSpeedMs) return false;
    double east = 0, north = 0;   // aircraft relative to the stand point
    StandLayout::toLocal(t.lat, t.lon, stand.lat, stand.lon, east, north);
    if (std::hypot(east, north) > 150.0) return false;   // cannot reach
    const auto mine  = StandLayout::place(staticShape, 0.0, 0.0, stand.headingDeg);
    const auto other = StandLayout::place(trafficShape(t.sizeCode), east, north, t.headingDeg);
    return StandLayout::overlaps(mine, other, kTouchClearanceM);
}

void Tracker::reset(std::size_t standCount) {
    m_claimed.assign(standCount, false);
    m_since.assign(standCount, -1.0);
    m_group.clear();
    m_shapes.clear();
}

std::vector<std::size_t> Tracker::update(const std::vector<Stand>& stands,
                                         const std::vector<Traffic>& traffic, double now) {
    if (m_claimed.size() != stands.size()) reset(stands.size());
    if (m_group.size() != stands.size()) {
        m_group.clear();
        m_cosLat.clear();
        for (const auto& s : stands) {
            m_group.push_back(gateGroup(s.id));
            m_cosLat.push_back(std::cos(s.lat * 3.14159265358979323846 / 180.0));
        }
    }
    std::vector<std::size_t> out;
    auto claim = [&](std::size_t i) {
        m_claimed[i] = true;
        out.push_back(i);
        if (m_group[i].empty()) return;
        for (std::size_t j = 0; j < stands.size(); ++j) {   // the gate's other positions
            if (m_claimed[j] || m_group[j] != m_group[i]) continue;
            double e = 0, n = 0;
            StandLayout::toLocal(stands[j].lat, stands[j].lon, stands[i].lat, stands[i].lon, e, n);
            if (std::hypot(e, n) > kSameGateM) continue;
            m_claimed[j] = true;
            out.push_back(j);
        }
    };
    for (std::size_t i = 0; i < stands.size(); ++i) {
        if (m_claimed[i]) continue;
        bool parked = false, moving = false, touchOnly = true;
        const bool haveShape = i < m_shapes.size();
        for (const auto& t : traffic) {
            // Cheap reject first: no test reaches beyond kReachM of the stand
            // (touches() stops at 150 m), so skip the trig for far traffic.
            if (!t.onGround || t.speedMs > kApproachMaxSpeedMs ||
                std::fabs(t.lat - stands[i].lat) > kReachDegLat ||
                std::fabs(t.lon - stands[i].lon) * m_cosLat[i] > kReachDegLat) continue;
            const bool on = occupies(stands[i], t) || approaching(stands[i], t);
            if (!on && !(haveShape && touches(stands[i], m_shapes[i], t))) continue;
            if (on) touchOnly = false;
            if (t.speedMs <= kParkedSpeedMs) parked = true;
            else moving = true;
        }
        if (!parked && !moving) { m_since[i] = -1.0; continue; }
        if (m_since[i] < 0) m_since[i] = now;
        if (parked || now - m_since[i] >= (touchOnly ? kTouchDwellSec : kDwellSec)) claim(i);
    }
    return out;
}

} // namespace GateOccupancy
