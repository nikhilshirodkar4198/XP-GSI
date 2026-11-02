#include "ui/Toast.h"

#include "core/Guard.h"

#include <XPLMGraphics.h>

namespace {
int  passClick(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 0; }   // let it through
int  passWheel(XPLMWindowID, int, int, int, int, void*) { return 0; }
XPLMCursorStatus noCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }
void noKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

// Small and discreet, top right below the menu bar.
constexpr int kPadX = 6, kPadY = 3, kTopGap = 34, kRightGap = 12;
} // namespace

void Toast::show(const std::string& text, double now, double seconds) {
    m_text  = text;
    m_until = now + seconds;

    int charH = 12;
    XPLMGetFontDimensions(xplmFont_Proportional, nullptr, &charH, nullptr);
    const int w = static_cast<int>(XPLMMeasureString(xplmFont_Proportional, m_text.c_str(),
                                                     static_cast<int>(m_text.size()))) + 2 * kPadX;
    const int h = charH + 2 * kPadY;
    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
    const int l = right - kRightGap - w;
    const int t = top - kTopGap;

    if (!m_win) {
        XPLMCreateWindow_t p{};
        p.structSize = static_cast<int>(sizeof(p));
        p.left = l; p.top = t; p.right = l + w; p.bottom = t - h;
        p.visible = 1;
        p.drawWindowFunc = [](XPLMWindowID w, void* r) { Guard::run("Toast draw", [&] { draw(w, r); }); };
        p.handleMouseClickFunc = passClick;
        p.handleRightClickFunc = passClick;
        p.handleKeyFunc = noKey;
        p.handleCursorFunc = noCursor;
        p.handleMouseWheelFunc = passWheel;
        p.refcon = this;
        p.decorateAsFloatingWindow = xplm_WindowDecorationNone;
        p.layer = xplm_WindowLayerFloatingWindows;
        m_win = XPLMCreateWindowEx(&p);
    } else {
        XPLMSetWindowGeometry(m_win, l, t, l + w, t - h);
        XPLMSetWindowIsVisible(m_win, 1);
    }
}

void Toast::tick(double now) {
    if (m_win && now >= m_until) XPLMSetWindowIsVisible(m_win, 0);
}

void Toast::destroy() {
    if (m_win) {
        XPLMDestroyWindow(m_win);
        m_win = nullptr;
    }
}

void Toast::draw(XPLMWindowID win, void* refcon) {
    auto* self = static_cast<Toast*>(refcon);
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(win, &l, &t, &r, &b);
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    float grey[3] = {0.85f, 0.85f, 0.85f};
    XPLMDrawString(grey, l + kPadX, b + kPadY + 1, const_cast<char*>(self->m_text.c_str()),
                   nullptr, xplmFont_Proportional);
}
