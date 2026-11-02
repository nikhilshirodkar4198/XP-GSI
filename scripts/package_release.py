#!/usr/bin/env python3
"""Build the XP-GSI release package and zip from a finished build.

    python scripts/package_release.py --build build --out release [--mac DIR] [--lin DIR]

Writes <out>/XP-GSI/ (what users drop into Resources/plugins) and
<out>/XP-GSI-v<version>.zip with exactly one top-level XP-GSI/ folder.

  * win_x64/XP-GSI.xpl from <build>/XP-GSI/win_x64 (the PDB stays behind:
    keep it privately for crash analysis); mac_x64 / lin_x64 from --mac /
    --lin (the CI artifacts), left out of the zip when not given.
  * data/ from plugin/data: JSON minified, fields the plugin never reads
    dropped (checked against the loaders), hand-written notes removed.
  * LICENSES/ from packaging/LICENSES, README.txt from packaging/README.txt.

Standard library only. Version from plugin/CMakeLists.txt (project VERSION + XPGSI_VERSION_SUFFIX).
"""
import argparse
import json
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Top-level keys the plugin never reads (grep of plugin/src), per file.
# "source" / "license" stay: they carry the data's attribution.
DROP_TOP = {
    "*": {"description"},
    "wiki_airlines.json": {"params"},
    "csl_livery_overrides.json": {"defunct_airline_notes"},
}
# Keys dropped wherever they appear in a file.
DROP_DEEP = {
    "airports": {"excluded"},                 # data/airports/*.json
    "wiki_airlines.json": {"traffic"},
    "stand_overrides.json": {"note", "area"},
}


def version():
    with open(os.path.join(ROOT, "plugin", "CMakeLists.txt"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"project\s*\([^)]*VERSION\s+([0-9.]+)", text, re.S)
    s = re.search(r'XPGSI_VERSION_SUFFIX\s+"([^"]*)"', text)
    if not m:
        sys.exit("project VERSION not found in plugin/CMakeLists.txt")
    return m.group(1) + (s.group(1) if s else "")


def strip(v, keys):
    if isinstance(v, dict):
        return {k: strip(x, keys) for k, x in v.items() if k not in keys}
    if isinstance(v, list):
        return [strip(x, keys) for x in v]
    return v


def pack_json(src, dst, group):
    with open(src, encoding="utf-8") as f:
        doc = json.load(f)
    if isinstance(doc, dict):
        for k in DROP_TOP["*"] | DROP_TOP.get(group, set()):
            doc.pop(k, None)
        if group == "airports" and isinstance(doc.get("source"), str):
            # The per-file list of sample dates: the attribution stays.
            doc["source"] = doc["source"].split(", sampled")[0]
        if group == "major_hubs.json" and isinstance(doc.get("airports"), dict):
            # Only the codes are read; the notes are working remarks.
            doc["airports"] = {k: "" for k in doc["airports"]}
    doc = strip(doc, DROP_DEEP.get(group, set()))
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, separators=(",", ":"))


def copy_data(src_dir, dst_dir):
    for dirpath, _, files in os.walk(src_dir):
        rel = os.path.relpath(dirpath, src_dir)
        out = os.path.join(dst_dir, rel) if rel != "." else dst_dir
        os.makedirs(out, exist_ok=True)
        for name in sorted(files):
            s, d = os.path.join(dirpath, name), os.path.join(out, name)
            if name.endswith(".json"):
                pack_json(s, d, "airports" if rel == "airports" else name)
            elif name.lower() != "readme.md":   # developer notes; attribution is in LICENSES/
                shutil.copyfile(s, d)


def copy_binary(src_dir, dst_dir):
    xpl = os.path.join(src_dir, "XP-GSI.xpl")
    if not os.path.isfile(xpl):
        return False
    os.makedirs(dst_dir, exist_ok=True)
    shutil.copyfile(xpl, os.path.join(dst_dir, "XP-GSI.xpl"))
    return True


def make_zip(pkg, zip_path):
    base = os.path.dirname(pkg)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, dirs, files in os.walk(pkg):
            dirs.sort()
            for name in sorted(files):
                p = os.path.join(dirpath, name)
                z.write(p, os.path.relpath(p, base).replace(os.sep, "/"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"), help="CMake build folder (Release)")
    ap.add_argument("--out", default=os.path.join(ROOT, "release"), help="output folder")
    ap.add_argument("--mac", help="folder holding the macOS XP-GSI.xpl (CI artifact)")
    ap.add_argument("--lin", help="folder holding the Linux XP-GSI.xpl (CI artifact)")
    args = ap.parse_args()

    ver = version()
    pkg = os.path.join(args.out, "XP-GSI")
    if os.path.isdir(pkg):
        shutil.rmtree(pkg)
    os.makedirs(pkg)

    if not copy_binary(os.path.join(args.build, "XP-GSI", "win_x64"), os.path.join(pkg, "win_x64")):
        sys.exit("win_x64/XP-GSI.xpl not found in the build folder: build Release first")
    built = ["win_x64"]
    for plat, d in (("mac_x64", args.mac), ("lin_x64", args.lin)):
        if d and copy_binary(d, os.path.join(pkg, plat)):
            built.append(plat)

    copy_data(os.path.join(ROOT, "plugin", "data"), os.path.join(pkg, "data"))
    shutil.copytree(os.path.join(ROOT, "packaging", "LICENSES"), os.path.join(pkg, "LICENSES"))
    with open(os.path.join(ROOT, "packaging", "README.txt"), encoding="utf-8") as f:
        readme = f.read().replace("@VERSION@", ver)
    with open(os.path.join(pkg, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(readme)

    zip_path = os.path.join(args.out, f"XP-GSI-v{ver}.zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    make_zip(pkg, zip_path)

    size = sum(os.path.getsize(os.path.join(dp, n)) for dp, _, fs in os.walk(pkg) for n in fs)
    print(f"XP-GSI v{ver}: platforms {', '.join(built)}; package {size / 1e6:.1f} MB; "
          f"{zip_path} {os.path.getsize(zip_path) / 1e6:.1f} MB")
    for plat in ("mac_x64", "lin_x64"):
        if plat not in built:
            print(f"note: no {plat} binary given - not in the package")


if __name__ == "__main__":
    main()
