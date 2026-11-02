#pragma once
#include <filesystem>

// Plugin path utilities. init() must be called once from XPluginStart (main
// thread) before any other use. All getters are read-only and thread-safe
// after that.
namespace Paths {

    // Derive plugin root from the .xpl binary path (go up two levels:
    // .../XP-GSI/win_x64/XP-GSI.xpl → .../XP-GSI/).
    // Verified: XPLMGetPluginInfo → outFilePath documented in XPLMPlugin.h
    // (XPLM200). Called with XPLMGetMyID() in XPluginStart.
    void init(const char* xplFilePath);

    // Query XPLMGetSystemPath (XPLMUtilities.h, XPLM200) and store result.
    // Must be called from the main thread, after init().
    void initXplaneRoot();

    // Root of the XP-GSI plugin folder (parent of win_x64/ mac_x64/ lin_x64/).
    const std::filesystem::path& pluginRoot();

    // X-Plane installation root (from XPLMGetSystemPath, trailing slash trimmed).
    const std::filesystem::path& xplaneRoot();

    // Store the X-Plane version integer from XPLMGetVersions (XPLM200).
    // Used by AptDat to select the XP11 vs XP12 global-airports path.
    void setXplaneVersion(int xpVer);
    int  xplaneVersion();

    // Subdirectory helpers — all relative to pluginRoot().
    std::filesystem::path logsDir();
    std::filesystem::path dataDir();
    std::filesystem::path cacheDir();
    std::filesystem::path settingsFile();

} // namespace Paths
