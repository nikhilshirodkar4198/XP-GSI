#pragma once
// MainWindow — XP-GSI's single small window (layout: design artboard
// XP-GSI.pdf, 2026-10-27):
//   header (status dot, title, airport summary) · dismissible banners ·
//   Start/Stop · passenger and cargo density for the active airport + reset ·
//   cargo option ·
//   Auto / Manual / VATSIM · Advanced toggle + Reserve gate · Advanced.
// It holds no settings of its own: every frame it asks main.cpp for a Model
// and reports user changes through Hooks, so values never lag behind settings.
// Only view state (Advanced open, dismissed banners) lives here.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/Settings.h"
#include "engine/Density.h"
#include "ui/ImGuiWindow.h"

class MainWindow final : public ImGuiWindow {
public:
    struct Model {
        std::vector<std::string> banners;
        std::string              summary;      // header: "KLAX · 42 parked"
        std::string              statusLine;   // header tooltip (full status)
        bool                     running = false;
        SizeCategory             category = SizeCategory::Standard;  // active airport's, or Standard
        bool                     hasAirport = false;
        // The active airport's densities (Density.h): in use, automatic, and
        // whether the user set it for this airport.
        float                    density = 0.5f, densityAuto = 0.5f;
        bool                     densityCustom = false;
        float                    cargoDensity = 0.05f, cargoDensityAuto = 0.05f;
        bool                     cargoDensityCustom = false;
        std::size_t              cargoStands = 0;                 // in the active plan
        bool                     cargo = true;                    // cargo aircraft on cargo stands
        StartMode                mode = StartMode::OnSimStart;
        bool                     vatsimAvailable = true;          // option enabled
        std::string              vatsimTooltip;
        bool                     gateAvailable = false;           // gate reservation possible now
        int                      maxAircraft = 60;
        float                    conflictRadiusM = 60.0f;
        float                    cooldownSec = 120.0f;
        float                    maxRenderDistKm = 15.0f;
        std::uint32_t            sessionSeed = 0;
        bool                     seedFixed = false;
        bool                     debugLog = false;
        std::string              footer;       // About: "XP-GSI 0.9.0-beta · data 2026-10-29"
    };
    struct Hooks {
        std::function<Model()>                     model;
        std::function<void()>                      startStop;
        std::function<void(Density::Kind, float)>  setDensity;     // for the active airport
        std::function<void(Density::Kind)>         resetDensity;   // back to automatic
        std::function<void(StartMode)>             setMode;
        std::function<void(bool)>                  setCargo;
        std::function<void(int)>                   setMaxAircraft;
        std::function<void(float, float, float)>   setConflict;   // radius m, cooldown s, render km
        std::function<void()>                      openGateWindow; // arrival gate reservation
        std::function<void(bool)>                  setSeedFixed;   // true = keep the current seed
        std::function<void(bool)>                  setDebugLog;
    };

    void setHooks(Hooks h) { m_hooks = std::move(h); }

protected:
    void buildUi() override;

private:
    void header(const Model& m);
    void bannerBoxes(const Model& m);
    void density(const Model& m);
    void densityRow(Density::Kind kind, const char* label, const std::string& sub,
                    float value, float automatic, bool custom, bool active, const char* tip);
    void advanced(const Model& m);

    Hooks                 m_hooks;
    bool                  m_advanced = false;
    std::set<std::string> m_dismissed;   // banners closed this session
};
