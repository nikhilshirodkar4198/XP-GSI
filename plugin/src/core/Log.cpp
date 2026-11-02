#include "Log.h"
#include <chrono>
#include <ctime>
#include <filesystem>

// Static member definitions
FILE*      Log::sFile     = nullptr;
Log::Level Log::sMinLevel = Log::Level::Info;
std::mutex Log::sMutex;

namespace {
std::filesystem::path sPath;
long                  sBytes = 0;

// Wide-character open on Windows: the path may hold any Unicode characters.
FILE* openFile(const std::filesystem::path& p) {
#if defined(_WIN32)
    return _wfopen(p.c_str(), L"w");
#else
    return std::fopen(p.c_str(), "w");
#endif
}

// XP-GSI.log → XP-GSI.prev.log (one previous file kept).
void rotate() {
    std::error_code ec;
    auto prev = sPath;
    prev.replace_extension(".prev.log");
    std::filesystem::remove(prev, ec);
    std::filesystem::rename(sPath, prev, ec);
}
} // namespace

void Log::init(const std::filesystem::path& logPath, Level minLevel) {
    std::lock_guard<std::mutex> lk(sMutex);
    sMinLevel = minLevel;
    sPath     = logPath;
    sBytes    = 0;

    std::error_code ec;
    std::filesystem::create_directories(logPath.parent_path(), ec);
    if (std::filesystem::exists(logPath, ec)) rotate();   // keep the last session's log

    sFile = openFile(logPath);
    // If the open fails we silently continue — no XPLM call available yet.
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lk(sMutex);
    if (sFile) {
        std::fflush(sFile);
        std::fclose(sFile);
        sFile = nullptr;
    }
}

void Log::setLevel(Level level) {
    std::lock_guard<std::mutex> lk(sMutex);
    sMinLevel = level;
}

Log::Level Log::getLevel() {
    std::lock_guard<std::mutex> lk(sMutex);
    return sMinLevel;
}

void Log::debug(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(Level::Debug, fmt, args);
    va_end(args);
}

void Log::info(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(Level::Info, fmt, args);
    va_end(args);
}

void Log::warn(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(Level::Warn, fmt, args);
    va_end(args);
}

void Log::error(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    write(Level::Error, fmt, args);
    va_end(args);
}

static const char* levelTag(Log::Level l) {
    switch (l) {
        case Log::Level::Debug: return "DEBUG";
        case Log::Level::Info:  return "INFO ";
        case Log::Level::Warn:  return "WARN ";
        case Log::Level::Error: return "ERROR";
    }
    return "?????";
}

void Log::write(Level level, const char* fmt, std::va_list args) {
    // Quick non-locking pre-check to avoid the lock on filtered messages.
    if (level < sMinLevel) return;

    // Timestamp (local time)
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf {};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char timeBuf[16];
    std::strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmBuf);

    // Format message into a fixed buffer; truncate gracefully.
    char msgBuf[2048];
    std::vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);

    std::lock_guard<std::mutex> lk(sMutex);
    if (level < sMinLevel) return; // re-check under lock

    if (sFile) {
        // Capped at kMaxBytes: the file moves to XP-GSI.prev.log and a new one starts.
        if (sBytes > kMaxBytes) {
            std::fclose(sFile);
            rotate();
            sFile  = openFile(sPath);
            sBytes = 0;
            if (!sFile) return;
        }
        const int n = std::fprintf(sFile, "[%s] [%s] %s\n", timeBuf, levelTag(level), msgBuf);
        if (n > 0) sBytes += n;
        std::fflush(sFile);
    }
}
