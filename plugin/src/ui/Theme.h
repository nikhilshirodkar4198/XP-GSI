#pragma once
// Theme — the XP-GSI look (design artboard XP-GSI.pdf, 2026-10-27): colours,
// metrics, fonts and the few custom widgets ImGui has no style for (thin
// slider, segmented control, chips, drawn icons). ImGui calls only; must be
// used inside a frame of the window's own context.
//
// Fonts: the artboard uses Inter. No font file is bundled; the closest system
// UI font is loaded at runtime (Segoe UI on Windows, Helvetica on macOS,
// DejaVu Sans / Liberation Sans on Linux), else ImGui's built-in font.

#include <imgui.h>

struct ImFont;

namespace Theme {

// ── Palette (artboard page 6) ────────────────────────────────────────────────
constexpr ImU32 rgb(unsigned hex, unsigned a = 255) {
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, a);
}
constexpr ImU32 kWindow     = rgb(0x16191E, 230);   // 90 % opacity
constexpr ImU32 kField      = rgb(0x1A1E24);
constexpr ImU32 kButton     = rgb(0x262B33);
constexpr ImU32 kButtonHov  = rgb(0x2F353F);
constexpr ImU32 kText       = rgb(0xE4E7EB);
constexpr ImU32 kText2      = rgb(0x8C95A1);
constexpr ImU32 kMuted      = rgb(0x6E7783);
constexpr ImU32 kBlue       = rgb(0x3D8FDB);
constexpr ImU32 kPrimary    = rgb(0x2A74BD);
constexpr ImU32 kPrimaryHov = rgb(0x3383CF);
constexpr ImU32 kOn         = rgb(0x4CC38A);
constexpr ImU32 kOff        = rgb(0xE5736B);
constexpr ImU32 kBorder     = rgb(0x323843);

// ── Metrics ──────────────────────────────────────────────────────────────────
constexpr float kPad        = 20.0f;   // window padding
constexpr float kGap        = 24.0f;   // between sections
constexpr float kFrameRound = 8.0f;
constexpr float kPrimaryH   = 40.0f;
constexpr float kControlH   = 32.0f;
constexpr float kRowH       = 40.0f;

struct Fonts {
    ImFont* body   = nullptr;   // 14 px regular
    ImFont* medium = nullptr;   // 16 px (buttons, inputs, list names)
    ImFont* title  = nullptr;   // 16 px semibold
};

// Load the fonts into the current context's atlas (before the texture is
// built) and apply the style. Returned fonts may be the built-in one.
Fonts setup();

// ── Widgets ──────────────────────────────────────────────────────────────────
// Full-width button of height `h`. primary = blue fill, else dark fill.
bool button(const char* label, bool primary, float width, float h, ImFont* font);
// Outlined button; `dim` draws it muted (still clickable).
bool outlineButton(const char* label, float width, bool dim);
// Thin slider: 4 px track, 16 px round grab. t in [0, 1]. Full available width.
bool slider(const char* id, float& t, ImU32 fill);
// Segmented control; returns the index clicked (or -1). `enabled` and `tips`
// (per-segment tooltips) may be null.
int  segmented(const char* id, const char* const* labels, int count, int selected,
               const bool* enabled = nullptr, const char* const* tips = nullptr);
// Small square icon buttons.
bool resetIcon(const char* id);             // circular arrow
bool closeIcon(const char* id, float size); // ×
// Disclosure chevron + label; toggles `open`.
bool disclosure(const char* label, bool& open);
// Value box showing `text` right-aligned. Dragging horizontally changes v by
// `perPixel` per pixel (clamped to lo..hi); perPixel 0 = display only.
// A click without dragging edits it as text (typed value, clamped).
bool valueField(const char* id, float& v, float lo, float hi, float perPixel, const char* text,
                float width);
// Styled checkbox (16 px box, blue when on).
bool checkbox(const char* label, bool& v);

// ── Drawing helpers ──────────────────────────────────────────────────────────
void dot(ImVec2 centre, float r, ImU32 col);
void warningTriangle(ImVec2 topLeft, float size, ImU32 col);
void plane(ImVec2 centre, float size, ImU32 col);
void text(const char* s, ImU32 col);
// Text right-aligned at the end of the current line (same line as the last item).
void rightText(const char* s, ImU32 col, float rightInset = 0.0f);
// Vertical space so the gap between the previous and next item is `px`.
void gap(float px);
// Small rounded box holding one letter or short text, drawn at `pos`; returns its width.
float chip(ImVec2 pos, const char* text, ImU32 bg, ImU32 fg);

} // namespace Theme
