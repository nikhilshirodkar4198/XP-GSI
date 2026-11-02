#include "ui/GateWindow.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

#include <XPLMDefs.h>
#include <XPLMProcessing.h>

#include <imgui.h>

#include "app/ArrivalGate.h"
#include "engine/Reservations.h"
#include "ui/Theme.h"

namespace {
constexpr float  kPillH        = 28.0f;
constexpr float  kRowGap       = 2.0f;
constexpr double kConfirmSec   = 1.5;
constexpr double kRerankSec    = 1.0;
}

// ── Open / close ──────────────────────────────────────────────────────────────

void GateWindow::open(bool withKeyboard) {
    if (!isCreated()) return;
    if (!isVisible()) {
        m_query[0] = '\0';
        m_ranked.assign(1, '\x01');   // force a re-rank
        m_sel = 0;
        m_confirm.clear();
        m_closeAt = -1.0;
    }
    setVisible(true);
    if (withKeyboard) {
        takeKeyboard();
        m_focusInput = true;
    }
}

void GateWindow::close() {
    setVisible(false);   // also returns the keyboard to X-Plane
    m_closeAt = -1.0;
}

void GateWindow::toggle() {
    if (isVisible()) close();
    else open(true);
}

void GateWindow::move(int delta) {
    if (!isVisible()) { open(false); return; }
    if (m_rows.empty()) return;
    m_sel = std::clamp(m_sel + delta, 0, static_cast<int>(m_rows.size()) - 1);
    m_scrollToSel = true;
}

void GateWindow::confirm() {
    if (!isVisible()) { open(false); return; }
    enter();
}

void GateWindow::tick(double now) {
    if (m_closeAt > 0.0 && now >= m_closeAt) close();
}

// ── Reserving ─────────────────────────────────────────────────────────────────

void GateWindow::reserved(const std::string& msg) {
    if (msg.empty()) return;
    m_confirm = msg;
    m_query[0] = '\0';
    m_closeAt = XPLMGetElapsedTime() + kConfirmSec;
}

void GateWindow::reserveRow(std::size_t stand) {
    if (!m_gate || stand >= m_gate->allNames().size()) return;
    const std::string label = Reservations::normalizeStand(m_gate->allNames()[stand]);
    reserved(m_gate->reserveStands({stand}, label, "gate window", XPLMGetElapsedTime()));
}

void GateWindow::enter() {
    if (!m_gate || !m_gate->available()) return;
    // An exact typed match wins over the highlight (part 1 rule, duplicates too).
    if (m_query[0] && !Reservations::match(m_query, m_gate->allNames()).empty()) {
        reserved(m_gate->reserve(m_query, "gate window", XPLMGetElapsedTime()));
        return;
    }
    if (m_sel >= 0 && m_sel < static_cast<int>(m_rows.size())) reserveRow(m_rows[static_cast<std::size_t>(m_sel)]);
}

bool GateWindow::onKey(char, XPLMKeyFlags flags, unsigned char vkey) {
    const bool down = (flags & xplm_DownFlag) != 0;
    switch (vkey) {
        case XPLM_VK_UP:     if (down) move(-1); return true;
        case XPLM_VK_DOWN:   if (down) move(+1); return true;
        case XPLM_VK_RETURN:
        case XPLM_VK_ENTER:
        case XPLM_VK_NUMPAD_ENT:
            if (down) enter();
            return true;
        case XPLM_VK_ESCAPE: if (down) close(); return true;
        default:             return false;
    }
}

// ── Ranking ───────────────────────────────────────────────────────────────────

void GateWindow::rerank(double now) {
    const auto& stands = m_gate->allStands();
    const bool queryChanged = m_ranked != m_query;
    if (!queryChanged && stands.size() == m_rankedFor && now < m_nextRerank) return;
    m_cands.resize(stands.size());
    for (std::size_t i = 0; i < stands.size(); ++i) {
        m_cands[i].norm  = m_gate->normNames()[i];
        m_cands[i].size  = stands[i].widthCode;
        m_cands[i].distM = m_gate->distanceTo(i);
    }
    m_rows = GateSuggest::rank(m_query, m_cands, m_gate->userSizeCode());
    if (queryChanged) { m_sel = 0; m_scrollToSel = true; }
    m_sel = std::clamp(m_sel, 0, std::max(0, static_cast<int>(m_rows.size()) - 1));
    m_ranked     = m_query;
    m_rankedFor  = stands.size();
    m_nextRerank = now + kRerankSec;
}

// ── UI (layout: design artboard XP-GSI.pdf pages 4–5) ─────────────────────────

void GateWindow::buildUi() {
    if (!m_gate) return;
    const double now = XPLMGetElapsedTime();
    if (m_closeAt > 0.0 && now >= m_closeAt) { close(); return; }   // per frame: on time
    std::string why;
    const bool ok = m_gate->available(&why);

    // ── Search field (16 px, blue border while typing) ───────────────────────
    ImGui::PushFont(m_fonts.medium);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginDisabled(!ok);
    ImGui::SetNextItemWidth(-1.0f);
    if (m_focusInput && ok) { ImGui::SetKeyboardFocusHere(); m_focusInput = false; }
    ImGui::InputTextWithHint("##gate", "Type gate number...", m_query, sizeof(m_query),
                             ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
    const bool typing = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                        typing ? Theme::kBlue : Theme::kBorder, Theme::kFrameRound, 0,
                                        typing ? 1.5f : 1.0f);
    if (ok && ImGui::IsItemHovered()) ImGui::SetTooltip("Enter reserves, Up / Down move, Esc closes");

    if (!m_confirm.empty()) Theme::text(m_confirm.c_str(), Theme::kOn);
    const auto res = m_gate->reserved();   // copy: releasing changes the set

    // ── Not available yet: centred plane and the reason ──────────────────────
    if (!ok) {
        if (!why.empty()) why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
        const ImVec2 p  = ImGui::GetCursorScreenPos();
        const ImVec2 a  = ImGui::GetContentRegionAvail();
        const float  tw = ImGui::CalcTextSize(why.c_str(), nullptr, false, a.x).x;
        const float  cy = p.y + std::max(a.y * 0.42f, 40.0f);
        Theme::plane(ImVec2(p.x + a.x * 0.5f, cy - 18.0f), 30.0f, Theme::kMuted);
        ImGui::SetCursorScreenPos(ImVec2(p.x + std::max((a.x - tw) * 0.5f, 0.0f), cy + 8.0f));
        ImGui::PushTextWrapPos(p.x + a.x);
        Theme::text(why.c_str(), Theme::kText2);
        ImGui::PopTextWrapPos();
        return;
    }

    rerank(now);

    // ── Suggestions: 40 px rows, name · size chip · status · distance ────────
    const char  userSize = m_gate->userSizeCode();
    const float footerH  = res.empty() ? ImGui::GetTextLineHeight() + 22.0f : kPillH + 22.0f;
    const float listH    = std::max(ImGui::GetContentRegionAvail().y - footerH, Theme::kRowH * 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, kRowGap));
    if (ImGui::BeginChild("##rows", ImVec2(0.0f, listH))) {
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        ImFont*     nameFont = m_fonts.title;
        const float nameSize = nameFont->FontSize;
        const float lh       = ImGui::GetTextLineHeight();
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(m_rows.size()), Theme::kRowH + kRowGap);
        if (m_scrollToSel && m_sel < static_cast<int>(m_rows.size())) clip.IncludeItemByIndex(m_sel);
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const std::size_t i = m_rows[static_cast<std::size_t>(r)];
                const Stand& st  = m_gate->allStands()[i];
                const bool   fit = GateSuggest::fits(st.widthCode, userSize);
                const ImVec2 p   = ImGui::GetCursorScreenPos();
                const float  rw  = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(r);
                if (ImGui::InvisibleButton("##row", ImVec2(rw, Theme::kRowH))) {
                    m_sel = r;
                    reserveRow(i);
                }
                const bool hov = ImGui::IsItemHovered();
                if (!fit && hov) ImGui::SetTooltip("May be too small for your aircraft");
                if (m_scrollToSel && r == m_sel) { ImGui::SetScrollHereY(0.5f); m_scrollToSel = false; }
                ImGui::PopID();

                const ImVec2 q(p.x + rw, p.y + Theme::kRowH);
                if (r == m_sel) dl->AddRectFilled(p, q, Theme::kButton, Theme::kFrameRound);
                else if (hov)   dl->AddRectFilled(p, q, Theme::rgb(0x262B33, 120), Theme::kFrameRound);
                const ImU32 main = fit ? Theme::kText : Theme::kMuted;
                const ImU32 sub  = fit ? Theme::kText2 : Theme::rgb(0x4A515C);
                const float cy   = p.y + Theme::kRowH * 0.5f;

                float x = p.x + 12.0f;
                const float nameH = nameFont->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, st.id.c_str()).y;
                dl->AddText(nameFont, nameSize, ImVec2(x, cy - nameH * 0.5f), main, st.id.c_str());
                x += nameFont->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, st.id.c_str()).x + 10.0f;
                const char code[2] = {(st.widthCode >= 'A' && st.widthCode <= 'F') ? st.widthCode : '-', 0};
                x += Theme::chip(ImVec2(x, cy - lh * 0.5f - 2.0f), code,
                                 fit ? Theme::rgb(0x2A3038) : Theme::rgb(0x20252C), sub) + 10.0f;
                const auto status = static_cast<GateSuggest::Status>(m_gate->statusOf(i));
                if (status != GateSuggest::Status::Free)
                    dl->AddText(ImVec2(x, cy - lh * 0.5f),
                                status == GateSuggest::Status::Reserved ? Theme::kBlue : sub,
                                GateSuggest::statusName(status));
                const std::string d = GateSuggest::formatDistance(m_gate->distanceTo(i));
                dl->AddText(ImVec2(q.x - 12.0f - ImGui::CalcTextSize(d.c_str()).x, cy - lh * 0.5f), sub,
                            d.c_str());
            }
        }
        if (m_rows.empty() && !m_gate->allStands().empty()) Theme::text("No stand matches", Theme::kMuted);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ── Reservations: pills with ×, "Release all" ────────────────────────────
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    const float  w  = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(sp, ImVec2(sp.x + w, sp.y), Theme::kBorder);
    ImGui::Dummy(ImVec2(w, 1.0f));
    if (res.empty()) {
        Theme::text("No gate reserved", Theme::kMuted);
        return;
    }
    const ImVec2 p0       = ImGui::GetCursorScreenPos();
    const char*  relLabel = "Release all";
    const float  relW     = ImGui::CalcTextSize(relLabel).x;
    const float  lh       = ImGui::GetTextLineHeight();
    const float  maxX     = p0.x + w - relW - 12.0f;
    ImDrawList*  dl       = ImGui::GetWindowDrawList();
    float x = p0.x, y = p0.y;
    for (std::size_t i : res) {
        if (i >= m_gate->allStands().size()) continue;
        const std::string& id = m_gate->allStands()[i].id;
        const float pw = ImGui::CalcTextSize(id.c_str()).x + 12.0f + 26.0f;   // text pad + ×
        if (x > p0.x && x + pw > maxX) { x = p0.x; y += kPillH + 6.0f; }
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##pill", ImVec2(pw, kPillH))) m_gate->releaseOne(i, now);
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetTooltip("Release %s", id.c_str());
        ImGui::PopID();
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + pw, y + kPillH), hov ? Theme::kButtonHov : Theme::kButton,
                          kPillH * 0.5f);
        dl->AddText(ImVec2(x + 12.0f, y + (kPillH - lh) * 0.5f), Theme::kText, id.c_str());
        const float cx = x + pw - 15.0f, cyy = y + kPillH * 0.5f, k = 3.5f;
        const ImU32 xc = hov ? Theme::kText : Theme::kText2;
        dl->AddLine(ImVec2(cx - k, cyy - k), ImVec2(cx + k, cyy + k), xc, 1.4f);
        dl->AddLine(ImVec2(cx + k, cyy - k), ImVec2(cx - k, cyy + k), xc, 1.4f);
        x += pw + 6.0f;
    }
    const ImVec2 rp(p0.x + w - relW, p0.y + (kPillH - lh) * 0.5f);
    ImGui::SetCursorScreenPos(rp);
    if (ImGui::InvisibleButton("##releaseall", ImVec2(relW, lh))) m_gate->releaseAll("gate window", now);
    dl->AddText(rp, ImGui::IsItemHovered() ? Theme::kText : Theme::kText2, relLabel);
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(w, y - p0.y + kPillH));
}
