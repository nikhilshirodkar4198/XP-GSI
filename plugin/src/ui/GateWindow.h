#pragma once
// GateWindow — arrival gate reservation, part 2: a small window with a text
// box and an autofill list of the active airport's stands (engine/GateSuggest),
// the current reservations with release buttons, and keyboard / button-only
// control. Reserving and releasing go through ArrivalGate (part 1), so the
// armed window, matching, clearing, notification and logging stay in one place.
//
// Keys while it has focus (XPLMDefs.h XPLM_VK_*): Up / Down move the
// highlight, Enter reserves (an exact typed match wins over the highlight),
// Esc closes; everything else edits the text box. Main thread only.

#include <string>
#include <vector>

#include "engine/GateSuggest.h"
#include "ui/ImGuiWindow.h"

class ArrivalGate;

class GateWindow : public ImGuiWindow {
public:
    void attach(ArrivalGate* gate) { m_gate = gate; }

    // Show it. withKeyboard: take X-Plane's keyboard focus and put the cursor
    // in the text box (key command, menu, main-window button). The button-only
    // commands open it without taking the keyboard.
    void open(bool withKeyboard);
    void close();
    void toggle();

    // Button-only control (xpgsi/gate_next, gate_prev, gate_confirm).
    void move(int delta);
    void confirm();

    // Auto-close after a reservation; call every flight-loop tick.
    void tick(double now);

protected:
    void buildUi() override;
    bool onKey(char key, XPLMKeyFlags flags, unsigned char vkey) override;

private:
    void rerank(double now);
    void enter();
    void reserveRow(std::size_t stand);
    void reserved(const std::string& msg);

    ArrivalGate*                         m_gate = nullptr;
    char                                 m_query[33] = {};
    std::string                          m_ranked;           // query the rows were ranked for
    double                               m_nextRerank = 0.0; // distances change as the user taxis
    std::size_t                          m_rankedFor = 0;    // stand count the rows were ranked for
    std::vector<std::size_t>             m_rows;             // stand indices in display order
    std::vector<GateSuggest::Candidate>  m_cands;
    int                                  m_sel = 0;
    bool                                 m_scrollToSel = false;
    bool                                 m_focusInput = false;
    std::string                          m_confirm;
    double                               m_closeAt = -1.0;
};
