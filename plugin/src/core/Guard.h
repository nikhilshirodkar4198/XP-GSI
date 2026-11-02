#pragma once
// Guard — the exception barrier at every XPLM callback. A C++ exception that
// reached X-Plane through a C callback would take the sim down; here it is
// logged (the first kMaxLogs times per call site, so a per-frame failure
// cannot flood the log) and the callback returns its neutral value.

#include <atomic>
#include <exception>
#include <utility>

#include "core/Log.h"

namespace Guard {

constexpr int kMaxLogs = 5;

template <class F>
auto run(const char* where, decltype(std::declval<F>()()) fallback, F&& f) noexcept -> decltype(f()) {
    static std::atomic<int> logged{0};
    try {
        return f();
    } catch (const std::exception& e) {
        if (logged.fetch_add(1) < kMaxLogs) Log::error("%s: unexpected error: %s", where, e.what());
    } catch (...) {
        if (logged.fetch_add(1) < kMaxLogs) Log::error("%s: unexpected error", where);
    }
    return fallback;
}

template <class F>
void run(const char* where, F&& f) noexcept {
    static std::atomic<int> logged{0};
    try {
        f();
    } catch (const std::exception& e) {
        if (logged.fetch_add(1) < kMaxLogs) Log::error("%s: unexpected error: %s", where, e.what());
    } catch (...) {
        if (logged.fetch_add(1) < kMaxLogs) Log::error("%s: unexpected error", where);
    }
}

} // namespace Guard
