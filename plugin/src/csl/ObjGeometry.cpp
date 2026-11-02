#include "csl/ObjGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ObjGeometry {

namespace {

struct Vtx { float x, y, z; };

constexpr const char* kGearDataref = "libxplanemp/controls/gear_ratio";

// Geometry of one OBJ file: bounding box of its vertices plus the vertices
// drawn by TRIS commands nested in a gear_ratio animation.
struct FileGeom {
    bool             valid = false;
    float            minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    std::vector<Vtx> gear;
    float            halfSpan = 0;                       // max |X|
    bool             wingFound = false;
    float            wingMinZ = 0, wingMaxZ = 0;         // outer-wing Z band
    bool             innerFound = false;
    float            innerMinZ = 0, innerMaxZ = 0;       // inner wing + engines Z band
};

void parseObj(const std::filesystem::path& path, FileGeom& out) {
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::vector<Vtx>      verts;    // this file's VT table (indices are per file)
    std::vector<unsigned> idx;      // IDX / IDX10 table
    // Per open ANIM_begin: does this block (or an enclosing one) animate on gear_ratio?
    std::vector<bool>     animGear;
    struct TrisCmd { unsigned offset, count; };
    std::vector<TrisCmd>  gearTris; // resolved after the whole file is read

    // Hand-rolled tokenising (strtof/strtoul): OBJs are 0.5–2 MB and
    // istringstream made a model take ~160 ms.
    std::string line;
    while (std::getline(f, line)) {
        const char* p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;
        const char* cmd = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') ++p;
        const std::size_t len = static_cast<std::size_t>(p - cmd);
        auto is = [&](const char* kw) {
            return std::strlen(kw) == len && std::strncmp(cmd, kw, len) == 0;
        };
        char* end = nullptr;

        if (is("VT")) {                            // VT x y z nx ny nz s t
            Vtx v{};
            v.x = std::strtof(p, &end); if (end == p) continue; p = end;
            v.y = std::strtof(p, &end); if (end == p) continue; p = end;
            v.z = std::strtof(p, &end); if (end == p) continue;
            verts.push_back(v);
        } else if (is("IDX") || is("IDX10")) {
            for (;;) {
                unsigned long i = std::strtoul(p, &end, 10);
                if (end == p) break;
                idx.push_back(static_cast<unsigned>(i));
                p = end;
            }
        } else if (is("ANIM_begin")) {
            animGear.push_back(!animGear.empty() && animGear.back());
        } else if (is("ANIM_end")) {
            if (!animGear.empty()) animGear.pop_back();
        } else if (len > 5 && std::strncmp(cmd, "ANIM_", 5) == 0) {  // _hide / _show / _rotate / _trans …
            if (!animGear.empty() && line.find(kGearDataref) != std::string::npos)
                animGear.back() = true;
        } else if (is("TRIS")) {
            if (animGear.empty() || !animGear.back()) continue;
            unsigned long off = std::strtoul(p, &end, 10); if (end == p) continue; p = end;
            unsigned long cnt = std::strtoul(p, &end, 10); if (end == p) continue;
            gearTris.push_back({static_cast<unsigned>(off), static_cast<unsigned>(cnt)});
        }
    }

    if (verts.empty()) return;
    out.valid = true;
    out.minX = out.maxX = verts[0].x;
    out.minY = out.maxY = verts[0].y;
    out.minZ = out.maxZ = verts[0].z;
    for (const auto& v : verts) {
        out.minX = std::min(out.minX, v.x); out.maxX = std::max(out.maxX, v.x);
        out.minY = std::min(out.minY, v.y); out.maxY = std::max(out.maxY, v.y);
        out.minZ = std::min(out.minZ, v.z); out.maxZ = std::max(out.maxZ, v.z);
    }
    for (const auto& t : gearTris)
        for (unsigned k = t.offset; k < t.offset + t.count && k < idx.size(); ++k)
            if (idx[k] < verts.size()) out.gear.push_back(verts[idx[k]]);

    out.halfSpan = std::max(std::fabs(out.minX), std::fabs(out.maxX));
    // The horizontal tail sits in the inner band's |X| range: only vertices
    // ahead of the aft kTailExcludeFraction of the length count there.
    const float tailStart = out.maxZ - kTailExcludeFraction * (out.maxZ - out.minZ);
    for (const auto& v : verts) {
        const float ax = std::fabs(v.x);
        if (ax >= kOuterWingFraction * out.halfSpan) {
            if (!out.wingFound) { out.wingMinZ = out.wingMaxZ = v.z; out.wingFound = true; }
            out.wingMinZ = std::min(out.wingMinZ, v.z);
            out.wingMaxZ = std::max(out.wingMaxZ, v.z);
        } else if (ax >= kInnerWingFraction * out.halfSpan && v.z < tailStart) {
            if (!out.innerFound) { out.innerMinZ = out.innerMaxZ = v.z; out.innerFound = true; }
            out.innerMinZ = std::min(out.innerMinZ, v.z);
            out.innerMaxZ = std::max(out.innerMaxZ, v.z);
        }
    }
}

// Liveries of one type share OBJ files (fan / airframe parts), so results are
// cached per file for the life of the plugin. Guarded: measure() is called
// from the worker thread, but nothing stops a second caller.
std::shared_ptr<const FileGeom> fileGeom(const std::filesystem::path& path) {
    static std::mutex mux;
    static std::unordered_map<std::string, std::shared_ptr<const FileGeom>> cache;
    const std::string key = path.generic_u8string();
    {
        std::lock_guard<std::mutex> lk(mux);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    auto g = std::make_shared<FileGeom>();
    parseObj(path, *g);
    std::lock_guard<std::mutex> lk(mux);
    return cache.emplace(key, std::move(g)).first->second;
}

} // namespace

Extents measure(const std::vector<std::filesystem::path>& objParts) {
    Extents e;
    std::vector<Vtx> gear;
    float widestHalfSpan = -1.0f;
    for (const auto& part : objParts) {
        auto g = fileGeom(part);
        if (!g->valid) continue;
        // The wing band comes from the widest part (the airframe, not e.g.
        // a separate engine-fan OBJ).
        if (g->wingFound && g->halfSpan > widestHalfSpan) {
            widestHalfSpan = g->halfSpan;
            e.wingFound = true;
            e.wingMinZ  = g->wingMinZ;
            e.wingMaxZ  = g->wingMaxZ;
            e.innerWingFound = g->innerFound;
            e.innerWingMinZ  = g->innerMinZ;
            e.innerWingMaxZ  = g->innerMaxZ;
        }
        if (!e.valid) {
            e.minX = g->minX; e.maxX = g->maxX;
            e.minY = g->minY; e.maxY = g->maxY;
            e.minZ = g->minZ; e.maxZ = g->maxZ;
            e.valid = true;
        }
        e.minX = std::min(e.minX, g->minX); e.maxX = std::max(e.maxX, g->maxX);
        e.minY = std::min(e.minY, g->minY); e.maxY = std::max(e.maxY, g->maxY);
        e.minZ = std::min(e.minZ, g->minZ); e.maxZ = std::max(e.maxZ, g->maxZ);
        gear.insert(gear.end(), g->gear.begin(), g->gear.end());
    }
    if (!e.valid) return e;

    // Nose gear: centreline gear geometry in the front half of the airframe.
    const float midZ = 0.5f * (e.minZ + e.maxZ);
    std::vector<Vtx> nose;
    for (const auto& v : gear)
        if (std::fabs(v.x) <= kCentrelineM && v.z < midZ) nose.push_back(v);
    if (nose.empty()) return e;

    float lowY = nose[0].y;
    for (const auto& v : nose) lowY = std::min(lowY, v.y);
    // Contact patch: the lowest vertices; their Z span is the tyre footprint.
    float zMin = 0, zMax = 0;
    bool  any  = false;
    for (const auto& v : nose) {
        if (v.y > lowY + kContactBandM) continue;
        if (!any) { zMin = zMax = v.z; any = true; }
        zMin = std::min(zMin, v.z);
        zMax = std::max(zMax, v.z);
    }
    e.noseGearFound = true;
    e.noseGearZ     = 0.5f * (zMin + zMax);
    e.noseGearY     = lowY;
    return e;
}

bool noseGearPlausible(const Extents& e) {
    if (!e.valid || !e.noseGearFound) return false;
    const float length = e.maxZ - e.minZ;
    if (length <= 0.0f) return false;
    const float behindNose = (e.noseGearZ - e.minZ) / length;
    return behindNose >= kNoseGearMinFraction && behindNose <= kNoseGearMaxFraction;
}

float noseGearForwardM(const Extents& e) {
    if (!e.valid) return 0.0f;
    const float z = noseGearPlausible(e)
        ? e.noseGearZ
        : e.minZ + kNoseGearFractionOfLength * (e.maxZ - e.minZ);
    return -z;   // -Z is forward
}

} // namespace ObjGeometry
