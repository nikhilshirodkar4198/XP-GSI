#include "ui/MainWindow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "ui/Theme.h"

namespace {
constexpr float kFieldW   = 110.0f;   // Advanced value boxes
constexpr const char* kProjectPage = "Project page: github.com/nikhilshirodkar4198/XP-GSI";
constexpr float kReserveW = 130.0f;   // "Reserve gate"

// Lets the next item sit at the right end of the current line, `w` wide.
void alignRight(float w) {
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
}
} // namespace

void MainWindow::buildUi() {
    if (!m_hooks.model) return;
    const Model m = m_hooks.model();

    header(m);
    Theme::gap(Theme::kGap);
    bannerBoxes(m);

    // ── Start / Stop ─────────────────────────────────────────────────────────
    if (Theme::button(m.running ? "Stop" : "Start", !m.running, -1.0f, Theme::kPrimaryH, m_fonts.medium) &&
        m_hooks.startStop)
        m_hooks.startStop();
    Theme::gap(Theme::kGap);

    density(m);
    Theme::gap(Theme::kGap);

    // ── Startup mode ─────────────────────────────────────────────────────────
    static const char* const kLabels[3] = {"Auto", "Manual", "VATSIM"};
    static const StartMode   kModes[3]  = {StartMode::OnSimStart, StartMode::Manual,
                                           StartMode::VatsimConnected};
    const std::string vatsimTip = m.vatsimTooltip.empty()
        ? std::string("Starts when xPilot connects to VATSIM as a pilot; stops when it disconnects.")
        : m.vatsimTooltip;
    const char* tips[3] = {"Starts for every flight once it has loaded and scenery has settled.",
                           "XP-GSI only runs when you press Start.",
                           vatsimTip.c_str()};
    const bool enabled[3] = {true, true, m.vatsimAvailable};
    int sel = 0;
    for (int i = 0; i < 3; ++i) if (kModes[i] == m.mode) sel = i;
    const int clicked = Theme::segmented("##mode", kLabels, 3, sel, enabled, tips);
    if (clicked >= 0 && m_hooks.setMode) m_hooks.setMode(kModes[clicked]);
    Theme::gap(16.0f);

    // ── Advanced toggle · Reserve gate ───────────────────────────────────────
    Theme::disclosure("Advanced", m_advanced);
    alignRight(kReserveW);
    if (Theme::outlineButton("Reserve gate", kReserveW, !m.gateAvailable) && m_hooks.openGateWindow)
        m_hooks.openGateWindow();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m.gateAvailable
                              ? "Type the gate you will taxi to (also xpgsi/gate_window)"
                              : "Available near an airport below 10,000 ft (also xpgsi/gate_window)");

    if (m_advanced) advanced(m);
}

// ── Header: status dot, title, airport summary ───────────────────────────────
void MainWindow::header(const Model& m) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  w = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(m_fonts.title);
    const float th = ImGui::GetTextLineHeight();
    Theme::dot(ImVec2(p.x + 5.0f, p.y + th * 0.5f), 5.0f, m.running ? Theme::kOn : Theme::kOff);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 18.0f, p.y));
    ImGui::TextUnformatted("XP-GSI");
    ImGui::PopFont();

    const float bh = ImGui::GetTextLineHeight();
    const float sw = ImGui::CalcTextSize(m.summary.c_str()).x;
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - sw, p.y + (th - bh) * 0.5f));
    Theme::text(m.summary.c_str(), Theme::kText2);
    if (ImGui::IsItemHovered() && !m.statusLine.empty()) ImGui::SetTooltip("%s", m.statusLine.c_str());

    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, th));
}

// ── Problem banners (one box each, dismissible for the session) ──────────────
void MainWindow::bannerBoxes(const Model& m) {
    bool any = false;
    for (const auto& b : m.banners) {
        if (m_dismissed.count(b)) continue;
        any = true;
        const ImVec2 p  = ImGui::GetCursorScreenPos();
        const float  w  = ImGui::GetContentRegionAvail().x;
        const float  in = 12.0f, icon = 16.0f;
        const float  textX = p.x + in + icon + 10.0f;
        const float  wrapW = (p.x + w - in - icon - 8.0f) - textX;
        const float  tH = ImGui::CalcTextSize(b.c_str(), nullptr, false, wrapW).y;
        const float  h  = std::max(tH, icon) + 2.0f * in;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Theme::kField, Theme::kFrameRound);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), Theme::kBorder, Theme::kFrameRound, 0, 1.0f);
        Theme::warningTriangle(ImVec2(p.x + in, p.y + in), icon, Theme::kOff);

        ImGui::SetCursorScreenPos(ImVec2(textX, p.y + in));
        ImGui::PushTextWrapPos(textX + wrapW);
        ImGui::TextUnformatted(b.c_str());
        ImGui::PopTextWrapPos();

        ImGui::SetCursorScreenPos(ImVec2(p.x + w - in - icon, p.y + in));
        ImGui::PushID(b.c_str());
        if (Theme::closeIcon("##dismiss", icon)) m_dismissed.insert(b);
        ImGui::PopID();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dismiss");

        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
    }
    if (any) Theme::gap(16.0f);
}

// ── Passenger and cargo density for the active airport ───────────────────────
void MainWindow::density(const Model& m) {
    std::string cat = Density::name(m.category);
    if (!cat.empty()) cat[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cat[0])));
    densityRow(Density::Kind::Passenger, "Passenger", m.hasAirport ? cat : std::string{}, m.density,
               m.densityAuto, m.densityCustom, m.hasAirport,
               "Share of the passenger stands filled at this airport.\n"
               "Automatic from its daily airline traffic; a change is kept for this airport only.");
    Theme::gap(16.0f);
    char stands[32] = "";
    if (m.hasAirport) std::snprintf(stands, sizeof(stands), "%zu stands", m.cargoStands);
    densityRow(Density::Kind::Cargo, "Cargo", stands, m.cargoDensity, m.cargoDensityAuto,
               m.cargoDensityCustom, m.hasAirport && m.cargo && m.cargoStands > 0,
               "Share of the cargo stands filled at this airport.\n"
               "Automatic from its daily cargo flights; a change is kept for this airport only.");
    bool cargo = m.cargo;
    if (Theme::checkbox("Cargo aircraft on cargo aprons", cargo) && m_hooks.setCargo)
        m_hooks.setCargo(cargo);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fill the scenery's cargo stands with the airport's real cargo airlines.\n"
                          "Turn off if you don't want cargo aircraft, or if the scenery already\n"
                          "has its own static freighters there. Passenger aircraft stay as they are.");
}

void MainWindow::densityRow(Density::Kind kind, const char* label, const std::string& sub,
                            float value, float automatic, bool custom, bool active, const char* tip) {
    const int lo  = static_cast<int>(std::lround(Density::kMin * 100.0f));
    const int hi  = static_cast<int>(std::lround(Density::kMax * 100.0f));
    const int pct = static_cast<int>(std::lround(value * 100.0f));
    ImGui::PushID(label);

    Theme::text(label, active ? Theme::kText : Theme::kMuted);
    if (!sub.empty()) {
        ImGui::SameLine(0.0f, 0.0f);
        Theme::text((" \xC2\xB7 " + sub).c_str(), Theme::kText2);
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d%%", pct);
    const char* tag = custom ? "" : "auto  ";
    const float iconW = 20.0f, sp = 6.0f;
    alignRight(ImGui::CalcTextSize(tag).x + ImGui::CalcTextSize(buf).x + sp + iconW);
    if (*tag) {
        Theme::text(tag, Theme::kMuted);
        ImGui::SameLine(0.0f, 0.0f);
    }
    Theme::text(buf, active ? Theme::kText : Theme::kMuted);
    ImGui::SameLine(0.0f, sp);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0f);
    if (Theme::resetIcon("##reset") && custom && m_hooks.resetDensity) m_hooks.resetDensity(kind);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(custom ? "Back to automatic for this airport (%d%%)" : "Automatic (%d%%)",
                          static_cast<int>(std::lround(automatic * 100.0f)));

    ImGui::BeginDisabled(!active);
    float t = hi > lo ? static_cast<float>(pct - lo) / static_cast<float>(hi - lo) : 0.0f;
    if (Theme::slider("##density", t, active ? Theme::kBlue : Theme::kMuted) && m_hooks.setDensity) {
        const int v = static_cast<int>(std::lround(static_cast<float>(lo) + t * static_cast<float>(hi - lo)));
        if (v != pct) m_hooks.setDensity(kind, static_cast<float>(v) / 100.0f);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
}

// ── Advanced ──────────────────────────────────────────────────────────────────
void MainWindow::advanced(const Model& m) {
    Theme::gap(Theme::kGap);

    // Aircraft limit: far left = none (default).
    char buf[32];
    if (m.maxAircraft < 0) std::snprintf(buf, sizeof(buf), "No limit");
    else                   std::snprintf(buf, sizeof(buf), "%d", m.maxAircraft);
    Theme::text("Max aircraft", Theme::kText);
    Theme::rightText(buf, Theme::kText);
    const float maxN = static_cast<float>(Settings::kAircraftLimitMax);
    float t = m.maxAircraft < 0 ? 0.0f : static_cast<float>(m.maxAircraft) / maxN;
    if (Theme::slider("##maxac", t, Theme::kMuted) && m_hooks.setMaxAircraft) {
        int n = static_cast<int>(std::lround(t * maxN));
        if (n <= 0) n = -1;
        if (n != m.maxAircraft) m_hooks.setMaxAircraft(n);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Optional performance cap on statics at one airport. Far left = no limit.");
    Theme::gap(16.0f);

    float radius = m.conflictRadiusM, cool = m.cooldownSec, dist = m.maxRenderDistKm;
    bool changed = false;
    auto row = [&](const char* label, const char* id, float& v, float lo, float hi, float perPx,
                   const char* text, const char* tip) {
        ImGui::AlignTextToFramePadding();
        Theme::text(label, Theme::kText);
        alignRight(kFieldW);
        changed |= Theme::valueField(id, v, lo, hi, perPx, text, kFieldW);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDrag left / right, or click to type a value.", tip);
    };
    if (cool < 0.5f) std::snprintf(buf, sizeof(buf), "Off");
    else             std::snprintf(buf, sizeof(buf), "%.0f s", static_cast<double>(cool));
    row("Restore cooldown", "##cool", cool, 0.0f, 900.0f, 2.0f, buf,
        "Off (default): a stand you or other traffic left stays empty.\n"
        "Otherwise a static returns this long after the stand is free.");
    if (dist < 0.5f) std::snprintf(buf, sizeof(buf), "No limit");
    else             std::snprintf(buf, sizeof(buf), "%.0f km", static_cast<double>(dist));
    row("Draw distance", "##dist", dist, 0.0f, 50.0f, 0.1f, buf,
        "Statics farther than this are not drawn. Far left = no limit.");
    if (changed && m_hooks.setConflict) m_hooks.setConflict(radius, cool, dist);

    // Layout seed: ticking keeps the current layout for every flight.
    const float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y + (Theme::kControlH - ImGui::GetTextLineHeight()) * 0.5f);
    bool fixed = m.seedFixed;
    if (Theme::checkbox("Fixed seed", fixed) && m_hooks.setSeedFixed) m_hooks.setSeedFixed(fixed);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(m.seedFixed ? "The same layout every time (fixed_seed in settings.json)."
                                      : "A new layout for every placement. Tick to keep this one.");
    alignRight(kFieldW);
    ImGui::SetCursorPosY(y);
    float seed = 0.0f;
    std::snprintf(buf, sizeof(buf), "%u", m.sessionSeed);
    Theme::valueField("##seed", seed, 0.0f, 0.0f, 0.0f, buf, kFieldW);

    bool dbg = m.debugLog;
    if (Theme::checkbox("Debug logging", dbg) && m_hooks.setDebugLog) m_hooks.setDebugLog(dbg);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("More detail in XP-GSI.log (timings, every decision). For bug reports.");

    if (!m.footer.empty()) {
        Theme::gap(Theme::kGap);
        Theme::text(m.footer.c_str(), Theme::kMuted);
        Theme::text(kProjectPage, Theme::kMuted);
    }
}
