#pragma once
#include <XPLMInstance.h>
#include <cstddef>

// Per-instance handle. One entry per airline stand that has a live XPLMInstanceRef.
struct StaticAircraft {
    XPLMInstanceRef instance = nullptr;
    std::size_t     standIdx = 0;   // index into StaticRenderer::m_stands
};
