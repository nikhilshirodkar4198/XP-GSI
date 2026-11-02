#pragma once
#include <string>
#include <vector>
#include "engine/StandState.h"

// Computes a deterministic fingerprint of an airport's stand layout.
// Used to detect scenery changes so that cached profiles can be invalidated.
//
// Algorithm: FNV-1a 64-bit over sorted "name|lat5dp|lon5dp|hdg5deg" tokens.
// Output format: "fnv1a:<16 hex digits>".
namespace Fingerprint {
    std::string compute(const std::vector<Stand>& stands);
} // namespace Fingerprint
