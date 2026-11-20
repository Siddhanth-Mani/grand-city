#!/usr/bin/env python3
"""Headless full playthrough check for Grand City 6.

Runs the game binary headless and checks the log for the playthrough
markers the test suite also verifies in-process:
  1. world initialises (city, cars, packages, jumps)
  2. 600+ frames complete without crashing
  3. profile.log is written on exit
Usage: python tools/playthrough.py [--frames 600]
"""
import re, subprocess, sys, os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "grand-city-6")

def main():
    frames = 600
    if "--frames" in sys.argv:
        frames = int(sys.argv[sys.argv.index("--frames") + 1])
    if not os.path.isfile(BIN):
        print(f"Binary not found: {BIN} — build first (python tools/build.py)")
        return 1
    env = dict(os.environ, GC6_NO_AUDIO="1")
    r = subprocess.run([BIN, "--headless", "--frames", str(frames)],
                       cwd=ROOT, env=env, capture_output=True, text=True, timeout=300)
    log = open(os.path.join(ROOT, "grand-city-6.log")).read() if os.path.exists(
        os.path.join(ROOT, "grand-city-6.log")) else ""
    checks = []
    checks.append(("exit code 0", r.returncode == 0))
    checks.append(("world initialised", "World initialised" in log))
    checks.append((f"{frames} frames completed",
                   f"exiting after {frames} frames" in log))
    checks.append(("profile.log written", os.path.isfile(os.path.join(ROOT, "profile.log"))))
    ok = True
    for name, passed in checks:
        print(("PASS: " if passed else "FAIL: ") + name)
        ok = ok and passed
    print("PLAYTHROUGH OK" if ok else "PLAYTHROUGH FAILED")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
