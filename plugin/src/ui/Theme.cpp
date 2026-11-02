#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Theme {

namespace {

constexpr float kPi = 3.14159265f;

ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// First existing file of `paths`, loaded at `px`; null if none.
ImFont* loadFirst(const std::vector<std::string>& paths, float px) {
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& p : paths) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(p), ec)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), px, &cfg)) return f;
    }
    return nullptr;
}

std::vector<std::string> regularFonts() {
    std::vector<std::string> v;
#if IBM
    const char* win = std::getenv("WINDIR");
    v.push_back(std::string(win ? win : "C:\\Windows") + "\\Fonts\\segoeui.ttf");
#elif APL
    v = {"/System/Library/Fonts/Supplemental/Arial.ttf", "/Library/Fonts/Arial.ttf"};
#else
    v = {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
         "/usr/share/fonts/TTF/DejaVuSans.ttf",
         "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
#endif
    return v;
}

std::vector<std::string> boldFonts() {
    std::vector<std::string> v;
#if IBM
    const char* win = std::getenv("WINDIR");
    v.push_back(std::string(win ? win : "C:\\Windows") + "\\Fonts\\seguisb.ttf");
#elif APL
    v = {"/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/Library/Fonts/Arial Bold.ttf"};
#else
    v = {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
         "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"};
#endif
    return v;
}

} // namespace

Fonts setup() {
    Fonts f;
    f.body = loadFirst(regularFonts(), 14.0f);   // first font added = default
    if (!f.body) f.body = ImGui::GetIO().Fonts->AddFontDefault();
    f.medium = loadFirst(regularFonts(), 16.0f);
    f.title  = loadFirst(boldFonts(), 16.0f);
    if (!f.medium) f.medium = f.body;
    if (!f.title)  f.title  = f.medium;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(kPad, kPad);
    s.WindowRounding    = 0.0f;   // X-Plane's decoration rounds the window
    s.WindowBorderSize  = 0.0f;
    s.FramePadding      = ImVec2(10.0f, 9.0f);   // 14 px text → 32 px controls
    s.ItemSpacing       = ImVec2(8.0f, 10.0f);
    s.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    s.FrameRounding     = kFrameRound;
    s.ChildRounding     = kFrameRound;
    s.PopupRounding     = kFrameRound;
    s.GrabRounding      = kFrameRound;
    s.ScrollbarRounding = kFrameRound;
    s.ScrollbarSize     = 8.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = v4(kText);
    c[ImGuiCol_TextDisabled]         = v4(kMuted);
    c[ImGuiCol_WindowBg]             = v4(kWindow);
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = v4(rgb(0x1D2128));
    c[ImGuiCol_Border]               = v4(kBorder);
    c[ImGuiCol_FrameBg]              = v4(kField);
    c[ImGuiCol_FrameBgHovered]       = v4(kField);
    c[ImGuiCol_FrameBgActive]        = v4(kField);
    c[ImGuiCol_Button]               = v4(kButton);
    c[ImGuiCol_ButtonHovered]        = v4(kButtonHov);
    c[ImGuiCol_ButtonActive]         = v4(kButtonHov);
    c[ImGuiCol_Header]               = v4(kButton);
    c[ImGuiCol_HeaderHovered]        = v4(kButtonHov);
    c[ImGuiCol_HeaderActive]         = v4(kButtonHov);
    c[ImGuiCol_CheckMark]            = v4(kText);
    c[ImGuiCol_SliderGrab]           = v4(kText);
    c[ImGuiCol_SliderGrabActive]     = v4(kText);
    c[ImGuiCol_Separator]            = v4(kBorder);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = v4(kButton);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(kButtonHov);
    c[ImGuiCol_ScrollbarGrabActive]  = v4(kButtonHov);
    c[ImGuiCol_TextSelectedBg]       = v4(rgb(0x3D8FDB, 90));
    c[ImGuiCol_NavCursor]            = v4(kBlue);
    return f;
}

// ── Widgets ───────────────────────────────────────────────────────────────────

bool button(const char* label, bool primary, float width, float h, ImFont* font) {
    if (font) ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Button,        v4(primary ? kPrimary : kButton));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(primary ? kPrimaryHov : kButtonHov));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  v4(primary ? kPrimaryHov : kButtonHov));
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const bool r = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    if (font) ImGui::PopFont();
    return r;
}

bool outlineButton(const char* label, float width, bool dim) {
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(kButton));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  v4(kButtonHov));
    ImGui::PushStyleColor(ImGuiCol_Border,        v4(dim ? rgb(0x2A2F38) : rgb(0x3A414C)));
    ImGui::PushStyleColor(ImGuiCol_Text,          v4(dim ? kMuted : kText));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool r = ImGui::Button(label, ImVec2(width, kControlH));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(5);
    return r;
}

bool slider(const char* id, float& t, ImU32 fill) {
    const float  w = std::max(ImGui::GetContentRegionAvail().x, 40.0f);
    const float  h = 16.0f, r = 8.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const float x0 = p.x + r, x1 = p.x + w - r, cy = p.y + h * 0.5f;
    bool changed = false;
    if (ImGui::IsItemActive()) {
        const float nt = std::clamp((ImGui::GetIO().MousePos.x - x0) / std::max(x1 - x0, 1.0f), 0.0f, 1.0f);
        if (nt != t) { t = nt; changed = true; }
    }
    const float gx = x0 + (x1 - x0) * std::clamp(t, 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x0, cy - 2.0f), ImVec2(x1, cy + 2.0f), kButton, 2.0f);
    dl->AddRectFilled(ImVec2(x0, cy - 2.0f), ImVec2(gx, cy + 2.0f), fill, 2.0f);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        dl->AddCircleFilled(ImVec2(gx, cy), r + 4.0f, rgb(0xFFFFFF, 28));
    dl->AddCircleFilled(ImVec2(gx, cy), r, rgb(0xFFFFFF));
    return changed;
}

int segmented(const char* id, const char* const* labels, int count, int selected,
              const bool* enabled, const char* const* tips) {
    ImGui::PushID(id);
    const float  w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + kControlH), kField, kFrameRound);
    const float inset = 3.0f, segW = (w - 2.0f * inset) / static_cast<float>(count);
    int clicked = -1;
    for (int i = 0; i < count; ++i) {
        const bool   on = !enabled || enabled[i];
        const ImVec2 a(p.x + inset + segW * static_cast<float>(i), p.y + inset);
        const ImVec2 b(a.x + segW, p.y + kControlH - inset);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(segW, b.y - a.y)) && on && i != selected) clicked = i;
        const bool hov = ImGui::IsItemHovered();
        if (tips && tips[i] && tips[i][0] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", tips[i]);
        ImGui::PopID();
        if (i == selected)        dl->AddRectFilled(a, b, kButton, kFrameRound - 2.0f);
        else if (hov && on)       dl->AddRectFilled(a, b, rgb(0x262B33, 110), kFrameRound - 2.0f);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(a.x + (segW - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f),
                    !on ? rgb(0x4A515C) : (i == selected ? kText : kText2), labels[i]);
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, kControlH));
    ImGui::PopID();
    return clicked;
}

bool resetIcon(const char* id) {
    const float  s = 20.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool   r = ImGui::InvisibleButton(id, ImVec2(s, s));
    const ImU32  col = ImGui::IsItemHovered() ? kText : kText2;
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    const float  rad = 6.0f, a0 = -kPi * 0.5f + 0.7f, a1 = -kPi * 0.5f + 2.0f * kPi - 0.2f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PathArcTo(c, rad, a0, a1, 20);
    dl->PathStroke(col, 0, 1.6f);
    // Arrowhead at the arc's end, pointing along the (clockwise) tangent.
    const ImVec2 e(c.x + rad * std::cos(a1), c.y + rad * std::sin(a1));
    const ImVec2 t(-std::sin(a1), std::cos(a1));    // tangent
    const ImVec2 n(std::cos(a1), std::sin(a1));     // outward normal
    const float  k = 3.2f;
    dl->AddTriangleFilled(ImVec2(e.x + t.x * k, e.y + t.y * k),
                          ImVec2(e.x - t.x * 1.0f + n.x * k, e.y - t.y * 1.0f + n.y * k),
                          ImVec2(e.x - t.x * 1.0f - n.x * k, e.y - t.y * 1.0f - n.y * k), col);
    return r;
}

bool closeIcon(const char* id, float size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool   r = ImGui::InvisibleButton(id, ImVec2(size, size));
    const ImU32  col = ImGui::IsItemHovered() ? kText : kText2;
    const float  in = size * 0.3f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(p.x + in, p.y + in), ImVec2(p.x + size - in, p.y + size - in), col, 1.5f);
    dl->AddLine(ImVec2(p.x + size - in, p.y + in), ImVec2(p.x + in, p.y + size - in), col, 1.5f);
    return r;
}

bool disclosure(const char* label, bool& open) {
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  w  = 18.0f + ts.x;
    const bool   r  = ImGui::InvisibleButton(label, ImVec2(w, kControlH));
    if (r) open = !open;
    const ImU32 col = ImGui::IsItemHovered() ? kText : kText2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + 5.0f, p.y + kControlH * 0.5f);
    if (open) {
        const ImVec2 pts[3] = {ImVec2(c.x - 4, c.y - 2), ImVec2(c.x, c.y + 2), ImVec2(c.x + 4, c.y - 2)};
        dl->AddPolyline(pts, 3, col, 0, 1.6f);
    } else {
        const ImVec2 pts[3] = {ImVec2(c.x - 2, c.y - 4), ImVec2(c.x + 2, c.y), ImVec2(c.x - 2, c.y + 4)};
        dl->AddPolyline(pts, 3, col, 0, 1.6f);
    }
    dl->AddText(ImVec2(p.x + 18.0f, p.y + (kControlH - ts.y) * 0.5f), col, label);
    return r;
}

bool valueField(const char* id, float& v, float lo, float hi, float perPixel, const char* text,
                float width) {
    // A click without dragging turns the field into a text box: type a value,
    // Enter or click away to apply, Esc to cancel.
    static ImGuiID editing = 0;
    static bool    focusEdit = false;
    static char    ebuf[32] = {};
    const ImGuiID  key = ImGui::GetID(id);
    bool changed = false;
    if (editing == key) {
        ImGui::SetNextItemWidth(width);
        if (focusEdit) { ImGui::SetKeyboardFocusHere(); focusEdit = false; }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(12.0f, (kControlH - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kField);
        const bool enter = ImGui::InputText(id, ebuf, sizeof(ebuf),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal |
                                                ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (enter || ImGui::IsItemDeactivated()) {
            editing = 0;
            char* end = nullptr;
            const float nv = std::strtof(ebuf, &end);
            if (end != ebuf) {
                const float c = std::clamp(nv, lo, hi);
                if (c != v) { v = c; changed = true; }
            }
        }
        return changed;
    }

    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, kControlH));
    if (perPixel > 0.0f && ImGui::IsItemDeactivated() && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f) {
        editing = key;
        focusEdit = true;
        std::snprintf(ebuf, sizeof(ebuf), "%g", static_cast<double>(v));
    }
    const bool active = ImGui::IsItemActive() && perPixel > 0.0f;
    if (active) {
        const float dx = ImGui::GetIO().MouseDelta.x;
        if (dx != 0.0f) {
            const float nv = std::clamp(v + dx * perPixel, lo, hi);
            if (nv != v) { v = nv; changed = true; }
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + width, p.y + kControlH);
    dl->AddRectFilled(p, b, kField, kFrameRound);
    if (active)                       dl->AddRect(p, b, kBlue, kFrameRound, 0, 1.0f);
    else if (ImGui::IsItemHovered() && perPixel > 0.0f) dl->AddRect(p, b, kBorder, kFrameRound, 0, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(b.x - 12.0f - ts.x, p.y + (kControlH - ts.y) * 0.5f), kText, text);
    return changed;
}

bool checkbox(const char* label, bool& v) {
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float  box = 16.0f, h = std::max(box, ts.y);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool   r = ImGui::InvisibleButton(label, ImVec2(box + 10.0f + ts.x, h));
    if (r) v = !v;
    const bool   hov = ImGui::IsItemHovered();
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + (h - box) * 0.5f), b(a.x + box, a.y + box);
    if (v) {
        dl->AddRectFilled(a, b, hov ? kPrimaryHov : kPrimary, 4.0f);
        const ImVec2 pts[3] = {ImVec2(a.x + 3.5f, a.y + 8.0f), ImVec2(a.x + 6.8f, a.y + 11.2f),
                               ImVec2(a.x + 12.5f, a.y + 4.8f)};
        dl->AddPolyline(pts, 3, rgb(0xFFFFFF), 0, 1.8f);
    } else {
        dl->AddRectFilled(a, b, kField, 4.0f);
        dl->AddRect(a, b, hov ? rgb(0x4A515C) : rgb(0x3A414C), 4.0f, 0, 1.0f);
    }
    dl->AddText(ImVec2(b.x + 10.0f, p.y + (h - ts.y) * 0.5f), kText, label);
    return r;
}

// ── Drawing helpers ───────────────────────────────────────────────────────────

void dot(ImVec2 c, float r, ImU32 col) {
    ImGui::GetWindowDrawList()->AddCircleFilled(c, r, col);
}

void warningTriangle(ImVec2 p, float s, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddTriangle(ImVec2(p.x + s * 0.5f, p.y + 1.0f), ImVec2(p.x + s - 0.5f, p.y + s - 1.5f),
                    ImVec2(p.x + 0.5f, p.y + s - 1.5f), col, 1.5f);
    dl->AddLine(ImVec2(p.x + s * 0.5f, p.y + s * 0.38f), ImVec2(p.x + s * 0.5f, p.y + s * 0.62f), col, 1.5f);
    dl->AddCircleFilled(ImVec2(p.x + s * 0.5f, p.y + s * 0.76f), 1.0f, col);
}

void plane(ImVec2 c, float s, ImU32 col) {
    // Top view, nose up-right (taking off). Points in units of s, then rotated.
    const float ang = -kPi / 4.0f, cs = std::cos(ang), sn = std::sin(ang);
    auto P = [&](float x, float y) {   // x along the fuselage (nose +), y across
        return ImVec2(c.x + (x * cs - y * sn) * s, c.y + (x * sn + y * cs) * s);
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(P(-0.45f, 0.0f), P(0.42f, 0.0f), col, s * 0.11f);
    dl->AddCircleFilled(P(0.42f, 0.0f), s * 0.055f, col);
    const ImVec2 lw[4] = {P(0.12f, -0.03f), P(-0.10f, -0.48f), P(-0.20f, -0.48f), P(-0.06f, -0.03f)};
    const ImVec2 rw[4] = {P(0.12f, 0.03f), P(-0.06f, 0.03f), P(-0.20f, 0.48f), P(-0.10f, 0.48f)};
    dl->AddConvexPolyFilled(lw, 4, col);
    dl->AddConvexPolyFilled(rw, 4, col);
    const ImVec2 lt[4] = {P(-0.32f, -0.02f), P(-0.42f, -0.18f), P(-0.48f, -0.18f), P(-0.42f, -0.02f)};
    const ImVec2 rt[4] = {P(-0.32f, 0.02f), P(-0.42f, 0.02f), P(-0.48f, 0.18f), P(-0.42f, 0.18f)};
    dl->AddConvexPolyFilled(lt, 4, col);
    dl->AddConvexPolyFilled(rt, 4, col);
}

void text(const char* s, ImU32 col) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col));
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
}

void rightText(const char* s, ImU32 col, float rightInset) {
    ImGui::SameLine();
    const float tw = ImGui::CalcTextSize(s).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - tw - rightInset);
    text(s, col);
}

float chip(ImVec2 pos, const char* s, ImU32 bg, ImU32 fg) {
    const ImVec2 ts = ImGui::CalcTextSize(s);
    const float  w = std::max(ts.x + 12.0f, 22.0f), h = ts.y + 4.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), bg, 5.0f);
    dl->AddText(ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + 2.0f), fg, s);
    return w;
}

void gap(float px) {
    const float sp = ImGui::GetStyle().ItemSpacing.y;
    if (px > 2.0f * sp) ImGui::Dummy(ImVec2(0.0f, px - 2.0f * sp));   // spacing + dummy + spacing
}

} // namespace Theme
