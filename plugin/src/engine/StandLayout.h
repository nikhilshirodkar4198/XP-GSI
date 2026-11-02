#pragma once
// StandLayout — keeps planned statics from overlapping (live test, EGLL):
//  * Footprint: each static's ground outline as two rectangles — fuselage
//    (length × nominal width) and wing (full span × outer-wing chord band) —
//    from the model's measured OBJ extents, placed with its nose gear on the
//    stand point and its nose along the stand heading.
//  * Checker: a planned static is accepted only if its footprint keeps
//    kClearanceM from every footprint already accepted, and if no other
//    stand of the same gate is used. Gates with several parking positions
//    (MARS, e.g. Aerosoft EGLL "305", "305L", "305R") get one aircraft only,
//    as in real life.
//
// Coordinates: metres east / north of an airport reference point
// (equirectangular, fine over an airport). Pure logic, no XPLM calls.

#include <string>
#include <vector>

#include "csl/ObjGeometry.h"

namespace StandLayout {

// Minimum gap between two statics, by the larger one's wingspan: ICAO Annex
// 14 stand clearance for code D–F (7.5 m: an A320 on LOWW
// F11 next to a Qatar 787 on F13-H could not have pushed back); 3 m up to
// code C (ICAO's 4.5 m for C would empty gates that sceneries space tighter
// than ICAO). kClearanceM is the old fixed gap, for callers that pass one.
constexpr double kClearanceM      = 1.5;
constexpr double kClearanceSmallM = 3.0;    // both up to code C (span < 36 m)
constexpr double kClearanceLargeM = 7.5;    // either code D or larger
constexpr double kCodeDSpanM      = 36.0;
double clearanceFor(double spanA, double spanB);
constexpr double kSameGateM    = 120.0;  // same base name within this → same gate

struct Rect {
    double cx = 0, cy = 0;         // centre, metres east / north
    double ux = 0, uy = 1;         // unit vector of the long (forward) axis
    double halfLen = 0, halfWid = 0;
};

struct Footprint {
    Rect fuselage;
    bool hasWing = false;
    Rect wing;
    bool hasInner = false;
    Rect inner;         // inner wing and engines (swept wings: forward of `wing`)
    bool hasTail = false;
    Rect tail;          // horizontal stabiliser (a neighbour's wingtip can reach it)
    double spanM = 0;   // wingspan (clearanceFor)
};

// Model outline in the stand frame: f = metres forward of the stand point
// along the heading, r = metres to the right.
struct Shape {
    double fMin = 0, fMax = 0, halfWidth = 0;          // fuselage
    bool   hasWing = false;
    double wingFMin = 0, wingFMax = 0, wingRMin = 0, wingRMax = 0;
    bool   hasInner = false;
    double innerFMin = 0, innerFMax = 0, innerHalfSpan = 0;
    bool   hasTail = false;
    double tailFMin = 0, tailFMax = 0, tailHalfSpan = 0;
};

// Horizontal tail: aft kTailLengthFraction of the length, kTailSpanFraction
// of the half-span wide each side (A320: 12.45 m of 35.8 m; B777: 21.5 m of
// 64.8 m → ~0.35).
constexpr double kTailLengthFraction = 0.15;
constexpr double kTailSpanFraction   = 0.37;

// From measured OBJ extents (OBJ -Z forward, +X right) and the nose-gear
// offset the renderer applies. Invalid extents → a 40 × 36 m narrowbody box.
Shape shapeFor(const ObjGeometry::Extents& e, double noseGearFwdM);

// The stub rectangle (static_stub.obj: 4 × 10 m, behind its origin).
Shape stubShape();

Footprint place(const Shape& s, double east, double north, double headingDeg);

// Separating-axis test on the rectangles, with `clearance` between them
// (< 0: clearanceFor the two wingspans).
bool overlaps(const Footprint& a, const Footprint& b, double clearance = -1.0);

// Gate key for multi-position gates: a trailing L / R / C after a digit is
// dropped ("305L" → "305", "Stand 305R" → "STAND 305"); everything else is
// the upper-cased name.
std::string gateKey(const std::string& standName);

// Gate keys for all of an airport's stands (same order). Beyond gateKey, a
// trailing letter after a digit marks an alternative position of the
// plain-numbered stand when that stand exists: SFD KLAX has "201" (one
// widebody) and "201A" / "201B" (two narrowbodies) → all "201". Without the
// plain stand the letters are separate gates (KLAX "52A" … "52I"). A name
// shared by several stands names an apron, not a gate (Global Airports KSDF:
// 14 × "UPS Ramp 7"): each of them gets a key of its own.
std::vector<std::string> gateKeys(const std::vector<std::string>& standNames);

// Fill order with the positions of each multi-position gate together, at the
// place of the gate's first position in `order`, largest stand first: the
// first position tried takes the gate, so a random narrowbody side position
// must not win over the widebody one (Global Airports VIDP
// "A07" E with "A07L" / "A07R" C — heavies only at remote stands).
// keys[i] / sizes[i] per stand; `order` indexes them; returns a permutation of it.
std::vector<std::size_t> largestFirstPerGate(const std::vector<std::size_t>& order,
                                             const std::vector<std::string>& keys,
                                             const std::vector<char>& sizes);

// Stand points this close are alternative positions of one gate whatever
// their names (Global Airports VOBL "2" / "3", 17 m apart).
constexpr double kAlternateM = 25.0;

// Equirectangular projection around (refLat, refLon).
void toLocal(double lat, double lon, double refLat, double refLon, double& east, double& north);

class Checker {
public:
    enum class Verdict { Fits, SameGate, Overlap };
    Verdict check(const Footprint& fp, const std::string& gate, double east, double north) const;
    // True if this static can be added (no overlap, gate not taken).
    bool fits(const Footprint& fp, const std::string& gate, double east, double north) const {
        return check(fp, gate, east, north) == Verdict::Fits;
    }
    void add(const Footprint& fp, const std::string& gate, double east, double north);
    std::size_t size() const { return m_items.size(); }

private:
    struct Item { Footprint fp; std::string gate; double east, north; };
    std::vector<Item> m_items;
};

} // namespace StandLayout
