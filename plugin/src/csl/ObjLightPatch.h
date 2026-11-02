#pragma once
// ObjLightPatch — copies of CSL OBJs whose always-on lights follow a switch
// of their own, so a parked static can show nav + logo lights, nav lights
// only, or nothing.
//
// Why: X-CSL models draw their logo lights (LIGHT_PARAM full_custom_halo_night
// at ANIM depth 0, "# logo_light_L/R") unconditionally, so every static had
// its logo lit at night. Nav lights already hide on
// libxplanemp/controls/nav_lites_on (ANIM_hide -1.0 0.0 …). Every depth-0
// LIGHT_* line is wrapped in the same kind of ANIM_hide on XP-GSI's own
// instance dataref xpgsi/controls/logo_lites_on (renderer/CslDatarefs).
//
// The copy lives in XP-GSI's own cache (modified OBJs go to our cache,
// CSL folders are read-only). OBJ8 texture paths are relative to the OBJ file
// and end at the first space (X-Plane log 2026-10-26: "Failed to find resource
// '../../../../../Custom'" for a path through "Custom Data"), so a path back to
// the original textures cannot be used. Instead each texture is hard-linked
// (copied if linking fails) next to the patched OBJ under a space-free name,
// together with its same-stem siblings (X-Plane prefers a .dds of the same
// name). Hard links add no disk use and leave the CSL folders untouched.
//
// Pure logic + file IO, no XPLM calls: run on the worker thread.

#include <filesystem>
#include <functional>
#include <string>

namespace ObjLightPatch {

constexpr const char* kLogoDataref = "xpgsi/controls/logo_lites_on";

// Rewrite one OBJ's text. Every relative texture path (TEXTURE, TEXTURE_LIT,
// TEXTURE_NORMAL) is replaced by mapTexture(path); an empty result keeps the
// line unchanged. Sets `changed` when at least one always-on light was wrapped.
std::string patch(const std::string& objText,
                  const std::function<std::string(const std::string&)>& mapTexture,
                  bool& changed);

// Patched copy of `obj` in a sub-folder of `cacheDir` (created if missing or
// stale: names encode the source path, size and modification time), with its
// textures linked beside it. Returns `obj` itself when the OBJ has no
// always-on lights or the copy cannot be made.
std::filesystem::path patchedCopy(const std::filesystem::path& obj,
                                  const std::filesystem::path& cacheDir);

} // namespace ObjLightPatch
