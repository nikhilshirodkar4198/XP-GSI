// StaticRenderer: places CSL model instances at airline gate stands,
// gradually, from a seeded plan.

#include "renderer/StaticRenderer.h"

#include "core/Guard.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <XPLMGraphics.h>

#include "core/Log.h"
#include "renderer/CslDatarefs.h"

// ── Public API ────────────────────────────────────────────────────────────────

void StaticRenderer::loadPlan(std::vector<Stand>           stands,
                              std::vector<StandModel>      models,
                              std::shared_ptr<const CslDb> db,
                              const std::filesystem::path& xplaneRoot,
                              const std::string&           stubObjXpRelPath) {
    destroyAll();

    m_stands = std::move(stands);
    m_db     = std::move(db);
    m_inst.assign(m_stands.size(), {});
    m_pos.assign(m_stands.size(), XPLMDrawInfo_t{});
    m_suppressed.assign(m_stands.size(), false);
    m_planned.assign(m_stands.size(), false);
    m_lights.assign(m_stands.size(), LightMode::Dark);
    m_groupOf.assign(m_stands.size(), 0);
    ++m_generation;

    // Instance datarefs must be registered before any object that uses them is
    // loaded (XPLMInstance.h). The same list and values serve the stub too.
    m_datarefs    = CslDatarefs::ensureRegistered();
    m_datarefVals = CslDatarefs::coldAndDarkValues();
    m_navVals     = CslDatarefs::navLightsValues();
    m_navLogoVals = CslDatarefs::navLogoValues();

    // Group stands that share the same model (nullptr = stub).
    std::unordered_map<const CslVariant*, size_t> modelToGroup;
    size_t cslStands = 0;
    for (size_t i = 0; i < m_stands.size(); ++i) {
        const StandModel  choice = (i < models.size()) ? models[i] : StandModel{};
        const CslVariant* model  = choice.model;
        auto [it, inserted] = modelToGroup.emplace(model, m_groups.size());
        if (inserted) {
            Group g;
            if (model) {
                g.isCsl        = true;
                g.vertOfsM     = model->vertOfsM;
                g.noseGearFwdM = choice.noseGearFwdM;
                for (const auto& p : choice.parts.empty() ? model->objPaths : choice.parts) {
                    // Catalogue paths are absolute under the X-Plane root;
                    // lexically_relative is pure string work (no disk access).
                    auto rel = p.lexically_relative(xplaneRoot);
                    g.objXpRelPaths.push_back((rel.empty() ? p : rel).generic_u8string());
                }
            } else {
                g.objXpRelPaths.push_back(stubObjXpRelPath);
            }
            g.objs.assign(g.objXpRelPaths.size(), nullptr);
            m_groups.push_back(std::move(g));
        }
        m_groupOf[i] = it->second;
        m_lights[i]  = choice.lights;
        if (model) ++cslStands;
    }

    Log::info("StaticRenderer: plan of %zu stands (%zu CSL, %zu stub), %zu model(s) (gen %u)",
              m_stands.size(), cslStands, m_stands.size() - cslStands, m_groups.size(), m_generation);
}

void StaticRenderer::setTargets(std::size_t passenger, std::size_t cargo) {
    if (passenger == m_paxTarget && cargo == m_cargoTarget && m_planned.size() == m_stands.size()) return;
    Log::info("StaticRenderer: target %zu passenger + %zu cargo → %zu + %zu statics",
              m_paxTarget, m_cargoTarget, passenger, cargo);
    m_paxTarget   = passenger;
    m_cargoTarget = cargo;
    m_planned.assign(m_stands.size(), false);
    std::size_t p = 0, c = 0;
    for (size_t i = 0; i < m_stands.size(); ++i) {
        std::size_t& n = m_stands[i].op == OpType::Cargo ? c : p;
        if (n < (m_stands[i].op == OpType::Cargo ? cargo : passenger)) { m_planned[i] = true; ++n; }
    }
    m_target       = p + c;
    m_reportedDone = false;
}

void StaticRenderer::step(int maxChanges) {
    if (m_stands.empty()) return;
    int changes = 0;

    // Remove what is no longer planned, last-ranked first.
    for (size_t i = m_stands.size(); i-- > 0 && changes < maxChanges;) {
        if (!m_planned[i] && !m_inst[i].empty()) {
            destroyStatic(i);
            ++changes;
        }
    }

    // Fill toward the target, first-ranked first. Loads are requested for
    // every planned model now, so they stream in while earlier stands spawn.
    for (size_t i = 0; i < m_stands.size(); ++i) {
        if (!m_planned[i] || !m_inst[i].empty() || m_suppressed[i]) continue;
        Group& g = m_groups[m_groupOf[i]];
        if (g.failed) continue;
        if (!g.ready) {
            if (!g.loadRequested) requestLoad(m_groupOf[i]);
            continue;
        }
        if (changes >= maxChanges) continue;
        if (createStatic(i)) ++changes;
    }

    if (!m_reportedDone && m_loadsInFlight == 0 && pendingCount() == 0) {
        m_reportedDone = true;
        size_t objInstances = 0;
        for (const auto& v : m_inst) objInstances += v.size();
        Log::info("StaticRenderer: %d static(s) placed (%zu OBJ instances), %d cleared for traffic",
                  instanceCount(), objInstances, clearedCount());
    }
}

void StaticRenderer::reposition() {
    if (!m_probe || m_stands.empty()) return;
    // Has the local frame moved? (X-Plane shifts its origin as the user flies.)
    double x = 0, y = 0, z = 0;
    XPLMWorldToLocal(m_stands.front().lat, m_stands.front().lon, 0.0, &x, &y, &z);
    const bool moved = !m_originKnown || std::abs(x - m_originX) > 0.01 || std::abs(z - m_originZ) > 0.01;
    m_originX = x; m_originZ = z; m_originKnown = true;
    if (moved) {
        for (size_t i = 0; i < m_stands.size(); ++i)
            if (!m_inst[i].empty()) placeStand(i);
        m_appliedNight = m_night;
        return;
    }
    if (m_night != m_appliedNight) {
        for (size_t i = 0; i < m_stands.size(); ++i)
            if (!m_inst[i].empty()) applyPosition(i);
        m_appliedNight = m_night;
    }
    // A few stands probed again each tick: terrain may still be loading.
    for (size_t n = 0; n < kRefreshPerTick && n < m_stands.size(); ++n) {
        m_refreshCursor = (m_refreshCursor + 1) % m_stands.size();
        if (!m_inst[m_refreshCursor].empty()) placeStand(m_refreshCursor);
    }
}

void StaticRenderer::destroyAll() {
    ++m_generation; // invalidate any in-flight XPLMLoadObjectAsync callbacks
    m_loadsInFlight = 0;

    const bool hadWork = !m_groups.empty() || m_probe;

    for (auto& perStand : m_inst)
        for (XPLMInstanceRef inst : perStand)
            XPLMDestroyInstance(inst);
    for (auto& g : m_groups)
        for (XPLMObjectRef obj : g.objs)
            if (obj) XPLMUnloadObject(obj);

    m_inst.clear();
    m_pos.clear();
    m_originKnown   = false;
    m_refreshCursor = 0;
    m_groups.clear();
    m_groupOf.clear();
    m_suppressed.clear();
    m_planned.clear();
    m_lights.clear();
    m_stands.clear();
    m_db.reset();
    m_target       = 0;
    m_paxTarget    = 0;
    m_cargoTarget  = 0;
    m_reportedDone = false;

    if (m_probe) {
        XPLMDestroyProbe(m_probe);
        m_probe = nullptr;
    }
    if (hadWork)
        Log::info("StaticRenderer: all instances destroyed");
}

bool StaticRenderer::hasInstance(std::size_t i) const {
    return i < m_inst.size() && !m_inst[i].empty();
}

void StaticRenderer::setSuppressed(std::size_t i, bool suppressed) {
    if (i >= m_suppressed.size()) return;
    m_suppressed[i] = suppressed;
    if (suppressed) destroyStatic(i);
    else            m_reportedDone = false;
}

bool StaticRenderer::isSuppressed(std::size_t i) const {
    return i < m_suppressed.size() && m_suppressed[i];
}

int StaticRenderer::instanceCount() const {
    int n = 0;
    for (const auto& v : m_inst) if (!v.empty()) ++n;
    return n;
}

int StaticRenderer::pendingCount() const {
    int n = 0;
    for (size_t i = 0; i < m_planned.size(); ++i)
        if (m_planned[i] && m_inst[i].empty() && !m_suppressed[i] && !m_groups[m_groupOf[i]].failed) ++n;
    return n;
}

int StaticRenderer::clearedCount() const {
    int n = 0;
    for (size_t i = 0; i < m_planned.size(); ++i)
        if (m_planned[i] && m_suppressed[i]) ++n;
    return n;
}

// ── Private ───────────────────────────────────────────────────────────────────

void StaticRenderer::requestLoad(std::size_t gi) {
    Group& g = m_groups[gi];
    g.loadRequested = true;
    g.partsPending  = static_cast<int>(g.objXpRelPaths.size());
    for (size_t pi = 0; pi < g.objXpRelPaths.size(); ++pi) {
        auto* ctx = new LoadCtx{this, m_generation, gi, pi};
        ++m_loadsInFlight;
        // XPLMLoadObjectAsync (XPLM210): non-blocking; callback on main thread.
        XPLMLoadObjectAsync(g.objXpRelPaths[pi].c_str(),
                            [](XPLMObjectRef o, void* r) { Guard::run("OBJ loaded", [&] { objLoadedCB(o, r); }); }, ctx);
    }
}

void StaticRenderer::objLoadedCB(XPLMObjectRef obj, void* refcon) {
    auto* ctx  = static_cast<LoadCtx*>(refcon);
    StaticRenderer* self = ctx->renderer;
    const uint32_t gen = ctx->generation;
    const size_t   gi  = ctx->groupIdx;
    const size_t   pi  = ctx->partIdx;
    delete ctx;

    if (gen != self->m_generation) {
        if (obj) XPLMUnloadObject(obj);
        return;
    }

    --self->m_loadsInFlight;
    Group& g = self->m_groups[gi];
    --g.partsPending;
    if (obj)
        g.objs[pi] = obj;
    else
        Log::warn("StaticRenderer: OBJ load failed for '%s'", g.objXpRelPaths[pi].c_str());

    if (g.partsPending == 0) {
        // Like XPMP2 (CSLModel::GetAllObjRefs), a model is drawn only when
        // every one of its objects loaded; a partial model stays empty.
        bool complete = true;
        for (XPLMObjectRef o : g.objs) complete = complete && o;
        g.ready  = complete;
        g.failed = !complete;
        if (!complete)
            Log::warn("StaticRenderer: model '%s' incomplete — its stands stay empty",
                      g.objXpRelPaths.front().c_str());
    }
}

bool StaticRenderer::createStatic(std::size_t i) {
    if (!m_probe)
        // XPLMCreateProbe (XPLM200): Y-probe; reused for cache efficiency.
        m_probe = XPLMCreateProbe(xplm_ProbeY);

    const Group& g = m_groups[m_groupOf[i]];
    std::vector<XPLMInstanceRef> out;
    out.reserve(g.objs.size());
    for (XPLMObjectRef obj : g.objs) {
        // XPLMCreateInstance: obj fully loaded (async CB has fired for every part).
        XPLMInstanceRef inst = XPLMCreateInstance(obj, m_datarefs);
        if (!inst) {
            for (XPLMInstanceRef made : out) XPLMDestroyInstance(made);
            Log::warn("StaticRenderer: XPLMCreateInstance failed for stand '%s'",
                      m_stands[i].id.c_str());
            return false;
        }
        out.push_back(inst);
    }
    m_inst[i] = std::move(out);
    placeStand(i);
    return true;
}

void StaticRenderer::destroyStatic(std::size_t i) {
    for (XPLMInstanceRef inst : m_inst[i])
        XPLMDestroyInstance(inst);
    m_inst[i].clear();
}

const float* StaticRenderer::valuesFor(std::size_t i) const {
    switch (m_lights[i]) {
        case LightMode::NavLogo: return m_night ? m_navLogoVals : m_navVals;
        case LightMode::NavOnly: return m_navVals;
        default:                 return m_datarefVals;
    }
}

void StaticRenderer::placeStand(std::size_t i) {
    m_pos[i] = computePosition(m_stands[i], m_groups[m_groupOf[i]]);
    applyPosition(i);
}

void StaticRenderer::applyPosition(std::size_t i) {
    // XPLMInstanceSetPosition (XPLM301): one float per registered dataref
    // (cold and dark: gear down, lights off, engines stopped; plus nav and,
    // in the patched OBJ copies, logo lights per valuesFor()).
    const float* values = valuesFor(i);
    for (XPLMInstanceRef inst : m_inst[i]) XPLMInstanceSetPosition(inst, &m_pos[i], values);
}

XPLMDrawInfo_t StaticRenderer::computePosition(const Stand& stand, const Group& group) {
    // XPLMWorldToLocal (XPLM200): lat/lon/alt_msl → local OpenGL XYZ.
    // The local frame is flat (+Y up at its origin only), so the stand must
    // be converted at its ground elevation: converting at 0 m MSL put the
    // point sideways by ≈ elevation × distance from the origin / earth radius
    // — metres at a high airport (every static off the
    // parking lines at KCOS, 6,187 ft). First find the ground elevation
    // (XPLMProbeTerrainXYZ, then XPLMLocalToWorld), then convert again there.
    XPLMProbeInfo_t info{};
    info.structSize = static_cast<int>(sizeof(info));
    auto probeY = [&](double x, double y, double z, float fallback) {
        const XPLMProbeResult r = XPLMProbeTerrainXYZ(m_probe, static_cast<float>(x), static_cast<float>(y),
                                                      static_cast<float>(z), &info);
        return r == xplm_ProbeHitTerrain ? info.locationY : fallback;
    };
    double lx, ly, lz;
    XPLMWorldToLocal(stand.lat, stand.lon, 0.0, &lx, &ly, &lz);
    double gLat = 0, gLon = 0, gAlt = 0;
    XPLMLocalToWorld(lx, probeY(lx, ly, lz, static_cast<float>(ly)), lz, &gLat, &gLon, &gAlt);
    XPLMWorldToLocal(stand.lat, stand.lon, gAlt, &lx, &ly, &lz);

    // The stand point is where the nose gear stops: move the model origin
    // back along the stand heading by the nose gear's forward offset.
    // Local frame (XPLMGraphics.h): +X east, +Z south; heading is true,
    // clockwise from north, so "forward" = (sin h, 0, -cos h).
    const double h = stand.headingDeg * 3.14159265358979323846 / 180.0;
    lx -= group.noseGearFwdM * std::sin(h);
    lz += group.noseGearFwdM * std::cos(h);

    const float groundY = probeY(lx, ly, lz, static_cast<float>(ly));

    // XPLMDrawInfo_t (XPLM200): heading is true heading clockwise from north.
    XPLMDrawInfo_t di{};
    di.structSize = static_cast<int>(sizeof(di));
    di.x          = static_cast<float>(lx);
    // Vertical offset lifts the model onto its gear: XPMP2 Aircraft::SetLocation
    // sets drawInfo.y = y + GetVertOfs().
    di.y          = groundY + group.vertOfsM;
    di.z          = static_cast<float>(lz);
    di.pitch      = 0.0f;
    di.heading    = stand.headingDeg;
    di.roll       = 0.0f;
    return di;
}
