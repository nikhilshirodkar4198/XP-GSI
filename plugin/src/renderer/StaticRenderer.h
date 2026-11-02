#pragma once
// StaticRenderer — places OBJ instances at airline gate stands.
//
// Holds a *plan*: every eligible stand of the active airport in fill order
// (Planner::rankStands) with its chosen model. A target count says how many
// of the plan, from the front, should show a static; step() moves toward it
// a few statics per call, so density changes are gradual and never reshuffle
// Models are
// loaded on first use. A CSL model may consist of several OBJs (one per OBJ8
// line); all are instanced at the same position.
// All public methods must be called from the main thread.
//
// SDK calls verified against vendor/XPSDK/CHeaders/XPLM/:
//   XPLMLoadObjectAsync      XPLMScenery.h   XPLM210  (path relative to X-System folder)
//   XPLMUnloadObject         XPLMScenery.h   XPLM200
//   XPLMCreateProbe          XPLMScenery.h   XPLM200
//   XPLMProbeTerrainXYZ      XPLMScenery.h   XPLM200
//   XPLMDestroyProbe         XPLMScenery.h   XPLM200
//   XPLMCreateInstance       XPLMInstance.h
//   XPLMInstanceSetPosition  XPLMInstance.h
//   XPLMDestroyInstance      XPLMInstance.h
//   XPLMWorldToLocal         XPLMGraphics.h  XPLM200

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <XPLMInstance.h>
#include <XPLMScenery.h>

#include "csl/CslDb.h"
#include "engine/StandLayout.h"
#include "engine/StandState.h"

// A static's lights: about a quarter of statics are "lit";
// of those most show nav + logo lights at night and a minority nav lights
// only (different company SOPs). By day every lit static shows nav lights
// only. The rest stay dark day and night.
enum class LightMode : std::uint8_t { Dark, NavOnly, NavLogo };

// The model chosen for one stand (worker-side selection result).
struct StandModel {
    const CslVariant* model        = nullptr;  // nullptr → stub OBJ
    // Metres the model's nose gear lies forward of its OBJ origin
    // (ObjGeometry::noseGearForwardM). The origin is placed this far behind
    // the stand point so the nose gear stops on the ramp start.
    float             noseGearFwdM = 0.0f;
    // OBJ parts to load instead of model->objPaths (XP-GSI's patched copies
    // whose logo lights follow the nav switch; csl/ObjLightPatch). Empty →
    // model->objPaths. Absolute paths.
    std::vector<std::filesystem::path> parts;
    LightMode         lights       = LightMode::Dark;
    // Ground outline as planned (StandLayout), for touch tests with traffic.
    StandLayout::Shape shape       = StandLayout::stubShape();
};

class StaticRenderer {
public:
    StaticRenderer()  = default;
    ~StaticRenderer() { destroyAll(); }

    // Replace the plan. stands[i] / models[i] in fill order (index 0 is the
    // first stand to fill). `db` owns the variants and is kept alive while
    // the plan exists. `xplaneRoot` turns the catalogue's absolute OBJ paths
    // into the X-System-relative paths XPLMLoadObjectAsync requires.
    // Nothing is placed until setTargets() + step().
    void loadPlan(std::vector<Stand>           stands,
                  std::vector<StandModel>      models,
                  std::shared_ptr<const CslDb> db,
                  const std::filesystem::path& xplaneRoot,
                  const std::string&           stubObjXpRelPath);

    // How many passenger stands and how many cargo stands (Stand::op Cargo)
    // should show a static: the first ones of each kind in plan order. The
    // two are independent, so switching cargo off removes only cargo statics.
    void        setTargets(std::size_t passenger, std::size_t cargo);
    std::size_t target() const { return m_target; }   // both together

    // Move toward the targets: remove statics no longer planned (from the
    // back) and place missing ones, at most maxChanges in total; request
    // model loads as needed. Call once per flight-loop tick.
    void step(int maxChanges);

    // Keep live instances placed. Positions are cached per stand: a tick does
    // nothing unless X-Plane's local origin moved (every stand placed again),
    // day / night changed (lights only, no probes), plus a round-robin
    // re-probe of kRefreshPerTick stands so heights settle as terrain loads.
    // Call once per flight-loop tick.
    void reposition();
    static constexpr std::size_t kRefreshPerTick = 4;

    // Night: NavLogo statics add their logo lights (by day every lit static
    // shows nav lights only). Takes effect at the next reposition().
    void setNight(bool night) { m_night = night; }

    // Destroy all instances, unload all OBJs and drop the plan.
    void destroyAll();

    bool        hasPlan()  const { return !m_stands.empty(); }
    std::size_t planSize() const { return m_stands.size(); }
    const Stand& stand(std::size_t i) const { return m_stands[i]; }
    const std::vector<Stand>& stands() const { return m_stands; }

    // Part of the current target (should have a static unless suppressed).
    bool isPlanned(std::size_t i) const { return i < m_planned.size() && m_planned[i]; }
    bool hasInstance(std::size_t i) const;

    // Conflict engine: keep stand i empty (destroying its static now), or let
    // step() fill it again.
    void setSuppressed(std::size_t i, bool suppressed);
    bool isSuppressed(std::size_t i) const;

    int instanceCount() const;  // stands showing a static
    int pendingCount()  const;  // planned, not suppressed, not yet placed
    int clearedCount()  const;  // planned but suppressed (cleared for traffic)

private:
    // One per distinct model; parts loaded lazily on first use.
    struct Group {
        std::vector<std::string>   objXpRelPaths;   // one per OBJ part
        std::vector<XPLMObjectRef> objs;            // parallel; null until loaded
        int                        partsPending  = 0;
        bool                       loadRequested = false;
        bool                       ready         = false;  // every part loaded
        bool                       failed        = false;  // a part failed: stands stay empty
        bool                       isCsl         = false;
        float                      vertOfsM      = 0.0f;
        float                      noseGearFwdM  = 0.0f;
    };

    // Heap-allocated refcon for XPLMLoadObjectAsync.
    struct LoadCtx {
        StaticRenderer* renderer;
        uint32_t        generation;
        size_t          groupIdx;
        size_t          partIdx;
    };

    static void objLoadedCB(XPLMObjectRef obj, void* refcon);
    void requestLoad(std::size_t groupIdx);
    bool createStatic(std::size_t standIdx);     // all parts or none
    void destroyStatic(std::size_t standIdx);
    XPLMDrawInfo_t computePosition(const Stand& stand, const Group& group);   // probes the terrain
    void applyPosition(std::size_t standIdx);                                 // cached position + lights
    void placeStand(std::size_t standIdx);                                    // compute + apply
    const float* valuesFor(std::size_t standIdx) const;   // dataref values for its lights now

    XPLMProbeRef                              m_probe = nullptr;
    std::vector<Stand>                        m_stands;
    std::vector<std::size_t>                  m_groupOf;     // stand → group
    std::vector<std::vector<XPLMInstanceRef>> m_inst;        // stand → one per part
    std::vector<bool>                         m_suppressed;
    std::vector<bool>                         m_planned;     // stand → within its kind's target
    std::vector<LightMode>                    m_lights;      // stand → its lights
    bool                                      m_night = false;
    bool                                      m_appliedNight = false;
    std::vector<XPLMDrawInfo_t>               m_pos;         // stand → its position (local frame)
    double                                    m_originX = 0, m_originZ = 0;   // a fixed point in the local frame
    bool                                      m_originKnown = false;
    std::size_t                               m_refreshCursor = 0;
    std::vector<Group>                        m_groups;
    std::shared_ptr<const CslDb>              m_db;          // keeps CslVariant pointers valid
    const char**                              m_datarefs    = nullptr;
    const float*                              m_datarefVals = nullptr;   // dark
    const float*                              m_navVals     = nullptr;   // nav lights
    const float*                              m_navLogoVals = nullptr;   // nav + logo lights
    std::size_t                               m_target      = 0;   // planned stands
    std::size_t                               m_paxTarget   = 0;
    std::size_t                               m_cargoTarget = 0;
    int                                       m_loadsInFlight = 0;
    uint32_t                                  m_generation  = 0;
    bool                                      m_reportedDone = false;
};
