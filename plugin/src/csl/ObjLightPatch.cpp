#include "csl/ObjLightPatch.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace ObjLightPatch {

namespace fs = std::filesystem;

namespace {

// First whitespace-separated word of a line, and where its value starts.
std::string firstWord(const std::string& line, size_t& rest) {
    size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos) { rest = line.size(); return {}; }
    size_t e = line.find_first_of(" \t\r", b);
    if (e == std::string::npos) e = line.size();
    rest = line.find_first_not_of(" \t", e);
    if (rest == std::string::npos) rest = line.size();
    return line.substr(b, e - b);
}

bool isTextureCmd(const std::string& w) {
    return w == "TEXTURE" || w == "TEXTURE_LIT" || w == "TEXTURE_NORMAL";
}

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

std::string hex(std::uint64_t v, int digits) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return std::string(buf + 16 - digits);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// OBJ8 paths end at whitespace: keep link names free of it.
std::string spaceFree(std::string s) {
    std::replace_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }, '_');
    return s;
}

// Hard link (or, failing that, copy) src to dst; an up-to-date dst is kept.
bool linkOrCopy(const fs::path& src, const fs::path& dst) {
    std::error_code ec;
    if (fs::exists(dst, ec)) {
        std::error_code e1, e2;
        if (fs::file_size(dst, e1) == fs::file_size(src, e2) && !e1 && !e2 &&
            fs::last_write_time(dst, e1) == fs::last_write_time(src, e2) && !e1 && !e2)
            return true;
        fs::remove(dst, ec);
    }
    fs::create_hard_link(src, dst, ec);
    if (!ec) return true;
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace

std::string patch(const std::string& objText,
                  const std::function<std::string(const std::string&)>& mapTexture,
                  bool& changed) {
    changed = false;
    std::istringstream in(objText);
    std::ostringstream out;
    std::string line;
    int  depth   = 0;
    bool inWrap  = false;   // inside an ANIM block we opened around always-on lights
    auto closeWrap = [&] {
        if (inWrap) { out << "ANIM_end\n"; inWrap = false; }
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t rest = 0;
        const std::string w = firstWord(line, rest);

        if (isTextureCmd(w)) {
            closeWrap();
            std::string path = line.substr(rest);
            while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
            const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' ||
                                                    (path.size() > 1 && path[1] == ':'));
            const std::string mapped = (!path.empty() && !absolute && mapTexture) ? mapTexture(path)
                                                                                 : std::string{};
            if (!mapped.empty()) out << w << ' ' << mapped << '\n';
            else                 out << line << '\n';
            continue;
        }
        if (w == "ANIM_begin") { closeWrap(); ++depth; out << line << '\n'; continue; }
        if (w == "ANIM_end")   { closeWrap(); if (depth > 0) --depth; out << line << '\n'; continue; }

        if (depth == 0 && w.rfind("LIGHT_", 0) == 0) {
            if (!inWrap) {
                out << "ANIM_begin\n"
                    << "ANIM_hide -1.0 0.0 " << kLogoDataref << '\n';
                inWrap  = true;
                changed = true;
            }
            out << line << '\n';
            continue;
        }
        // Comments and blank lines may sit between grouped lights.
        if (inWrap && (w.empty() || w[0] == '#')) { out << line << '\n'; continue; }
        closeWrap();
        out << line << '\n';
    }
    closeWrap();
    return out.str();
}

fs::path patchedCopy(const fs::path& obj, const fs::path& cacheDir) {
    std::error_code ec;
    const auto size  = fs::file_size(obj, ec);
    if (ec) return obj;
    const auto mtime = fs::last_write_time(obj, ec);
    if (ec) return obj;

    // One result per source file for the plugin's lifetime.
    static std::mutex mux;
    static std::unordered_map<std::string, fs::path> done;
    static bool cleaned = false;
    // "v2": logo lights on xpgsi/controls/logo_lites_on (v1 used nav_lites_on).
    const std::string key = obj.generic_u8string() + '|' + std::to_string(size) + '|' +
                            std::to_string(mtime.time_since_epoch().count()) + "|v2";
    {
        std::lock_guard<std::mutex> lk(mux);
        auto it = done.find(key);
        if (it != done.end()) return it->second;
        if (!cleaned) {
            // Once per session: drop every cached OBJ copy (older layouts and
            // patch versions, models no longer used); the ones needed are
            // rebuilt on demand. Texture links stay and are reused.
            cleaned = true;
            std::vector<fs::path> stale;
            for (const auto& e : fs::recursive_directory_iterator(cacheDir, ec))
                if (e.is_regular_file(ec) && (e.path().extension() == ".obj" || e.path().extension() == ".tmp"))
                    stale.push_back(e.path());
            for (const auto& p : stale) fs::remove(p, ec);
            ec.clear();
        }
    }

    const fs::path srcDir = obj.parent_path();
    const fs::path dir    = cacheDir / hex(fnv1a(srcDir.generic_u8string()), 16);
    const fs::path copy   = dir / (hex(fnv1a(key), 16) + ".obj");
    fs::path result = obj;

    if (fs::exists(copy, ec)) {
        result = copy;                                    // made earlier (same source, size, time)
    } else {
        std::ifstream f(obj, std::ios::binary);
        std::ostringstream text;
        text << f.rdbuf();
        bool any = false;
        patch(text.str(), nullptr, any);                  // dry run: anything to wrap?
        if (!any || !f) {
            std::lock_guard<std::mutex> lk(mux);
            done.emplace(key, obj);
            return obj;
        }
        fs::create_directories(dir, ec);
        bool linked = true;
        auto mapTexture = [&](const std::string& rel) -> std::string {
            const fs::path src    = (srcDir / fs::u8path(rel)).lexically_normal();
            const std::string pre = hex(fnv1a(src.parent_path().generic_u8string()), 8) + '_';
            // The named file and its same-stem siblings (A320.png → A320.dds).
            const std::string stem = lower(src.stem().u8string());
            bool any = false;
            std::error_code e;
            for (const auto& s : fs::directory_iterator(src.parent_path(), e)) {
                if (!s.is_regular_file(e) || lower(s.path().stem().u8string()) != stem) continue;
                any = linkOrCopy(s.path(), dir / (pre + spaceFree(s.path().filename().u8string()))) || any;
            }
            linked = linked && any;
            return pre + spaceFree(src.filename().u8string());
        };
        bool changed = false;
        const std::string patched = patch(text.str(), mapTexture, changed);
        if (changed && f && linked) {
            fs::path tmp = copy;
            tmp += ".tmp";
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            o << patched;
            o.close();
            if (o) {
                fs::rename(tmp, copy, ec);
                if (!ec) result = copy;
            }
            if (result != copy) fs::remove(tmp, ec);
        }
    }

    std::lock_guard<std::mutex> lk(mux);
    done.emplace(key, result);
    return result;
}

} // namespace ObjLightPatch
