#include "Paths.h"
// XPLMGetSystemPath is declared in XPLMUtilities.h (XPLM200).
// Verified: function signature void XPLMGetSystemPath(char* outSystemPath);
// Buffer must be at least 512 bytes per SDK documentation.
#include <XPLMUtilities.h>

namespace {
    std::filesystem::path sPluginRoot;
    std::filesystem::path sXplaneRoot;
    int sXplaneVersion = 0;
}

namespace Paths {

void init(const char* xplFilePath) {
    // xplFilePath example: /X-Plane 12/Resources/plugins/XP-GSI/win_x64/XP-GSI.xpl
    // parent_path() twice gives: /X-Plane 12/Resources/plugins/XP-GSI/
    // X-Plane passes paths as UTF-8 on every platform.
    const std::filesystem::path xpl = std::filesystem::u8path(xplFilePath);
    sPluginRoot = xpl.parent_path().parent_path();
}

void initXplaneRoot() {
    // XPLMGetSystemPath fills the buffer with the X-Plane root (UTF-8)
    // including a trailing path separator. Verified in XPLMUtilities.h (XPLM200).
    char buf[512] = {};
    XPLMGetSystemPath(buf);
    // Trim the trailing separator so path composition works uniformly.
    std::string str = buf;
    if (!str.empty() && (str.back() == '/' || str.back() == '\\'))
        str.pop_back();
    sXplaneRoot = std::filesystem::u8path(str);
}

void setXplaneVersion(int xpVer) { sXplaneVersion = xpVer; }
int  xplaneVersion()             { return sXplaneVersion;  }

const std::filesystem::path& pluginRoot()  { return sPluginRoot;  }
const std::filesystem::path& xplaneRoot()  { return sXplaneRoot;  }

std::filesystem::path logsDir()      { return sPluginRoot / "logs";     }
std::filesystem::path dataDir()      { return sPluginRoot / "data";     }
std::filesystem::path cacheDir()     { return sPluginRoot / "cache";    }
std::filesystem::path settingsFile() { return sPluginRoot / "settings.json"; }

} // namespace Paths
