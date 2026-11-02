#pragma once
#include "core/Timing.h"
// ImGuiWindow — a Dear ImGui context living in one modern X-Plane window.
// Our own thin integration (no ImgWindow/LiveTraffic code), XPLM303 level so
// it runs in X-Plane 11 and 12.
//
// SDK calls (vendor/XPSDK/CHeaders/XPLM):
//   XPLMCreateWindowEx, XPLMDestroyWindow, XPLMGet/SetWindowGeometry,
//   XPLMGet/SetWindowIsVisible                         XPLMDisplay.h  XPLM200
//   XPLMSetWindowTitle, XPLMSetWindowResizingLimits,
//   XPLMSetWindowPositioningMode, XPLMGetScreenBoundsGlobal          XPLM300
//   xplm_WindowVR, XPLMSet/GetWindowGeometryVR, XPLMWindowIsInVR,
//   xplm_WindowDecorationRoundRectangle                              XPLM301
//   XPLMSetGraphicsState, XPLMBindTexture2d,
//   XPLMGenerateTextureNumbers                        XPLMGraphics.h XPLM200
//
// Coordinates: the draw and mouse callbacks of a modern window use the same
// boxel space as XPLMGetWindowGeometry (XPLMDisplay.h, XPLMCreateWindow_t).
// Every frame and every mouse event re-reads the geometry and converts to
// ImGui's window-local, y-down space, so the same code works on the desktop
// and in VR. Hover comes from the cursor callback, not a global mouse query.
//
// Rendering: fixed-function OpenGL (vertex arrays), in X-Plane's current
// matrices; scissor rects are mapped to pixels through those matrices.
// The window draws nothing while hidden. ImGui's ini file is disabled.

#include <XPLMDisplay.h>

#include "ui/Theme.h"

struct ImGuiContext;
struct ImDrawData;

class ImGuiWindow {
public:
    // Time spent drawing XP-GSI windows, every window together (core/Timing.h).
    static TickStats& drawStats() { static TickStats s; return s; }

    ImGuiWindow()          = default;
    virtual ~ImGuiWindow() { destroy(); }
    ImGuiWindow(const ImGuiWindow&)            = delete;
    ImGuiWindow& operator=(const ImGuiWindow&) = delete;

    // Create hidden. Geometry in global desktop boxels; pass right <= left
    // for defaultW × defaultH near the top-right of the main screen.
    void create(const char* title, int left, int top, int right, int bottom,
                int minW, int minH, int maxW, int maxH, int defaultW = 400, int defaultH = 360);
    void destroy();

    bool isCreated() const { return m_win != nullptr; }
    void setVisible(bool v);
    bool isVisible() const;
    void toggleVisible() { setVisible(!isVisible()); }

    // Desktop geometry (left/top/right/bottom). While in VR, returns the last
    // desktop geometry so it can still be saved.
    void getDesktopGeometry(int& l, int& t, int& r, int& b) const;

    // Move the window into / out of the VR headset (XPLM_MSG_ENTERED_VR /
    // XPLM_MSG_EXITING_VR, or sim/graphics/VR/enabled at start).
    void setVr(bool inVr);
    bool inVr() const { return m_inVr; }

    // Keyboard (opt-in; the main window never takes it). While a window holds
    // X-Plane's keyboard focus every key goes to it and the sim's own key
    // bindings pause. XPLMTakeKeyboardFocus / XPLMHasKeyboardFocus
    // (XPLMDisplay.h, XPLM200); 0 returns focus to X-Plane.
    void takeKeyboard();
    void releaseKeyboard();
    bool hasKeyboard() const;

protected:
    // Build the UI with ImGui calls; the ImGui window filling the X-Plane
    // window is already begun.
    virtual void buildUi() = 0;

    // A key while this window has focus (`vkey`: XPLM_VK_*, XPLMDefs.h).
    // Return true if handled; otherwise characters and editing keys go to
    // ImGui's focused text field.
    virtual bool onKey(char /*key*/, XPLMKeyFlags /*flags*/, unsigned char /*vkey*/) { return false; }
    // Focus taken away (the user clicked elsewhere).
    virtual void onKeyboardLost() {}

    Theme::Fonts m_fonts;   // body font is pushed around buildUi()

private:
    static void             drawCB      (XPLMWindowID, void* refcon);
    static int              mouseCB     (XPLMWindowID, int x, int y, XPLMMouseStatus, void*);
    static int              rightClickCB(XPLMWindowID, int x, int y, XPLMMouseStatus, void*);
    static void             keyCB       (XPLMWindowID, char, XPLMKeyFlags, char, void*, int);
    static XPLMCursorStatus cursorCB    (XPLMWindowID, int x, int y, void*);
    static int              wheelCB     (XPLMWindowID, int x, int y, int wheel, int clicks, void*);

    void frame();
    void render(ImDrawData* dd, int left, int top);
    void mouseButton(int button, int x, int y, XPLMMouseStatus status);
    void toLocal(int x, int y, float& lx, float& ly) const;
    void ensureFontTexture();

    XPLMWindowID  m_win      = nullptr;
    ImGuiContext* m_ctx      = nullptr;
    int           m_fontTex  = 0;
    double        m_lastTime = 0.0;
    bool          m_hovered  = false;   // cursor callback fired since the last frame
    bool          m_autoKeyboard = false;   // taken because a text box is being edited
    bool          m_keyboardLost = false;   // X-Plane took the keyboard: end text editing
    bool          m_inVr     = false;
    int           m_desk[4]  = {0, 0, 0, 0};   // last desktop l, t, r, b
};
