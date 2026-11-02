#include "ui/UiText.h"

namespace UiText {

namespace {
// U+00B7 MIDDLE DOT, UTF-8. In the default ImGui font (Latin-1 range).
const std::string kSep = " \xC2\xB7 ";
}

std::string statusLine(const Status& s) {
    std::string out = s.running ? "Running" : "Stopped";
    if (s.running && s.icao.empty() && !s.preparing.empty()) return out + kSep + s.preparing;
    if (s.running && s.waitingForFlight && s.icao.empty())
        return out + kSep + "waiting for the flight to finish loading";
    if (!s.icao.empty()) {
        out += kSep + s.icao;
        if (!s.category.empty()) out += " (" + s.category + ")";
    }
    if (s.running && !s.icao.empty()) {
        out += kSep + std::to_string(s.parked)  + " parked";
        out += kSep + std::to_string(s.cleared) + " cleared";
        if (s.loading > 0) out += kSep + "loading " + std::to_string(s.loading);
    }
    return out;
}

std::string summary(const Status& s) {
    if (s.icao.empty() && s.running && !s.preparing.empty()) return "preparing";
    if (s.icao.empty()) return s.running ? "waiting for flight" : "stopped";
    if (!s.running) return s.icao + kSep + "stopped";
    if (s.parked == 0 && s.loading > 0) return s.icao + kSep + "loading " + std::to_string(s.loading);
    return s.icao + kSep + std::to_string(s.parked) + " parked";
}

std::vector<std::string> banners(const Problems& p) {
    std::vector<std::string> b;
    if (p.xplaneTooOld)
        b.push_back("X-Plane 11.50 or newer is required: update X-Plane to use XP-GSI.");
    if (p.cslScanDone && p.cslModels == 0)
        b.push_back("No CSL aircraft models found: put a CSL library (e.g. xPilot's X-CSL) in "
                    "this X-Plane's Custom Data/CSL folder so XP-GSI has aircraft to park.");
    if (p.noEligibleStands)
        b.push_back((p.airportIcao.empty() ? std::string("This airport") : p.airportIcao) +
                    " has no airline stands in your scenery: XP-GSI only fills gate and "
                    "tie-down ramp starts marked for airlines.");
    if (p.vatsimMode && p.xpilotMissing)
        b.push_back("xPilot not found: 'Start when VATSIM connects' needs the xPilot "
                    "plugin installed, or choose another startup mode.");
    return b;
}

} // namespace UiText
