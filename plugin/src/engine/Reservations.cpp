#include "engine/Reservations.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "engine/GateOccupancy.h"

namespace Reservations {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ── Gate names ────────────────────────────────────────────────────────────────

std::string normalizeEntry(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (std::isspace(c) || c == '-' || c == '.' || c == '_') continue;
        out += static_cast<char>(std::toupper(c));
    }
    return out;
}

std::string normalizeStand(const std::string& name) {
    std::string n = normalizeEntry(name);
    for (const char* prefix : {"PARKING", "STAND", "GATE", "STD", "GT"}) {
        const std::string p(prefix);
        if (n.size() > p.size() && n.compare(0, p.size(), p) == 0) return n.substr(p.size());
    }
    return n;
}

std::vector<std::size_t> match(const std::string& entry, const std::vector<std::string>& names) {
    std::vector<std::size_t> out;
    const std::string e = normalizeEntry(entry);
    if (e.empty()) return out;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (normalizeStand(names[i]) == e) out.push_back(i);
    if (!out.empty()) return out;
    // Else the part after the last GATE / STAND / PARKING word: "F15" for
    // "T2 Gate F15" (KORD).
    for (std::size_t i = 0; i < names.size(); ++i) {
        const std::string n = normalizeEntry(names[i]);
        for (const char* word : {"GATE", "STAND", "PARKING"}) {
            const auto at = n.rfind(word);
            if (at != std::string::npos && n.substr(at + std::strlen(word)) == e) {
                out.push_back(i);
                break;
            }
        }
    }
    return out;
}

// ── Armed window ──────────────────────────────────────────────────────────────

Event ArmState::update(const Sample& s) {
    if (!s.onGround) {
        if (m_airborneSince < 0) m_airborneSince = s.now;
        m_parkedSince = -1;
        if (m_armed && s.now - m_airborneSince >= kTakeoffSec) {
            m_armed = false;
            return Event::TookOff;
        }
        return Event::None;
    }

    // On the ground.
    const bool touchdown = m_airborneSince >= 0;
    const double airborne = touchdown ? s.now - m_airborneSince : 0.0;
    m_airborneSince = -1;
    if (!m_armed) {
        if (touchdown && airborne >= kMinAirborneSec) {
            m_armed = true;
            m_parkedSince = -1;
            return Event::Armed;
        }
        return Event::None;
    }

    if (!s.anyEngineRunning) {
        m_armed = false;
        return Event::EnginesOff;
    }
    if (s.groundSpeedMs < kParkedSpeedMs && s.parkBrake >= kParkBrakeSet) {
        if (m_parkedSince < 0) m_parkedSince = s.now;
        if (s.now - m_parkedSince >= kParkedSec) {
            m_armed = false;
            return Event::Parked;
        }
    } else {
        m_parkedSince = -1;
    }
    return Event::None;
}

void ArmState::reset() {
    m_armed = false;
    m_airborneSince = -1;
    m_parkedSince = -1;
}

const char* eventName(Event e) {
    switch (e) {
        case Event::Armed:      return "armed (touchdown)";
        case Event::Parked:     return "parked";
        case Event::EnginesOff: return "engines shut down";
        case Event::TookOff:    return "took off";
        default:                return "none";
    }
}

// ── Reservation set ───────────────────────────────────────────────────────────

std::size_t Set::add(const std::vector<std::size_t>& stands) {
    std::size_t n = 0;
    for (std::size_t s : stands) n += m_stands.insert(s).second ? 1 : 0;
    return n;
}

bool Set::release(std::size_t stand) { return m_stands.erase(stand) != 0; }

std::size_t Set::releaseAllExcept(std::size_t keep) {
    const bool had = contains(keep);
    const std::size_t n = m_stands.size() - (had ? 1 : 0);
    m_stands.clear();
    if (had) m_stands.insert(keep);
    return n;
}

std::size_t Set::releaseAll() {
    const std::size_t n = m_stands.size();
    m_stands.clear();
    return n;
}

bool cduGateEntry(const std::string& text, std::string& stand) {
    if (text.size() < 2 || std::toupper(static_cast<unsigned char>(text[0])) != 'G') return false;
    const auto first = text.find_first_not_of(" /", 1);
    if (first == std::string::npos) return false;
    stand = text.substr(first);
    return true;
}

StandLayout::Shape userShapeForSize(char sizeCode) {
    // The traffic outline is centred on the reference point; move it back so
    // the nose sits on the stand point, as a parked aircraft's does.
    StandLayout::Shape s = GateOccupancy::trafficShape(sizeCode);
    const double shift = -s.fMax;
    s.fMin += shift; s.fMax += shift;
    s.wingFMin += shift; s.wingFMax += shift;
    s.tailFMin += shift; s.tailFMax += shift;
    return s;
}

std::vector<bool> blockedPlanStands(const std::vector<Stand>& reserved,
                                    const std::vector<Stand>& plan,
                                    const std::vector<StandLayout::Shape>& planShapes,
                                    const StandLayout::Shape& userShape) {
    std::vector<bool> out(plan.size(), false);
    for (const Stand& r : reserved) {
        const std::string group = GateOccupancy::gateGroup(r.id);
        const auto mine = StandLayout::place(userShape, 0.0, 0.0, r.headingDeg);
        for (std::size_t j = 0; j < plan.size(); ++j) {
            if (out[j]) continue;
            double e = 0, n = 0;   // plan stand relative to the reserved one
            StandLayout::toLocal(plan[j].lat, plan[j].lon, r.lat, r.lon, e, n);
            const double d = std::hypot(e, n);
            if (d < 1.0 && plan[j].id == r.id) { out[j] = true; continue; }          // the stand itself
            if (!group.empty() && d <= GateOccupancy::kSameGateM &&
                GateOccupancy::gateGroup(plan[j].id) == group) { out[j] = true; continue; }
            if (j < planShapes.size() && d < 150.0 &&
                StandLayout::overlaps(mine, StandLayout::place(planShapes[j], e, n, plan[j].headingDeg),
                                      GateOccupancy::kTouchClearanceM))
                out[j] = true;                                                        // would touch
        }
    }
    return out;
}

bool inView(double dx, double dy, double dz, double headingDeg, double pitchDeg,
            double hfovDeg, double vfovDeg, double radiusM) {
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= radiusM) return true;
    const double h = headingDeg * kPi / 180.0, p = pitchDeg * kPi / 180.0;
    // Camera basis in local coordinates (+X east, +Y up, +Z south).
    const double fx = std::sin(h) * std::cos(p), fy = std::sin(p), fz = -std::cos(h) * std::cos(p);
    const double rx = std::cos(h),               ry = 0.0,         rz = std::sin(h);
    const double ux = -std::sin(h) * std::sin(p), uy = std::cos(p), uz = std::cos(h) * std::sin(p);
    const double z = dx * fx + dy * fy + dz * fz;
    if (z <= 0.0) return false;                                   // behind the camera
    const double x = dx * rx + dy * ry + dz * rz;
    const double y = dx * ux + dy * uy + dz * uz;
    const double margin = std::atan(radiusM / dist);
    return std::atan(std::fabs(x) / z) <= hfovDeg * kPi / 360.0 + margin &&
           std::atan(std::fabs(y) / z) <= vfovDeg * kPi / 360.0 + margin;
}

// ── CDU scratchpad stability ──────────────────────────────────────────────────

std::string StableText::feed(const std::string& text, double now) {
    if (text != m_text) {
        m_text  = text;
        m_since = now;
        return {};
    }
    if (m_text.empty() || m_since < 0 || now - m_since < kStableSec) return {};
    if (!m_done.insert(m_text).second) return {};                 // processed already
    return m_text;
}

void StableText::reset() {
    m_text.clear();
    m_since = -1;
    m_done.clear();
}

std::string cleanCduText(const char* bytes, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && bytes[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        s += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
    }
    const auto b = s.find_first_not_of(' ');
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(' ') - b + 1);
}

} // namespace Reservations
