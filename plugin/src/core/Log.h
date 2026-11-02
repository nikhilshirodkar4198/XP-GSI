#pragma once
#include <filesystem>
#include <mutex>
#include <cstdio>
#include <cstdarg>

// Compiler format-string checking (GCC/Clang). MSVC silently ignores it.
#if defined(__GNUC__) || defined(__clang__)
#  define XPGSI_PRINTF_LIKE(fmtArg, vaArg) \
       __attribute__((format(printf, fmtArg, vaArg)))
#else
#  define XPGSI_PRINTF_LIKE(fmtArg, vaArg)
#endif

// Thread-safe file logger. init() must be called from the main thread before
// any worker threads start. All log methods are safe from any thread after that.
// XPLMDebugString is intentionally NOT called — it requires the main thread.
class Log {
public:
    enum class Level { Debug = 0, Info, Warn, Error };

    // Largest log file before it rotates (one previous file is kept).
    static constexpr long kMaxBytes = 5L * 1024 * 1024;

    // Open the log file, moving last session's to XP-GSI.prev.log. Creates
    // parent directories.
    static void init(const std::filesystem::path& logPath,
                     Level minLevel = Level::Info);
    // Flush and close. Safe to call even if init() was never called.
    static void shutdown();

    static void debug(const char* fmt, ...) XPGSI_PRINTF_LIKE(1, 2);
    static void info (const char* fmt, ...) XPGSI_PRINTF_LIKE(1, 2);
    static void warn (const char* fmt, ...) XPGSI_PRINTF_LIKE(1, 2);
    static void error(const char* fmt, ...) XPGSI_PRINTF_LIKE(1, 2);

    static void  setLevel(Level level);
    static Level getLevel();

private:
    static void write(Level level, const char* fmt, std::va_list args);

    static FILE*      sFile;
    static Level      sMinLevel;
    static std::mutex sMutex;
};
