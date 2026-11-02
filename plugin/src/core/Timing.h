#pragma once
// Timing — lightweight timers for the release build. A TickStats keeps the
// last kSamples durations of one recurring job (the flight loop, the window
// draw) and reports p50 / p95 / max; a Stopwatch times one-off jobs (apt.dat
// index, CSL scan, planning). Summaries are logged at debug level (Advanced >
// Debug logging). Pure, no XPLM calls.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>

class Stopwatch {
public:
    Stopwatch() : m_start(std::chrono::steady_clock::now()) {}
    double ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    }
private:
    std::chrono::steady_clock::time_point m_start;
};

class TickStats {
public:
    static constexpr std::size_t kSamples = 512;

    void add(double ms) {
        m_ring[m_next] = static_cast<float>(ms);
        m_next = (m_next + 1) % kSamples;
        if (m_count < kSamples) ++m_count;
        m_max = std::max(m_max, static_cast<float>(ms));
    }
    std::size_t count() const { return m_count; }
    // p in 0–1 over the samples kept.
    float percentile(double p) const {
        if (m_count == 0) return 0.0f;
        std::array<float, kSamples> s{};
        std::copy(m_ring.begin(), m_ring.begin() + static_cast<std::ptrdiff_t>(m_count), s.begin());
        auto nth = s.begin() + static_cast<std::ptrdiff_t>(std::min(m_count - 1, static_cast<std::size_t>(p * static_cast<double>(m_count))));
        std::nth_element(s.begin(), nth, s.begin() + static_cast<std::ptrdiff_t>(m_count));
        return *nth;
    }
    float maxEver() const { return m_max; }

private:
    std::array<float, kSamples> m_ring{};
    std::size_t                 m_next  = 0;
    std::size_t                 m_count = 0;
    float                       m_max   = 0.0f;
};

// Times the enclosing scope into a TickStats.
class ScopedTick {
public:
    explicit ScopedTick(TickStats& s) : m_stats(s) {}
    ~ScopedTick() { m_stats.add(m_watch.ms()); }
    ScopedTick(const ScopedTick&) = delete;
    ScopedTick& operator=(const ScopedTick&) = delete;
private:
    TickStats& m_stats;
    Stopwatch  m_watch;
};
