#pragma once
// AutoStart — decides when XP-GSI starts/stops on its own (Phase 8 UI spec).
//
//  * Manual:          never starts or stops by itself.
//  * OnSimStart:      starts for every flight once it has finished loading:
//                     after XPLM_MSG_PLANE_LOADED(user) / XPLM_MSG_AIRPORT_LOADED,
//                     and kSettleSec after the last XPLM_MSG_SCENERY_LOADED.
//                     ("Sim start" is a loaded flight, not the main menu: the
//                     flight loop that drives tick() does not run at the menu.)
//  * VatsimConnected: starts when xPilot reports a pilot connection
//                     (xpilot/login/status == 1) and the flight is ready; stops
//                     (full cleanup) when the status leaves 1.
//  Both automatic modes: a manual Stop suppresses auto-start until the next
//  flight loads. (Other AI-traffic plugins and X-Plane's own parked aircraft
//  are not detected: users are expected not to run them alongside XP-GSI.)
//
// Pure logic, no XPLM calls: main.cpp feeds it events and the clock.

#include "core/Settings.h"

class AutoStart {
public:
    static constexpr double kSettleSec = 5.0;
    // xpilot/login/status (xPilot plugin/src/xpilot.cpp: 0 = disconnected,
    // 1 = pilot, 2 = observer). Only a pilot connection counts.
    static constexpr int kXpilotPilot = 1;

    enum class Action { None, Start, Stop };

    // A new flight is loading (user plane loaded / positioned at an airport).
    // Clears the manual-Stop suppression and resets the settle timer.
    void onFlightLoading(double now);
    // New scenery finished loading: restarts the settle timer.
    void onSceneryLoaded(double now);
    void onManualStop()  { m_suppressed = true; }
    void onManualStart() { m_suppressed = false; }

    // True once a flight is loaded and scenery has been quiet for kSettleSec.
    bool flightReady(double now) const;
    bool flightLoaded() const { return m_flightLoaded; }
    bool suppressed()  const { return m_suppressed; }

    // vatsimStatus: xpilot/login/status, or -1 when xPilot / the dataref is absent.
    Action tick(StartMode mode, bool running, double now, int vatsimStatus);

private:
    bool   m_flightLoaded = false;
    double m_lastLoadAt   = 0.0;
    bool   m_suppressed   = false;
    int    m_lastVatsim   = -1;
};
