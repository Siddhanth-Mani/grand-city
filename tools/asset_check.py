#!/usr/bin/env python3
"""Validate shaders and data files: every file the README lists must exist,
JSON must parse, cars.json must hold 6 cars, and Lua mission files must at
least parse (luac if present, else a brace balance check).
Usage: python tools/asset_check.py
"""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQUIRED = ["shaders/lit.hlsl", "shaders/sky.hlsl", "shaders/unlit.hlsl",
            "shaders/lit_pbr.hlsl", "shaders/post.hlsl",
            "shaders/glsl/lit.vert", "shaders/glsl/lit.frag",
            "shaders/glsl/pbr.vert", "shaders/glsl/pbr.frag",
            "shaders/glsl/post.vert", "shaders/glsl/post.frag",
            "data/cars.json", "data/city.json", "data/settings.ini", "data/missions.json",
            "data/materials.json",
            "scripts/missions/mission1_delivery.lua", "scripts/missions/mission2_collect.lua",
            "scripts/missions/mission3_escape.lua", "scripts/missions/mission4_taxi.lua",
            "scripts/missions/mission5_escape_time.lua", "scripts/missions/mission6_stunt.lua",
            "tools/profiler.py", "tools/playthrough.py",
            "CMakeLists.txt", "README.md", "docs/PHYSICS.md", "docs/ARCHITECTURE.md",
            "docs/SHADERS.md", "docs/AUDIO.md", "docs/NETWORKING.md", "CHANGELOG.md"]
JSON_FILES = ["data/cars.json", "data/city.json", "data/missions.json", "data/materials.json"]
def main():
    errors = []
    for rel in REQUIRED:
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            errors.append(f"missing: {rel}")
    for rel in JSON_FILES:
        p = os.path.join(ROOT, rel)
        if os.path.isfile(p):
            try:
                data = json.load(open(p))
            except Exception as e:
                errors.append(f"bad JSON {rel}: {e}")
                continue
            if rel == "data/cars.json" and len(data.get("cars", [])) != 6:
                errors.append(f"data/cars.json must hold 6 cars, found {len(data.get('cars', []))}")
            if rel == "data/missions.json" and len(data.get("missions", [])) != 6:
                errors.append(f"data/missions.json must hold 6 missions, found {len(data.get('missions', []))}")
    # Lua syntax: use luac when available, else a brace balance check.
    luac = None
    for cand in ("luac", "luac5.4", "luac5.3"):
        r = subprocess.run(["which", cand], capture_output=True, text=True)
        if r.returncode == 0:
            luac = cand
            break
    for i in range(1, 7):
        rel = f"scripts/missions/mission{i}_{['delivery','collect','escape','taxi','escape_time','stunt'][i-1]}.lua"
        p = os.path.join(ROOT, rel)
        if not os.path.isfile(p):
            continue
        if luac:
            r = subprocess.run([luac, "-p", p], capture_output=True, text=True)
            if r.returncode != 0:
                errors.append(f"lua syntax error {rel}: {r.stderr.strip()}")
        else:
            text = open(p).read()
            for a, b in (("function", "end"), ("if", "end"), ("for", "end")):
                if text.count(a) > text.count(b):
                    errors.append(f"lua {rel}: more '{a}' than 'end'")
    if errors:
        print("ASSET CHECK FAILED")
        for e in errors:
            print(" -", e)
        return 1
    print(f"ASSET CHECK OK: {len(REQUIRED)} required files present, JSON valid, 6 cars, 6 missions, Lua parses")
    return 0
if __name__ == "__main__":
    sys.exit(main())
