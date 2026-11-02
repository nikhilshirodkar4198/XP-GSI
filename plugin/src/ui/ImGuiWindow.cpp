#include "ui/ImGuiWindow.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>

#if IBM
#  include <windows.h>
#endif
#if APL
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#include <XPLMDefs.h>
#include <XPLMGraphics.h>
#include <XPLMProcessing.h>

#include <imgui.h>

#include "core/Guard.h"
#include "core/Timing.h"
#include "ui/Theme.h"

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ImGuiWindow::create(const char* title, int left, int top, int right, int bottom,
                         int minW, int minH, int maxW, int maxH, int defaultW, int defaultH) {
    if (m_win) return;

    if (right <= left || top <= bottom) {
        // XPLMGetScreenBoundsGlobal (XPLM300): main screen, global boxels.
        int sl = 0, st = 0, sr = 0, sb = 0;
        XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
        const int w = defaultW, h = defaultH;
        right = sr - 60;  left = right - w;
        top   = st - 120; bottom = top - h;
    }

    XPLMCreateWindow_t p{};
    p.structSize               = static_cast<int>(sizeof(p));
    p.left = left; p.top = top; p.right = right; p.bottom = bottom;
    p.visible                  = 0;
    // Every callback behind the exception barrier (core/Guard.h).
    p.drawWindowFunc           = [](XPLMWindowID w, void* r) {
        const ScopedTick timed(drawStats());
        Guard::run("Window draw", [&] { drawCB(w, r); });
    };
    p.handleMouseClickFunc     = [](XPLMWindowID w, int x, int y, XPLMMouseStatus s, void* r) {
        return Guard::run("Window click", 1, [&] { return mouseCB(w, x, y, s, r); }); };
    p.handleRightClickFunc     = [](XPLMWindowID w, int x, int y, XPLMMouseStatus s, void* r) {
        return Guard::run("Window click", 1, [&] { return rightClickCB(w, x, y, s, r); }); };
    p.handleKeyFunc            = [](XPLMWindowID w, char k, XPLMKeyFlags f, char v, void* r, int l) {
        Guard::run("Window key", [&] { keyCB(w, k, f, v, r, l); }); };
    p.handleCursorFunc         = [](XPLMWindowID w, int x, int y, void* r) {
        return Guard::run("Window cursor", xplm_CursorDefault, [&] { return cursorCB(w, x, y, r); }); };
    p.handleMouseWheelFunc     = [](XPLMWindowID w, int x, int y, int wh, int c, void* r) {
        return Guard::run("Window wheel", 1, [&] { return wheelCB(w, x, y, wh, c, r); }); };
    p.refcon                   = this;
    p.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;  // XPLM301
    p.layer                    = xplm_WindowLayerFloatingWindows;      // XPLM300
    m_win = XPLMCreateWindowEx(&p);
    if (!m_win) return;

    XPLMSetWindowTitle(m_win, title);                                   // XPLM300
    XPLMSetWindowResizingLimits(m_win, minW, minH, maxW, maxH);         // XPLM300
    m_desk[0] = left; m_desk[1] = top; m_desk[2] = right; m_desk[3] = bottom;

    IMGUI_CHECKVERSION();
    ImGuiContext* prev = ImGui::GetCurrentContext();
    m_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename  = nullptr;   // never write imgui.ini anywhere
    io.LogFilename  = nullptr;
    io.BackendPlatformName = "xpgsi_xplm";
    io.BackendRendererName = "xpgsi_gl_fixed";
    m_fonts = Theme::setup();   // fonts into this context's atlas, then the style
    ImGui::SetCurrentContext(prev);
}

void ImGuiWindow::destroy() {
    releaseKeyboard();
    if (m_ctx) {
        ImGui::DestroyContext(m_ctx);
        m_ctx = nullptr;
    }
    if (m_fontTex) {
        GLuint id = static_cast<GLuint>(m_fontTex);
        glDeleteTextures(1, &id);
        m_fontTex = 0;
    }
    if (m_win) {
        XPLMDestroyWindow(m_win);
        m_win = nullptr;
    }
}

void ImGuiWindow::setVisible(bool v) {
    if (!v) releaseKeyboard();   // never keep the sim's keys once hidden
    if (m_win) XPLMSetWindowIsVisible(m_win, v ? 1 : 0);
}

bool ImGuiWindow::isVisible() const {
    return m_win && XPLMGetWindowIsVisible(m_win) != 0;
}

void ImGuiWindow::getDesktopGeometry(int& l, int& t, int& r, int& b) const {
    if (m_win && !m_inVr) {
        XPLMGetWindowGeometry(m_win, &l, &t, &r, &b);
        return;
    }
    l = m_desk[0]; t = m_desk[1]; r = m_desk[2]; b = m_desk[3];
}

void ImGuiWindow::setVr(bool vr) {
    if (!m_win || vr == m_inVr) return;
    if (vr) {
        XPLMGetWindowGeometry(m_win, &m_desk[0], &m_desk[1], &m_desk[2], &m_desk[3]);
        // xplm_WindowVR (XPLM301): "A floating window visible on the VR headset".
        XPLMSetWindowPositioningMode(m_win, xplm_WindowVR, -1);
        XPLMSetWindowGeometryVR(m_win, m_desk[2] - m_desk[0], m_desk[1] - m_desk[3]);
    } else {
        XPLMSetWindowPositioningMode(m_win, xplm_WindowPositionFree, -1);
        XPLMSetWindowGeometry(m_win, m_desk[0], m_desk[1], m_desk[2], m_desk[3]);
    }
    m_inVr = vr;
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void ImGuiWindow::drawCB(XPLMWindowID, void* refcon) {
    static_cast<ImGuiWindow*>(refcon)->frame();
}

int ImGuiWindow::mouseCB(XPLMWindowID, int x, int y, XPLMMouseStatus s, void* refcon) {
    static_cast<ImGuiWindow*>(refcon)->mouseButton(0, x, y, s);
    return 1;
}

int ImGuiWindow::rightClickCB(XPLMWindowID, int x, int y, XPLMMouseStatus s, void* refcon) {
    static_cast<ImGuiWindow*>(refcon)->mouseButton(1, x, y, s);
    return 1;
}

void ImGuiWindow::takeKeyboard() {
    if (m_win) XPLMTakeKeyboardFocus(m_win);
}

void ImGuiWindow::releaseKeyboard() {
    if (m_win && XPLMHasKeyboardFocus(m_win)) XPLMTakeKeyboardFocus(nullptr);
}

bool ImGuiWindow::hasKeyboard() const {
    return m_win && XPLMHasKeyboardFocus(m_win) != 0;
}

// XPLMHandleKey_f (XPLMDisplay.h): key = ASCII, flags = xplm_*Flag (XPLMDefs.h),
// vkey = XPLM_VK_*, losingFocus = 1 when focus is being taken away.
void ImGuiWindow::keyCB(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void* refcon,
                        int losingFocus) {
    auto* self = static_cast<ImGuiWindow*>(refcon);
    if (losingFocus) {
        self->m_autoKeyboard = false;
        self->m_keyboardLost = true;
        self->onKeyboardLost();
        return;
    }
    const auto vk = static_cast<unsigned char>(vkey);
    if (self->onKey(key, flags, vk) || !self->m_ctx) return;

    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self->m_ctx);
    ImGuiIO& io = ImGui::GetIO();
    const bool down = (flags & xplm_DownFlag) != 0;
    const bool up   = (flags & xplm_UpFlag) != 0;
    ImGuiKey k = ImGuiKey_None;
    switch (vk) {
        case XPLM_VK_BACK:   k = ImGuiKey_Backspace;  break;
        case XPLM_VK_DELETE: k = ImGuiKey_Delete;     break;
        case XPLM_VK_LEFT:   k = ImGuiKey_LeftArrow;  break;
        case XPLM_VK_RIGHT:  k = ImGuiKey_RightArrow; break;
        case XPLM_VK_HOME:   k = ImGuiKey_Home;       break;
        case XPLM_VK_END:    k = ImGuiKey_End;        break;
        default: break;
    }
    if (k != ImGuiKey_None) {
        if (down) io.AddKeyEvent(k, true);
        if (up || down) io.AddKeyEvent(k, false);   // a repeat arrives as another down
    } else if (down && !(flags & (xplm_ControlFlag | xplm_OptionAltFlag))) {
        const auto c = static_cast<unsigned char>(key);
        if (c >= 0x20 && c < 0x7F) io.AddInputCharacter(c);
    }
    ImGui::SetCurrentContext(prev);
}

XPLMCursorStatus ImGuiWindow::cursorCB(XPLMWindowID, int x, int y, void* refcon) {
    auto* self = static_cast<ImGuiWindow*>(refcon);
    if (self->m_ctx) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(self->m_ctx);
        float lx, ly;
        self->toLocal(x, y, lx, ly);
        ImGui::GetIO().AddMousePosEvent(lx, ly);
        ImGui::SetCurrentContext(prev);
        self->m_hovered = true;
    }
    return xplm_CursorDefault;
}

int ImGuiWindow::wheelCB(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon) {
    auto* self = static_cast<ImGuiWindow*>(refcon);
    if (!self->m_ctx) return 1;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self->m_ctx);
    float lx, ly;
    self->toLocal(x, y, lx, ly);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(lx, ly);
    // wheel: 0 = vertical, 1 = horizontal (XPLMHandleMouseWheel_f).
    io.AddMouseWheelEvent(wheel == 1 ? static_cast<float>(clicks) : 0.0f,
                          wheel == 0 ? static_cast<float>(clicks) : 0.0f);
    ImGui::SetCurrentContext(prev);
    return 1;
}

void ImGuiWindow::mouseButton(int button, int x, int y, XPLMMouseStatus status) {
    if (!m_ctx) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    float lx, ly;
    toLocal(x, y, lx, ly);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(lx, ly);
    if (status == xplm_MouseDown) io.AddMouseButtonEvent(button, true);
    else if (status == xplm_MouseUp) io.AddMouseButtonEvent(button, false);
    m_hovered = true;
    ImGui::SetCurrentContext(prev);
}

void ImGuiWindow::toLocal(int x, int y, float& lx, float& ly) const {
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(m_win, &l, &t, &r, &b);
    lx = static_cast<float>(x - l);
    ly = static_cast<float>(t - y);
}

// ── Frame ─────────────────────────────────────────────────────────────────────

void ImGuiWindow::ensureFontTexture() {
    if (m_fontTex) return;
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    // XPLMGenerateTextureNumbers / XPLMBindTexture2d (XPLMGraphics.h): X-Plane
    // owned texture IDs and its bind cache.
    XPLMGenerateTextureNumbers(&m_fontTex, 1);
    XPLMBindTexture2d(m_fontTex, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    // ImTextureID is an integer (ImU64) by default since ImGui 1.91.4.
    io.Fonts->SetTexID(static_cast<ImTextureID>(m_fontTex));
}

void ImGuiWindow::frame() {
    if (!m_ctx) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(m_ctx);
    ImGuiIO& io = ImGui::GetIO();

    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(m_win, &l, &t, &r, &b);
    if (!m_inVr) { m_desk[0] = l; m_desk[1] = t; m_desk[2] = r; m_desk[3] = b; }
    io.DisplaySize = ImVec2(static_cast<float>(std::max(r - l, 1)),
                            static_cast<float>(std::max(t - b, 1)));

    const double now = XPLMGetElapsedTime();
    io.DeltaTime  = (m_lastTime > 0.0 && now > m_lastTime) ? static_cast<float>(now - m_lastTime)
                                                           : 1.0f / 60.0f;
    m_lastTime = now;
    if (!m_hovered) io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    m_hovered = false;

    ensureFontTexture();
    ImGui::NewFrame();
    if (m_keyboardLost) {   // clicked elsewhere: stop editing so the box does not grab the keys back
        m_keyboardLost = false;
        ImGui::SetWindowFocus(nullptr);   // unfocusing ends the active text edit
    }
    ImGui::PushFont(m_fonts.body);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##xpgsi", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    buildUi();
    ImGui::End();
    ImGui::PopFont();
    ImGui::Render();
    // Any text box being edited holds X-Plane's keyboard, so typing goes to
    // the box and not to the sim's key bindings.
    if (io.WantTextInput && !hasKeyboard()) {
        takeKeyboard();
        m_autoKeyboard = true;
    } else if (!io.WantTextInput && m_autoKeyboard) {
        m_autoKeyboard = false;
        releaseKeyboard();
    }
    render(ImGui::GetDrawData(), l, t);
    ImGui::SetCurrentContext(prev);
}

void ImGuiWindow::render(ImDrawData* dd, int left, int top) {
    if (!dd || dd->CmdListsCount == 0) return;

    // fog off, 1 texture unit, no lighting, no alpha test, alpha blend on,
    // no depth test / write (XPLMSetGraphicsState, XPLMGraphics.h).
    XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);

    // ImGui space (window-local, y down) → window boxels.
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(static_cast<float>(left), static_cast<float>(top), 0.0f);
    glScalef(1.0f, -1.0f, 1.0f);

    // Map clip rects to framebuffer pixels through the current matrices.
    GLfloat mv[16], pj[16];
    GLint   vp[4];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    glGetFloatv(GL_PROJECTION_MATRIX, pj);
    glGetIntegerv(GL_VIEWPORT, vp);
    auto toPixels = [&](float x, float y, float& px, float& py) {
        const float cx = mv[0] * x + mv[4] * y + mv[12];
        const float cy = mv[1] * x + mv[5] * y + mv[13];
        const float cw = mv[3] * x + mv[7] * y + mv[15];
        const float ox = pj[0] * cx + pj[4] * cy + pj[12] * cw;
        const float oy = pj[1] * cx + pj[5] * cy + pj[13] * cw;
        const float ow = pj[3] * cx + pj[7] * cy + pj[15] * cw;
        const float nx = ow != 0.0f ? ox / ow : ox, ny = ow != 0.0f ? oy / ow : oy;
        px = static_cast<float>(vp[0]) + (nx * 0.5f + 0.5f) * static_cast<float>(vp[2]);
        py = static_cast<float>(vp[1]) + (ny * 0.5f + 0.5f) * static_cast<float>(vp[3]);
    };
    const GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);
    glEnable(GL_SCISSOR_TEST);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl  = dd->CmdLists[n];
        const ImDrawVert* vtx = cl->VtxBuffer.Data;
        const ImDrawIdx*  idx = cl->IdxBuffer.Data;
        glVertexPointer  (2, GL_FLOAT,         sizeof(ImDrawVert), &vtx->pos);
        glTexCoordPointer(2, GL_FLOAT,         sizeof(ImDrawVert), &vtx->uv);
        glColorPointer   (4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), &vtx->col);

        for (int c = 0; c < cl->CmdBuffer.Size; ++c) {
            const ImDrawCmd* cmd = &cl->CmdBuffer[c];
            if (cmd->UserCallback) { cmd->UserCallback(cl, cmd); continue; }
            float x0, y0, x1, y1;
            toPixels(cmd->ClipRect.x, cmd->ClipRect.y, x0, y0);
            toPixels(cmd->ClipRect.z, cmd->ClipRect.w, x1, y1);
            const GLint   sx = static_cast<GLint>(std::min(x0, x1));
            const GLint   sy = static_cast<GLint>(std::min(y0, y1));
            const GLsizei sw = static_cast<GLsizei>(std::max(x0, x1) - std::min(x0, x1) + 1.0f);
            const GLsizei sh = static_cast<GLsizei>(std::max(y0, y1) - std::min(y0, y1) + 1.0f);
            if (sw <= 0 || sh <= 0) continue;
            glScissor(sx, sy, sw, sh);
            XPLMBindTexture2d(static_cast<int>(cmd->GetTexID()), 0);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd->ElemCount),
                           sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                           idx + cmd->IdxOffset);
        }
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (!scissorWas) glDisable(GL_SCISSOR_TEST);
    glPopMatrix();
    if (cull) glEnable(GL_CULL_FACE);
}
