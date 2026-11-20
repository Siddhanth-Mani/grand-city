#!/usr/bin/env python3
"""Summarise a Grand City 6 profile log (profile.log).

The engine writes one line per section:
    PROFILE <name> <total_ms> <calls>
Usage: python tools/profiler.py [profile.log]
"""
import sys

def main(path="profile.log"):
    rows = []
    try:
        lines = open(path).read().splitlines()
    except FileNotFoundError:
        print(f"No profile log at {path} — run the game first (it dumps profile.log on exit).")
        return 1
    for line in lines:
        parts = line.split()
        if len(parts) != 4 or parts[0] != "PROFILE":
            continue
        name, total, calls = parts[1], float(parts[2]), int(parts[3])
        rows.append((name, total, calls, total / calls if calls else 0.0))
    if not rows:
        print(f"No PROFILE lines in {path}.")
        return 1
    total_all = sum(r[1] for r in rows)
    print(f"Profile summary ({path}): {len(rows)} sections, {total_all:.1f} ms total")
    print(f"{'section':<22}{'total ms':>12}{'calls':>10}{'avg ms':>10}{'%':>7}")
    for name, total, calls, avg in sorted(rows, key=lambda r: -r[1]):
        print(f"{name:<22}{total:>12.2f}{calls:>10}{avg:>10.3f}{100*total/total_all:>6.1f}%")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "profile.log"))
