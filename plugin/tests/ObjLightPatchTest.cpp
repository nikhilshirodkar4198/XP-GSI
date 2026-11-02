#include <doctest/doctest.h>

#include "csl/ObjLightPatch.h"
#include "engine/Planner.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// Shape of an X-CSL OBJ (A320 family): nav lights already on the nav
// switch, logo lights always on at ANIM depth 0.
static const char* kObj =
    "I\n800\nOBJ\n\n"
    "TEXTURE A320.png\n"
    "TEXTURE_LIT A320_LIT.png\n"
    "POINT_COUNTS 4 0 0 6\n"
    "ANIM_begin\n"
    "ANIM_hide -1.0 0.0 libxplanemp/controls/nav_lites_on\n"
    "LIGHT_NAMED airplane_nav_left 1 2 3\n"
    "ANIM_end\n"
    "# logo_light_L\n"
    "LIGHT_PARAM full_custom_halo_night 1 2 3 1 1 1 1 0.5 0 0 0 0 0\n"
    "# logo_light_R\n"
    "LIGHT_PARAM full_custom_halo_night -1 2 3 1 1 1 1 0.5 0 0 0 0 0\n"
    "TRIS 0 6\n";

static int count(const std::string& s, const std::string& what) {
    int n = 0;
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) ++n;
    return n;
}

TEST_SUITE("ObjLightPatch") {

static std::string prefixed(const std::string& p) { return "pre_" + p; }

TEST_CASE("always-on lights are wrapped in one logo-switch ANIM_hide; nav lights untouched") {
    bool changed = false;
    const std::string out = ObjLightPatch::patch(kObj, prefixed, changed);
    CHECK(changed);
    // The model's own nav hide stays; one new hide on the logo switch wraps both logo lights.
    CHECK(count(out, "ANIM_hide -1.0 0.0 libxplanemp/controls/nav_lites_on") == 1);
    CHECK(count(out, "ANIM_hide -1.0 0.0 xpgsi/controls/logo_lites_on") == 1);
    CHECK(count(out, "ANIM_begin") == count(out, "ANIM_end"));
    const auto wrap  = out.rfind("ANIM_begin");
    const auto logoL = out.find("full_custom_halo_night 1 ");
    const auto logoR = out.find("full_custom_halo_night -1 ");
    const auto close = out.rfind("ANIM_end");
    CHECK(wrap < logoL);
    CHECK(logoR < close);
    CHECK(close < out.find("TRIS"));   // geometry stays outside the wrap
}

TEST_CASE("relative textures are mapped; absolute ones kept") {
    bool changed = false;
    std::string out = ObjLightPatch::patch(kObj, prefixed, changed);
    CHECK(out.find("TEXTURE pre_A320.png\n") != std::string::npos);
    CHECK(out.find("TEXTURE_LIT pre_A320_LIT.png\n") != std::string::npos);

    out = ObjLightPatch::patch("TEXTURE C:/x/a.png\nTEXTURE /x/b.png\n", prefixed, changed);
    CHECK(out == "TEXTURE C:/x/a.png\nTEXTURE /x/b.png\n");
    CHECK_FALSE(changed);
}

TEST_CASE("an OBJ without always-on lights is unchanged") {
    bool changed = true;
    ObjLightPatch::patch("I\n800\nOBJ\nANIM_begin\nLIGHT_NAMED x 0 0 0\nANIM_end\n", nullptr, changed);
    CHECK_FALSE(changed);
}

TEST_CASE("patchedCopy: copy beside linked textures, no spaces in texture paths") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "xpgsi_objlightpatch_test";
    fs::remove_all(root);
    const fs::path src = root / "Custom Data" / "CSL" / "A320";   // a space, like the real path
    fs::create_directories(src);
    const fs::path lit   = src / "lit.obj";
    const fs::path plain = src / "plain.obj";
    { std::ofstream(lit, std::ios::binary) << kObj; }
    { std::ofstream(plain, std::ios::binary) << "I\n800\nOBJ\nTEXTURE a.png\n"; }
    { std::ofstream(src / "A320.png", std::ios::binary) << "png"; }
    { std::ofstream(src / "A320.dds", std::ios::binary) << "dds"; }      // same-stem sibling
    { std::ofstream(src / "A320_LIT.dds", std::ios::binary) << "lit"; }  // only a .dds on disk
    const fs::path cache = root / "cache" / "obj";
    fs::create_directories(cache);
    { std::ofstream(cache / "0123456789abcdef.obj") << "old layout"; }

    const fs::path copy = ObjLightPatch::patchedCopy(lit, cache);
    REQUIRE(copy != lit);
    CHECK(copy.parent_path().parent_path() == cache);
    CHECK_FALSE(fs::exists(cache / "0123456789abcdef.obj"));            // old flat copies removed

    std::string text;
    {
        std::ifstream f(copy, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    // TEXTURE <8 hex>_A320.png, with the file (and its .dds sibling) beside the copy.
    const auto at = text.find("TEXTURE ");
    REQUIRE(at != std::string::npos);
    const std::string name = text.substr(at + 8, text.find('\n', at) - at - 8);
    CHECK(name.size() == 8 + 1 + 8);
    CHECK(name.find(' ') == std::string::npos);
    CHECK(fs::exists(copy.parent_path() / name));
    const std::string pre = name.substr(0, 9);
    CHECK(fs::exists(copy.parent_path() / (pre + "A320.dds")));
    CHECK(fs::exists(copy.parent_path() / (pre + "A320_LIT.dds")));
    CHECK(text.find("TEXTURE_LIT " + pre + "A320_LIT.png") != std::string::npos);
    CHECK(ObjLightPatch::patchedCopy(lit, cache) == copy);   // cached

    CHECK(ObjLightPatch::patchedCopy(plain, cache) == plain);
    fs::remove_all(root);
}

TEST_CASE("about one static in four is lit at night") {
    int lit = 0;
    const int n = 20000;
    for (int i = 0; i < n; ++i)
        if (Planner::litAtNight(static_cast<std::uint32_t>(i) * 2654435761u)) ++lit;
    CHECK(lit > n * 0.23);
    CHECK(lit < n * 0.27);
}

TEST_CASE("of the lit statics, about one in five shows nav lights only at night") {
    int lit = 0, navOnly = 0;
    for (int i = 0; i < 40000; ++i) {
        const auto seed = static_cast<std::uint32_t>(i) * 2654435761u;
        if (!Planner::litAtNight(seed)) continue;
        ++lit;
        if (!Planner::logoAtNight(seed)) ++navOnly;
    }
    REQUIRE(lit > 0);
    CHECK(navOnly > lit * 0.16);
    CHECK(navOnly < lit * 0.24);
}

} // TEST_SUITE
