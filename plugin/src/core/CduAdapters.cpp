#include "core/CduAdapters.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

#include <nlohmann/json.hpp>

namespace CduAdapters {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string upperTrim(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (!std::isspace(c)) o += static_cast<char>(std::toupper(c));
    return o;
}

} // namespace

bool load(const std::filesystem::path& file, std::vector<Adapter>& out, std::string* error) {
    out.clear();
    std::ifstream f(file);
    if (!f.is_open()) { if (error) *error = "not found"; return false; }
    nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object() || j.value("schema", 0) != 1) {
        if (error) *error = "not valid JSON with \"schema\": 1";
        return false;
    }
    auto it = j.find("adapters");
    if (it == j.end() || !it->is_array()) { if (error) *error = "no \"adapters\" array"; return false; }
    for (const auto& e : *it) {
        if (!e.is_object()) continue;
        Adapter a;
        a.name    = e.value("name", std::string{});
        a.enabled = e.value("enabled", false);
        a.note    = e.value("note", std::string{});
        if (auto m = e.find("match"); m != e.end() && m->is_object()) {
            a.requiresDataref = m->value("requires_dataref", std::string{});
            a.acfFileRegex    = m->value("acf_file_regex", std::string{});
            a.acfPathContains = m->value("acf_path_contains", std::string{});
            if (auto ic = m->find("icao"); ic != m->end() && ic->is_array())
                for (const auto& s : *ic)
                    if (s.is_string()) a.icao.push_back(upperTrim(s.get<std::string>()));
        }
        if (auto r = e.find("read"); r != e.end() && r->is_object()) {
            a.readDataref = r->value("dataref", std::string{});
            a.readBytes   = std::clamp(r->value("bytes", 96), 1, 1024);
        }
        if (!a.name.empty() && !a.readDataref.empty()) out.push_back(std::move(a));
    }
    return true;
}

const Adapter* find(const std::vector<Adapter>& adapters, const Aircraft& ac,
                    const std::function<bool(const std::string&)>& datarefExists) {
    for (const auto& a : adapters) {
        if (!a.enabled) continue;
        bool any = false, ok = true;
        if (!a.requiresDataref.empty()) {
            any = true;
            ok = ok && datarefExists && datarefExists(a.requiresDataref);
        }
        if (ok && !a.acfFileRegex.empty()) {
            any = true;
            try {
                ok = std::regex_match(ac.acfFile, std::regex(a.acfFileRegex, std::regex::ECMAScript |
                                                                              std::regex::icase));
            } catch (const std::regex_error&) {
                ok = false;
            }
        }
        if (ok && !a.acfPathContains.empty()) {
            any = true;
            ok = lower(ac.acfPath).find(lower(a.acfPathContains)) != std::string::npos;
        }
        if (ok && !a.icao.empty()) {
            any = true;
            ok = std::find(a.icao.begin(), a.icao.end(), upperTrim(ac.icao)) != a.icao.end();
        }
        if (any && ok) return &a;
    }
    return nullptr;
}

} // namespace CduAdapters
