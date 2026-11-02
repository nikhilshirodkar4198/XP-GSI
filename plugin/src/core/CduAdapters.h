#pragma once
// CduAdapters — how to read the scratchpad of the user's aircraft's CDU/MCDU,
// from data/cdu_adapters.json (editable without a rebuild):
//
//   { "schema": 1, "adapters": [
//       { "name": "Zibo / LevelUp 737", "enabled": true,
//         "match": { "requires_dataref": "laminar/B738/fmc1/Line_entry",
//                    "acf_file_regex": "...", "acf_path_contains": "...",
//                    "icao": ["B738"] },
//         "read":  { "dataref": "laminar/B738/fmc1/Line_entry", "bytes": 64 } } ] }
//
// Every given match condition must hold (none given = never matches). The
// first enabled adapter that matches wins. `read.dataref` is a byte/string
// dataref (xplmType_Data); its text up to the first NUL is the scratchpad.
// A CDU screen line is just such a dataref
// (sim/cockpit2/radios/indicators/fms_cduN_text_lineM, byte[96]).
//
// Pure logic (JSON + matching); the caller supplies dataref existence.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace CduAdapters {

struct Adapter {
    std::string              name;
    bool                     enabled = false;
    std::string              requiresDataref;
    std::string              acfFileRegex;      // ECMAScript, case-insensitive, whole file name
    std::string              acfPathContains;   // case-insensitive, '/' separators
    std::vector<std::string> icao;              // any of these (sim/aircraft/view/acf_ICAO)
    std::string              readDataref;
    int                      readBytes = 96;
    std::string              note;
};

// Parse the file; false (and empty) if missing or malformed. Entries without
// a name or read dataref are skipped.
bool load(const std::filesystem::path& file, std::vector<Adapter>& out, std::string* error = nullptr);

struct Aircraft {
    std::string acfFile;      // e.g. "b738.acf"
    std::string acfPath;      // full path as X-Plane reports it
    std::string icao;         // e.g. "B738"
};

// First enabled adapter whose conditions all hold, or nullptr.
const Adapter* find(const std::vector<Adapter>& adapters, const Aircraft& ac,
                    const std::function<bool(const std::string&)>& datarefExists);

} // namespace CduAdapters
