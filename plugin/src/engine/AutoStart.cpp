#include "engine/AutoStart.h"

void AutoStart::onFlightLoading(double now) {
    m_flightLoaded = true;
    m_lastLoadAt   = now;
    m_suppressed   = false;
}

void AutoStart::onSceneryLoaded(double now) {
    if (m_flightLoaded) m_lastLoadAt = now;
}

bool AutoStart::flightReady(double now) const {
    return m_flightLoaded && now - m_lastLoadAt >= kSettleSec;
}

AutoStart::Action AutoStart::tick(StartMode mode, bool running, double now, int vatsimStatus) {
    const int prevVatsim = m_lastVatsim;
    m_lastVatsim = vatsimStatus;

    switch (mode) {
        case StartMode::Manual:
            return Action::None;

        case StartMode::OnSimStart:
            if (!running && !m_suppressed && flightReady(now))
                return Action::Start;
            return Action::None;

        case StartMode::VatsimConnected: {
            const bool connected = vatsimStatus == kXpilotPilot;
            // Disconnect (pilot → anything else) stops, whoever started us.
            if (running && prevVatsim == kXpilotPilot && !connected)
                return Action::Stop;
            if (!running && connected && !m_suppressed && flightReady(now))
                return Action::Start;
            return Action::None;
        }
    }
    return Action::None;
}
