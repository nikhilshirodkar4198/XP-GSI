#include "app/ArrivalGate.h"

#include "core/Guard.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <XPLMGraphics.h>
#include <XPLMPlanes.h>

#include "core/Log.h"
#include "csl/IcaoSize.h"
#include "engine/FollowUser.h"
#include "engine/GateOccupancy.h"
#include "engine/GateSuggest.h"
#include "geo/Geo.h"
#include "renderer/StaticRenderer.h"

namespace {

constexpr float  kCduPollSec      = 0.25f;
constexpr double kAirportRangeKm  = 20.0;    // reservations only near the active airport
constexpr const char* kArrivalDataref = "xpgsi/arrival_stand";

struct RefSet {
    XPLMDataRef onGround = nullptr, gs = nullptr, brake = nullptr, engRunning = nullptr,
                numEngines = nullptr, lat = nullptr, lon = nullptr, psi = nullptr,
                elev = nullptr, agl = nullptr, vx = nullptr, vy = nullptr, vz = nullptr,
                vHeading = nullptr, vPitch = nullptr, fov = nullptr, winW = nullptr,
                winH = nullptr, icao = nullptr;
    bool found = false;
};
RefSet gRefs;

std::string acfIcao() {
    if (!gRefs.icao) return {};
    char buf[41] = {};
    XPLMGetDatab(gRefs.icao, buf, 0, 40);
    return Reservations::cleanCduText(buf, 40);
}

std::string standList(const std::vector<Stand>& all, const std::vector<std::size_t>& idx) {
    std::string s;
    for (std::size_t i : idx) {
        if (!s.empty()) s += ", ";
        s += all[i].id;
    }
    return s;
}

} // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ArrivalGate::registerApi(const std::string& adaptersFile) {
    m_adaptersFile = adaptersFile;
    // Writable string dataref (xplmType_Data). Other add-ons write a gate name.
    m_arrivalRef = XPLMRegisterDataAccessor(kArrivalDataref, xplmType_Data, 1,
                                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr, nullptr,
                                            [](void* r, void* o, int off, int n) { return Guard::run("xpgsi/arrival_stand", 0, [&] { return arrivalGet(r, o, off, n); }); },
                                            [](void* r, void* v, int off, int n) { Guard::run("xpgsi/arrival_stand", [&] { arrivalSet(r, v, off, n); }); },
                                            this, this);
    // No commands of its own: only xpgsi/toggle_window and xpgsi/gate_window
    // are bindable (main.cpp).
}

void ArrivalGate::unregisterApi() {
    if (m_arrivalRef) XPLMUnregisterDataAccessor(m_arrivalRef);
    m_arrivalRef = nullptr;
    m_toast.destroy();
}

void ArrivalGate::enable() {
    XPLMCreateFlightLoop_t p{};
    p.structSize   = static_cast<int>(sizeof(p));
    p.phase        = xplm_FlightLoop_Phase_BeforeFlightModel;
    p.callbackFunc = [](float a, float b, int n, void* r) { return Guard::run("CDU poll", kCduPollSec, [&] { return cduLoopCB(a, b, n, r); }); };
    p.refcon       = this;
    m_cduLoop = XPLMCreateFlightLoop(&p);
    XPLMScheduleFlightLoop(m_cduLoop, kCduPollSec, 1);
}

void ArrivalGate::disable() {
    if (m_cduLoop) XPLMDestroyFlightLoop(m_cduLoop);
    m_cduLoop = nullptr;
    m_arm.reset();
    m_set.releaseAll();
    m_toast.destroy();
}

void ArrivalGate::findRefs() {
    if (gRefs.found) return;
    gRefs.found      = true;
    gRefs.onGround   = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    gRefs.gs         = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    // DataRefs.txt, XP11 and XP12: float [0..1], 1 = max. (XP12 marks
    // sim/cockpit2/controls/parking_brake_ratio "REPLACED" and logs it.)
    gRefs.brake      = XPLMFindDataRef("sim/flightmodel/controls/parkbrake");
    gRefs.engRunning = XPLMFindDataRef("sim/flightmodel/engine/ENGN_running");
    gRefs.numEngines = XPLMFindDataRef("sim/aircraft/engine/acf_num_engines");
    gRefs.lat        = XPLMFindDataRef("sim/flightmodel/position/latitude");
    gRefs.lon        = XPLMFindDataRef("sim/flightmodel/position/longitude");
    gRefs.psi        = XPLMFindDataRef("sim/flightmodel/position/psi");
    gRefs.elev       = XPLMFindDataRef("sim/flightmodel/position/elevation");
    gRefs.agl        = XPLMFindDataRef("sim/flightmodel/position/y_agl");
    gRefs.vx         = XPLMFindDataRef("sim/graphics/view/view_x");
    gRefs.vy         = XPLMFindDataRef("sim/graphics/view/view_y");
    gRefs.vz         = XPLMFindDataRef("sim/graphics/view/view_z");
    gRefs.vHeading   = XPLMFindDataRef("sim/graphics/view/view_heading");
    gRefs.vPitch     = XPLMFindDataRef("sim/graphics/view/view_pitch");
    gRefs.fov        = XPLMFindDataRef("sim/graphics/view/field_of_view_deg");
    gRefs.winW       = XPLMFindDataRef("sim/graphics/view/window_width");
    gRefs.winH       = XPLMFindDataRef("sim/graphics/view/window_height");
    gRefs.icao       = XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
    if (!gRefs.onGround || !gRefs.gs || !gRefs.brake || !gRefs.engRunning || !gRefs.lat || !gRefs.psi)
        Log::warn("ArrivalGate: some flight datarefs are missing — reservations may not arm");
}

void ArrivalGate::onNewFlight() {
    m_arm.reset();
    m_stable.reset();
    releaseAll("new flight loaded", 0.0);
    m_adapterChosen = false;
    m_adapter = nullptr;
    m_cduRef = nullptr;
}

void ArrivalGate::onPlan(std::vector<Stand> allStands, std::vector<StandLayout::Shape> planShapes,
                         double refLat, double refLon) {
    if (!m_set.empty()) releaseAll("the active airport changed", 0.0);
    m_allStands  = std::move(allStands);
    m_planShapes = std::move(planShapes);
    m_refLat = refLat;
    m_refLon = refLon;
    m_allNames.clear();
    m_normNames.clear();
    for (const auto& s : m_allStands) {
        m_allNames.push_back(s.id);
        m_normNames.push_back(Reservations::normalizeStand(s.id));
    }
    m_planIndex.clear();   // rebuilt on the next tick, against the renderer's new plan
    m_trafficAt.assign(m_allStands.size(), false);
    m_blocked.clear();
    m_clearedByUs.clear();
    m_needRecompute = true;
}

// ── Reserve / release ─────────────────────────────────────────────────────────

bool ArrivalGate::available(std::string* why) const {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    // Any time an airport is in range: below the
    // follow-user ceiling (or on the ground) and near the planned airport.
    const bool onGround = gRefs.onGround && XPLMGetDatai(gRefs.onGround) != 0;
    if (!onGround && gRefs.elev && XPLMGetDatad(gRefs.elev) * 3.28084 >= FollowUser::kCeilingFt)
        return no("Gate reservation is available below 10,000 ft");
    if (m_allStands.empty()) return no("No airport with stands is planned here");
    if (gRefs.lat && gRefs.lon &&
        haversineKm(XPLMGetDatad(gRefs.lat), XPLMGetDatad(gRefs.lon), m_refLat, m_refLon) > kAirportRangeKm)
        return no("Not at the planned airport");
    if (why) why->clear();
    return true;
}

std::string ArrivalGate::reserve(const std::string& entry, const char* source, double now) {
    findRefs();
    std::string why;
    if (!available(&why)) {
        Log::info("Reservation: '%s' from %s ignored — %s", entry.c_str(), source, why.c_str());
        return {};
    }
    const auto idx = Reservations::match(entry, m_allNames);
    if (idx.empty()) {
        Log::info("Reservation: '%s' from %s matches no stand — ignored", entry.c_str(), source);
        return {};
    }
    return reserveStands(idx, Reservations::normalizeEntry(entry), source, now);
}

std::string ArrivalGate::reserveStands(const std::vector<std::size_t>& stands, const std::string& label,
                                       const char* source, double now) {
    findRefs();
    std::string why;
    if (!available(&why)) {
        Log::info("Reservation: '%s' from %s ignored — %s", label.c_str(), source, why.c_str());
        return {};
    }
    std::vector<std::size_t> idx;
    for (std::size_t s : stands)
        if (s < m_allStands.size()) idx.push_back(s);
    if (idx.empty()) return {};
    if (m_set.add(idx) == 0) {
        Log::info("Reservation: '%s' already reserved", label.c_str());
        return {};
    }
    char msg[160];
    if (idx.size() > 1) std::snprintf(msg, sizeof(msg), "Gate %s reserved (%zu stands)", label.c_str(), idx.size());
    else                std::snprintf(msg, sizeof(msg), "Gate %s reserved", label.c_str());
    m_toast.show(msg, now);
    applyBlocks(now);   // the reserved stand empties at once
    Log::info("Reservation: %s from %s — stand(s) %s; %zu reserved in all",
              msg, source, standList(m_allStands, idx).c_str(), m_set.stands().size());
    m_needRecompute = true;
    return msg;
}

int ArrivalGate::statusOf(std::size_t i) const {
    using GateSuggest::Status;
    if (m_set.contains(i)) return static_cast<int>(Status::Reserved);
    if (i < m_trafficAt.size() && m_trafficAt[i]) return static_cast<int>(Status::Traffic);
    if (m_renderer && i < m_planIndex.size() && m_planIndex[i] >= 0 &&
        m_renderer->hasInstance(static_cast<std::size_t>(m_planIndex[i])))
        return static_cast<int>(Status::Static);
    return static_cast<int>(Status::Free);
}

double ArrivalGate::distanceTo(std::size_t i) const {
    if (i >= m_allStands.size() || !gRefs.lat || !gRefs.lon) return 0.0;
    return haversineKm(XPLMGetDatad(gRefs.lat), XPLMGetDatad(gRefs.lon),
                       m_allStands[i].lat, m_allStands[i].lon) * 1000.0;
}

char ArrivalGate::userSizeCode() const {
    char code = IcaoSize::lookup(acfIcao());
    return (code >= 'A' && code <= 'F') ? code : 'C';
}

void ArrivalGate::releaseOne(std::size_t stand, double /*now*/) {
    if (!m_set.release(stand)) return;
    Log::info("Reservation: released stand '%s'",
              stand < m_allStands.size() ? m_allStands[stand].id.c_str() : "?");
    m_needRecompute = true;
}

void ArrivalGate::releaseAll(const char* why, double /*now*/) {
    const std::size_t n = m_set.releaseAll();
    if (n) {
        Log::info("Reservation: released all %zu reservation(s) (%s)", n, why);
        m_needRecompute = true;
    }
}

// ── Geometry helpers ──────────────────────────────────────────────────────────

StandLayout::Shape ArrivalGate::userShape() const {
    return Reservations::userShapeForSize(userSizeCode());
}

bool ArrivalGate::userAtStand(const Stand& s) const {
    GateOccupancy::Traffic t;
    t.lat = XPLMGetDatad(gRefs.lat);
    t.lon = XPLMGetDatad(gRefs.lon);
    t.headingDeg = gRefs.psi ? XPLMGetDataf(gRefs.psi) : 0.0f;
    t.speedMs = 0.0f;
    t.onGround = true;
    return GateOccupancy::occupies(s, t);
}

// ── 1 Hz tick ─────────────────────────────────────────────────────────────────

void ArrivalGate::tick(double now, StaticRenderer& r, std::vector<float>& cooldownUntil, float cooldownSec) {
    findRefs();
    m_toast.tick(now);

    // Armed window.
    Reservations::Sample s;
    s.now           = now;
    s.onGround      = gRefs.onGround && XPLMGetDatai(gRefs.onGround) != 0;
    s.groundSpeedMs = gRefs.gs ? XPLMGetDataf(gRefs.gs) : 0.0f;
    s.parkBrake     = gRefs.brake ? XPLMGetDataf(gRefs.brake) : 0.0f;
    if (gRefs.engRunning) {
        int run[8] = {};
        const int n = std::clamp(gRefs.numEngines ? XPLMGetDatai(gRefs.numEngines) : 8, 1, 8);
        XPLMGetDatavi(gRefs.engRunning, run, 0, n);
        s.anyEngineRunning = std::any_of(run, run + n, [](int v) { return v != 0; });
    }
    const auto ev = m_arm.update(s);
    switch (ev) {
        case Reservations::Event::Armed:
            Log::info("Reservation: armed — touchdown after a flight; gate entries accepted until parked");
            m_stable.reset();
            chooseAdapter();
            break;
        case Reservations::Event::Parked:
        case Reservations::Event::EnginesOff: {
            std::size_t keep = static_cast<std::size_t>(-1);
            for (std::size_t st : m_set.stands())
                if (st < m_allStands.size() && userAtStand(m_allStands[st])) { keep = st; break; }
            if (keep != static_cast<std::size_t>(-1)) {
                const std::size_t n = m_set.releaseAllExcept(keep);
                Log::info("Reservation: disarmed (%s) at reserved stand '%s' — kept; %zu other(s) released",
                          Reservations::eventName(ev), m_allStands[keep].id.c_str(), n);
                if (n) m_needRecompute = true;
            } else {
                Log::info("Reservation: disarmed (%s)", Reservations::eventName(ev));
                releaseAll(Reservations::eventName(ev), now);
            }
            break;
        }
        case Reservations::Event::TookOff:
            Log::info("Reservation: disarmed (took off)");
            releaseAll("took off", now);
            break;
        case Reservations::Event::None: break;
    }

    // Status for the gate window: every stand → its planned static, if any;
    // live traffic on it (same occupancy test as the conflict engine).
    m_renderer = &r;
    if (m_planIndex.size() != m_allStands.size()) {
        m_planIndex.assign(m_allStands.size(), -1);
        for (std::size_t p = 0; p < r.planSize(); ++p)
            for (std::size_t a = 0; a < m_allStands.size(); ++a)
                if (m_planIndex[a] < 0 && m_allStands[a].id == r.stand(p).id &&
                    haversineKm(m_allStands[a].lat, m_allStands[a].lon, r.stand(p).lat, r.stand(p).lon) < 0.002) {
                    m_planIndex[a] = static_cast<long>(p);
                    break;
                }
    }
    m_trafficAt.assign(m_allStands.size(), false);
    for (const auto& t : m_traffic) {
        if (!t.onGround) continue;
        for (std::size_t a = 0; a < m_allStands.size(); ++a) {
            const Stand& st = m_allStands[a];
            // ~150 m box first: the full test only for stands nearby.
            if (m_trafficAt[a] || std::fabs(st.lat - t.lat) > 0.0014 || std::fabs(st.lon - t.lon) > 0.0025)
                continue;
            if (GateOccupancy::occupies(st, t)) m_trafficAt[a] = true;
        }
    }

    m_cooldownUntil = &cooldownUntil;
    m_cooldownSec   = cooldownSec;
    applyBlocks(now);
}

// Which planned statics the reservations keep away; blocked ones are removed
// at once (cleared instantly, not when out of view).
void ArrivalGate::applyBlocks(double now) {
    if (!m_renderer || !m_cooldownUntil) return;
    StaticRenderer& r = *m_renderer;
    std::vector<float>& cooldownUntil = *m_cooldownUntil;
    const float cooldownSec = m_cooldownSec;
    if (!r.hasPlan()) return;
    const std::size_t n = r.planSize();
    if (m_blocked.size() != n) m_needRecompute = true;
    if (m_clearedByUs.size() != n) m_clearedByUs.assign(n, false);
    if (cooldownUntil.size() != n) cooldownUntil.assign(n, -1.0f);

    if (m_needRecompute) {
        m_needRecompute = false;
        std::vector<Stand> reserved;
        for (std::size_t st : m_set.stands())
            if (st < m_allStands.size()) reserved.push_back(m_allStands[st]);
        auto blocked = Reservations::blockedPlanStands(reserved, r.stands(), m_planShapes, userShape());
        for (std::size_t i = 0; i < n; ++i)
            if (!blocked[i] && m_clearedByUs[i]) {   // released: back after the normal delay
                m_clearedByUs[i] = false;
                cooldownUntil[i] = static_cast<float>(now) + cooldownSec;
                Log::info("Reservation: stand '%s' free again — static returns in %.0f s",
                          r.stand(i).id.c_str(), static_cast<double>(cooldownSec));
            }
        m_blocked = std::move(blocked);
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!m_blocked[i] || r.isSuppressed(i)) continue;
        if (r.hasInstance(i)) Log::info("Reservation: static at '%s' cleared", r.stand(i).id.c_str());
        r.setSuppressed(i, true);
        m_clearedByUs[i] = true;
    }
}

// ── CDU ───────────────────────────────────────────────────────────────────────

void ArrivalGate::chooseAdapter() {
    m_adapterChosen = true;
    m_adapter = nullptr;
    m_cduRef = nullptr;
    std::string err;
    if (!CduAdapters::load(std::filesystem::u8path(m_adaptersFile), m_adapters, &err)) {   // re-read: edits apply without a rebuild
        Log::warn("ArrivalGate: %s: %s — MCDU gate entry unavailable", m_adaptersFile.c_str(), err.c_str());
        return;
    }
    char file[256] = {}, path[512] = {};
    XPLMGetNthAircraftModel(0, file, path);   // XPLMPlanes.h: 0 = user's aircraft
    CduAdapters::Aircraft ac{file, path, acfIcao()};
    m_adapter = CduAdapters::find(m_adapters, ac,
                                  [](const std::string& name) { return XPLMFindDataRef(name.c_str()) != nullptr; });
    if (!m_adapter) {
        Log::info("ArrivalGate: no CDU adapter for %s (%s) — MCDU gate entry unavailable",
                  file, ac.icao.c_str());
        return;
    }
    const std::string& rd = m_adapter->readDataref;
    m_cduLines.clear();
    if (const auto at = rd.find("{n}"); at != std::string::npos) {
        for (int i = 0; i < 16; ++i)
            if (XPLMDataRef ref = XPLMFindDataRef((rd.substr(0, at) + std::to_string(i) + rd.substr(at + 3)).c_str()))
                m_cduLines.push_back(ref);
        m_cduRef = m_cduLines.empty() ? nullptr : m_cduLines.front();
    } else {
        m_cduRef = XPLMFindDataRef(rd.c_str());
    }
    if (!m_cduRef)
        Log::warn("ArrivalGate: CDU adapter '%s': dataref '%s' not found", m_adapter->name.c_str(),
                  m_adapter->readDataref.c_str());
    else
        Log::info("ArrivalGate: CDU adapter '%s' (%s) for %s", m_adapter->name.c_str(),
                  m_adapter->readDataref.c_str(), file);
}

std::string ArrivalGate::readCdu() const {
    if (!m_cduRef || !m_adapter) return {};
    if (!m_cduLines.empty()) {   // the first line that is G + an existing stand; others ignored
        std::string stand;
        for (XPLMDataRef ref : m_cduLines) {
            std::vector<char> line(static_cast<std::size_t>(m_adapter->readBytes) + 1, '\0');
            const int got = XPLMGetDatab(ref, line.data(), 0, m_adapter->readBytes);
            std::string t = Reservations::cleanCduText(line.data(), static_cast<std::size_t>(std::max(got, 0)));
            if (Reservations::cduGateEntry(t, stand) && !Reservations::match(stand, m_allNames).empty())
                return t;
        }
        return {};
    }
    std::vector<char> buf(static_cast<std::size_t>(m_adapter->readBytes) + 1, '\0');
    const int got = XPLMGetDatab(m_cduRef, buf.data(), 0, m_adapter->readBytes);
    return Reservations::cleanCduText(buf.data(), static_cast<std::size_t>(std::max(got, 0)));
}

float ArrivalGate::cduLoopCB(float, float, int, void* refcon) {
    auto* self = static_cast<ArrivalGate*>(refcon);
    self->findRefs();
    // Whenever on the ground, "G<stand>" entries only.
    if (!gRefs.onGround || XPLMGetDatai(gRefs.onGround) == 0) return kCduPollSec;
    if (!self->m_adapterChosen) self->chooseAdapter();
    if (self->m_cduRef) {
        const double now = XPLMGetElapsedTime();
        const std::string due = self->m_stable.feed(self->readCdu(), now);
        std::string stand;
        // G + no existing stand (a G typed for something else): ignored, no message.
        if (!due.empty() && Reservations::cduGateEntry(due, stand)) {
            if (!Reservations::match(stand, self->m_allNames).empty()) self->reserve(stand, "CDU", now);
            else Log::info("CDU: '%s' names no stand here — ignored", due.c_str());
        }
    }
    return kCduPollSec;
}

// ── Published API ─────────────────────────────────────────────────────────────

int ArrivalGate::arrivalGet(void* refcon, void* out, int offset, int maxLength) {
    const auto* self = static_cast<const ArrivalGate*>(refcon);
    const std::string& t = self->m_arrivalText;
    const int len = static_cast<int>(t.size());
    if (!out) return len;
    if (offset < 0 || offset >= len || maxLength <= 0) return 0;
    const int n = std::min(maxLength, len - offset);
    std::memcpy(out, t.data() + offset, static_cast<std::size_t>(n));
    return n;
}

void ArrivalGate::arrivalSet(void* refcon, void* in, int offset, int length) {
    auto* self = static_cast<ArrivalGate*>(refcon);
    if (!in || length <= 0 || offset < 0 || offset > 256 || length > 256) return;
    std::string t = offset == 0 ? std::string{} : self->m_arrivalText;
    t.resize(static_cast<std::size_t>(offset), ' ');
    t.append(static_cast<const char*>(in), static_cast<std::size_t>(length));
    t = Reservations::cleanCduText(t.data(), t.size());
    self->m_arrivalText = t;
    if (!t.empty()) self->reserve(t, kArrivalDataref, XPLMGetElapsedTime());
}
