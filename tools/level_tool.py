#!/usr/bin/env python3
"""Generate data/city.json from parameters (seed, size, road spacing).
Usage: python tools/level_tool.py [--seed 42] [--size 400] [--spacing 40]
The C++ City::generate uses the same deterministic algorithm; this file is the
authoring-side twin used to preview/inspect a city without running the game.
"""
import argparse, json, os, random
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--size", type=float, default=400.0)
    ap.add_argument("--spacing", type=float, default=40.0)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    buildings, roads = [], []
    half = int(args.size / 2)
    gx = -half
    while gx < half:
        gz = -half
        while gz < half:
            roads.append({"x": gx, "z": gz})
            if gz >= -half + 40 and not (abs(gx) < 25 and abs(gz) < 25):
                h = round(rng.uniform(8, 55), 1)
                buildings.append({"x": gx + args.spacing / 2, "z": gz + args.spacing / 2,
                                  "w": round(18 + rng.random() * 10, 1), "height": h})
            gz += int(args.spacing)
        gx += int(args.spacing)
    out = {"seed": args.seed, "size": args.size, "spacing": args.spacing,
           "buildings": buildings, "roadNodes": roads}
    path = os.path.join(ROOT, "data", "city.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(out, f, indent=2)
    print(f"Wrote {path}: {len(buildings)} buildings, {len(roads)} road nodes")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
