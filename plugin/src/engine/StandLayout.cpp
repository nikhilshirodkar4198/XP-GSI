#include "engine/StandLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace StandLayout {

namespace {
constexpr double kPi = 3.14159265358979323846;

Rect rectInStandFrame(double fMin, double fMax, double rMin, double rMax,
                      double east, double north, double headingDeg) {
    const double h  = headingDeg * kPi / 180.0;
    const double fx = std::sin(h), fy = std::cos(h);     // forward (east, north)
    const double rx = std::cos(h), ry = -std::sin(h);    // right
    const double fc = 0.5 * (fMin + fMax), rc = 0.5 * (rMin + rMax);
    Rect r;
    r.cx = east  + fc * fx + rc * rx;
    r.cy = north + fc * fy + rc * ry;
    r.ux = fx; r.uy = fy;
    r.halfLen = 0.5 * (fMax - fMin);
    r.halfWid = 0.5 * (rMax - rMin);
    return r;
}

bool rectsOverlap(const Rect& a, const Rect& b, double clearance) {
    const double axes[4][2] = {{a.ux, a.uy}, {-a.uy, a.ux}, {b.ux, b.uy}, {-b.uy, b.ux}};
    const double dx = b.cx - a.cx, dy = b.cy - a.cy;
    for (const auto& ax : axes) {
        auto radius = [&](const Rect& r) {
            return r.halfLen * std::fabs(r.ux * ax[0] + r.uy * ax[1]) +
                   r.halfWid * std::fabs(-r.uy * ax[0] + r.ux * ax[1]);
        };
        if (std::fabs(dx * ax[0] + dy * ax[1]) >= radius(a) + radius(b) + clearance)
            return false;   // separating axis found
    }
    return true;
}
} // namespace

Shape shapeFor(const ObjGeometry::Extents& e, double d) {
    Shape s;
    if (!e.valid) {
        s.fMin = -36.0; s.fMax = 4.0; s.halfWidth = 2.0;
        s.hasWing = true; s.wingFMin = -22.0; s.wingFMax = -14.0; s.wingRMin = -17.9; s.wingRMax = 17.9;
        return s;
    }
    // OBJ vertex (x, z) lands at f = -z - d, r = x (origin moved back by d).
    s.fMin = -e.maxZ - d;
    s.fMax = -e.minZ - d;
    const double length = e.maxZ - e.minZ;
    s.halfWidth = std::min(std::max(0.055 * length, 1.5), 3.5);   // fuselage
    if (e.wingFound) {
        s.hasWing  = true;
        s.wingFMin = -e.wingMaxZ - d;
        s.wingFMax = -e.wingMinZ - d;
        s.wingRMin = e.minX;
        s.wingRMax = e.maxX;
    }
    if (e.innerWingFound) {
        s.hasInner      = true;
        s.innerFMin     = -e.innerWingMaxZ - d;
        s.innerFMax     = -e.innerWingMinZ - d;
        s.innerHalfSpan = ObjGeometry::kOuterWingFraction * std::max(std::fabs(e.minX), std::fabs(e.maxX));
    }
    s.hasTail      = true;
    s.tailFMin     = s.fMin;
    s.tailFMax     = s.fMin + kTailLengthFraction * length;
    s.tailHalfSpan = kTailSpanFraction * std::max(std::fabs(e.minX), std::fabs(e.maxX));
    return s;
}

Shape stubShape() {
    Shape s;
    s.fMin = -10.0; s.fMax = 0.0; s.halfWidth = 2.0;
    return s;
}

Footprint place(const Shape& s, double east, double north, double headingDeg) {
    Footprint fp;
    fp.fuselage = rectInStandFrame(s.fMin, s.fMax, -s.halfWidth, s.halfWidth, east, north, headingDeg);
    if (s.hasWing) {
        fp.hasWing = true;
        fp.wing = rectInStandFrame(s.wingFMin, s.wingFMax, s.wingRMin, s.wingRMax, east, north, headingDeg);
    }
    if (s.hasInner) {
        fp.hasInner = true;
        fp.inner = rectInStandFrame(s.innerFMin, s.innerFMax, -s.innerHalfSpan, s.innerHalfSpan,
                                    east, north, headingDeg);
    }
    fp.spanM = s.hasWing ? s.wingRMax - s.wingRMin : 2.0 * s.halfWidth;
    if (s.hasTail) {
        fp.hasTail = true;
        fp.tail = rectInStandFrame(s.tailFMin, s.tailFMax, -s.tailHalfSpan, s.tailHalfSpan,
                                   east, north, headingDeg);
    }
    return fp;
}

double clearanceFor(double spanA, double spanB) {
    return std::max(spanA, spanB) >= kCodeDSpanM ? kClearanceLargeM : kClearanceSmallM;
}

bool overlaps(const Footprint& a, const Footprint& b, double clearance) {
    if (clearance < 0.0) clearance = clearanceFor(a.spanM, b.spanM);
    const Rect* ra[4] = {&a.fuselage, a.hasWing ? &a.wing : nullptr, a.hasInner ? &a.inner : nullptr,
                         a.hasTail ? &a.tail : nullptr};
    const Rect* rb[4] = {&b.fuselage, b.hasWing ? &b.wing : nullptr, b.hasInner ? &b.inner : nullptr,
                         b.hasTail ? &b.tail : nullptr};
    for (const Rect* x : ra)
        for (const Rect* y : rb)
            if (x && y && rectsOverlap(*x, *y, clearance)) return true;
    return false;
}

std::string gateKey(const std::string& name) {
    std::string k;
    for (char c : name) k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    // A trailing tag in brackets (Global Airports LTFM "A2L-(C)", "A2-(F)":
    // the size) and the separators before it go first, so the L / R is
    // seen (aircraft on A2L and A2R at once).
    auto trimSep = [&] {
        while (!k.empty() && (std::isspace(static_cast<unsigned char>(k.back())) || k.back() == '-' || k.back() == '_'))
            k.pop_back();
    };
    trimSep();
    while (!k.empty() && k.back() == ')') {
        const auto open = k.rfind('(');
        if (open == std::string::npos || open == 0) break;
        k.resize(open);
        trimSep();
    }
    const size_t n = k.size();
    if (n >= 2 && (k[n - 1] == 'L' || k[n - 1] == 'R' || k[n - 1] == 'C')) {
        size_t i = n - 1;
        if (i > 0 && k[i - 1] == ' ') --i;          // "305 L"
        if (i > 0 && std::isdigit(static_cast<unsigned char>(k[i - 1]))) k.resize(i);
    }
    return k;
}

std::vector<std::string> gateKeys(const std::vector<std::string>& names) {
    // A name used for several stands (Global Airports KSDF: 14 × "UPS Ramp
    // 7") names an apron, not a gate: each of those stands is its own gate.
    std::unordered_map<std::string, int> uses;
    std::vector<std::string> full;
    full.reserve(names.size());
    for (const auto& n : names) {
        std::string k;
        for (char c : n) k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
        ++uses[k];
        full.push_back(std::move(k));
    }
    // (A tagged name's plain form — "A2-(F)" → "A2" — is what the lettered
    // positions "A2L-(C)" / "A2R-(C)" are matched against below.)
    std::vector<std::string> keys;
    keys.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i)
        keys.push_back(uses[full[i]] > 1 ? full[i] + "#" + std::to_string(i) : gateKey(names[i]));
    std::unordered_set<std::string> plain(keys.begin(), keys.end());
    for (auto& k : keys) {
        const size_t n = k.size();
        if (n >= 2 && std::isalpha(static_cast<unsigned char>(k[n - 1])) &&
            std::isdigit(static_cast<unsigned char>(k[n - 2]))) {
            const std::string base = k.substr(0, n - 1);
            if (plain.count(base)) k = base;
        }
    }
    return keys;
}

std::vector<std::size_t> largestFirstPerGate(const std::vector<std::size_t>& order,
                                             const std::vector<std::string>& keys,
                                             const std::vector<char>& sizes) {
    std::unordered_map<std::string, std::vector<std::size_t>> members;
    for (std::size_t i : order) members[keys[i]].push_back(i);
    std::vector<std::size_t> out;
    out.reserve(order.size());
    for (std::size_t i : order) {
        auto it = members.find(keys[i]);
        if (it == members.end()) continue;   // gate already emitted
        auto& m = it->second;
        std::stable_sort(m.begin(), m.end(), [&](std::size_t a, std::size_t b) { return sizes[a] > sizes[b]; });
        out.insert(out.end(), m.begin(), m.end());
        members.erase(it);
    }
    return out;
}

void toLocal(double lat, double lon, double refLat, double refLon, double& east, double& north) {
    north = (lat - refLat) * 110574.0;
    east  = (lon - refLon) * 111320.0 * std::cos(refLat * kPi / 180.0);
}

Checker::Verdict Checker::check(const Footprint& fp, const std::string& gate,
                                double east, double north) const {
    for (const auto& it : m_items) {
        const double d = std::hypot(it.east - east, it.north - north);
        if ((!gate.empty() && it.gate == gate && d < kSameGateM) || d < kAlternateM)
            return Verdict::SameGate;
    }
    for (const auto& it : m_items)
        if (overlaps(fp, it.fp)) return Verdict::Overlap;
    return Verdict::Fits;
}

void Checker::add(const Footprint& fp, const std::string& gate, double east, double north) {
    m_items.push_back({fp, gate, east, north});
}

} // namespace StandLayout
