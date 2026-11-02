#pragma once
// Cancel — set when the plugin is stopping (XPluginStop) so long worker jobs
// (apt.dat indexing, CSL scan) end early instead of holding up X-Plane's exit.
// Pure, no XPLM calls; safe from any thread.

#include <atomic>

namespace Cancel {

inline std::atomic<bool>& flag() {
    static std::atomic<bool> f{false};
    return f;
}
inline bool requested() { return flag().load(std::memory_order_relaxed); }

} // namespace Cancel
