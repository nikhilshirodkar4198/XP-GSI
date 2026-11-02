#pragma once
// ArrivalGate — arrival gate reservation, the X-Plane side.
// Pure rules live in engine/Reservations; this file reads the
// sim, reads the CDU scratchpad, clears statics, shows the notification and
// publishes xpgsi/arrival_stand.
//
// Datarefs (DataRefs.txt of the X-Plane 11 install):
//   sim/flightmodel/failures/onground_any        int      user on the ground
//   sim/flightmodel/position/groundspeed         float    m/s
//   sim/flightmodel/controls/parkbrake           float    0..1 (XP11 + XP12)
//   sim/flightmodel/engine/ENGN_running          int[8]   engine on and using fuel
//   sim/aircraft/engine/acf_num_engines          int
//   sim/flightmodel/position/latitude|longitude  double
//   sim/flightmodel/position/psi                 float    true heading, degrees
//   sim/flightmodel/position/elevation           double   metres MSL
//   sim/flightmodel/position/y_agl               float    metres AGL
//   sim/graphics/view/view_x|y|z                 float    camera, local OpenGL metres
//   sim/graphics/view/view_heading|view_pitch    float    degrees
//   sim/graphics/view/field_of_view_deg          float    horizontal field of view
//   sim/graphics/view/window_width|window_height int      pixels (vertical FOV from aspect)
//   sim/aircraft/view/acf_ICAO                   byte[40] user aircraft ICAO type
//   sim/cockpit2/radios/indicators/fms_cdu1_text_line0..15  byte[96]
//
// SDK: XPLMRegisterDataAccessor (xplmType_Data, XPLMGetDatab_f / XPLMSetDatab_f),
//   XPLMUnregisterDataAccessor (XPLMDataAccess.h); XPLMCreateFlightLoop
//   (XPLMProcessing.h, XPLM300); XPLMGetNthAircraftModel (XPLMPlanes.h);
//   XPLMWorldToLocal (XPLMGraphics.h).
// Main thread only.

#include <cstddef>
#include <string>
#include <vector>

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <set>

#include "core/CduAdapters.h"
#include "engine/GateOccupancy.h"
#include "engine/Reservations.h"
#include "engine/StandLayout.h"
#include "engine/StandState.h"
#include "ui/Toast.h"

class StaticRenderer;

class ArrivalGate {
public:
    // XPluginStart / XPluginStop: the published dataref.
    void registerApi(const std::string& adaptersFile);
    void unregisterApi();
    // XPluginEnable / XPluginDisable: the 0.25 s CDU poll.
    void enable();
    void disable();

    // A new flight / aircraft was loaded: disarm, release everything.
    void onNewFlight();
    // The active airport's plan changed (empty vectors when it was dropped):
    // every reservation is released, the stand lists replaced.
    void onPlan(std::vector<Stand> allStands, std::vector<StandLayout::Shape> planShapes,
                double refLat, double refLon);

    // 1 Hz from the flight loop while running: armed window, clearing.
    // `cooldownUntil` / `cooldownSec`: the normal restore delay for released stands.
    void tick(double now, StaticRenderer& renderer, std::vector<float>& cooldownUntil, float cooldownSec);

    // A planned static kept away by a reservation (never restored meanwhile).
    bool blocked(std::size_t planIdx) const { return planIdx < m_blocked.size() && m_blocked[planIdx]; }

    // Reserve by name (CDU, dataref, gate window Enter): exact part 1 match.
    // Returns the confirmation ("Gate B12 reserved"), or "" if nothing new.
    std::string reserve(const std::string& entry, const char* source, double now);
    // Reserve given stands (indices into allStands(); gate window rows).
    std::string reserveStands(const std::vector<std::size_t>& stands, const std::string& label,
                              const char* source, double now);
    // Release one stand (index into the full stand list) / all.
    void releaseOne(std::size_t stand, double now);
    void releaseAll(const char* why, double now);

    bool armed() const { return m_arm.armed(); }

    // ── For the gate window ──────────────────────────────────────────────────
    // Reserving possible right now? If not, `why` says so.
    bool available(std::string* why = nullptr) const;
    const std::vector<Stand>&       allStands() const { return m_allStands; }
    const std::vector<std::string>& allNames()  const { return m_allNames; }
    const std::vector<std::string>& normNames() const { return m_normNames; }
    const std::set<std::size_t>&    reserved()  const { return m_set.stands(); }
    // Reserved / live traffic / static parked / free, for stand i of allStands().
    int  statusOf(std::size_t i) const;   // GateSuggest::Status as int
    // Metres from the user's aircraft to stand i.
    double distanceTo(std::size_t i) const;
    // The user's aircraft size code (A–F) from its ICAO type; 'C' if unknown.
    char userSizeCode() const;
    // Latest live traffic (main.cpp passes its TCAS snapshot every tick).
    void setTraffic(std::vector<GateOccupancy::Traffic> t) { m_traffic = std::move(t); }

private:
    static float cduLoopCB(float, float, int, void*);
    static int   arrivalGet(void*, void*, int, int);
    static void  arrivalSet(void*, void*, int, int);

    void findRefs();
    void applyBlocks(double now);
    void chooseAdapter();
    std::string readCdu() const;
    StandLayout::Shape userShape() const;
    bool userAtStand(const Stand& s) const;

    Reservations::ArmState    m_arm;
    Reservations::Set         m_set;
    Reservations::StableText  m_stable;
    std::vector<Stand>        m_allStands;      // active airport, every stand
    std::vector<std::string>  m_allNames;
    std::vector<std::string>  m_normNames;      // Reservations::normalizeStand, per stand
    std::vector<long>         m_planIndex;      // all stand → plan index, -1 if none
    std::vector<bool>         m_trafficAt;      // all stand → live traffic occupies it
    std::vector<GateOccupancy::Traffic> m_traffic;
    StaticRenderer*           m_renderer = nullptr;
    std::vector<float>*       m_cooldownUntil = nullptr;   // main.cpp's, set every tick
    float                     m_cooldownSec = 0.0f;
    std::vector<StandLayout::Shape> m_planShapes;
    std::vector<bool>         m_blocked;        // per plan stand
    std::vector<bool>         m_clearedByUs;    // per plan stand: suppressed for a reservation
    double                    m_refLat = 0, m_refLon = 0;

    std::string                        m_adaptersFile;
    std::vector<CduAdapters::Adapter>  m_adapters;
    const CduAdapters::Adapter*        m_adapter = nullptr;
    XPLMDataRef                        m_cduRef = nullptr;
    std::vector<XPLMDataRef>           m_cduLines;   // "{n}" adapters: every screen line
    bool                               m_adapterChosen = false;

    XPLMDataRef      m_arrivalRef = nullptr;
    std::string      m_arrivalText;

    XPLMFlightLoopID m_cduLoop = nullptr;
    Toast            m_toast;
    bool             m_needRecompute = false;
};
