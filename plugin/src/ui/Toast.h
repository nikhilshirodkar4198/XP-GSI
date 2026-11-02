#pragma once
// Toast — a small non-blocking message near the top of the screen for a few
// seconds ("Gate B12 reserved"). An undecorated XPLM300 window in the
// floating layer that draws a translucent box and one line of text and lets
// every click through.
//
// SDK calls verified against vendor/XPSDK/CHeaders/XPLM:
//   XPLMCreateWindowEx, xplm_WindowDecorationNone, xplm_WindowLayerFloatingWindows,
//   XPLMSetWindowGeometry, XPLMSetWindowIsVisible, XPLMGetScreenBoundsGlobal,
//   XPLMDestroyWindow                                   XPLMDisplay.h (XPLM300)
//   XPLMDrawTranslucentDarkBox, XPLMDrawString, XPLMGetFontDimensions
//                                                       XPLMGraphics.h (XPLM200)
// Main thread only.

#include <string>

#include <XPLMDisplay.h>

class Toast {
public:
    ~Toast() { destroy(); }
    void show(const std::string& text, double now, double seconds = 3.0);
    void tick(double now);       // hides it when its time is up
    void destroy();

private:
    static void draw(XPLMWindowID, void* refcon);
    XPLMWindowID m_win = nullptr;
    std::string  m_text;
    double       m_until = 0;
};
