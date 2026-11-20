#!/usr/bin/env python3
"""Zip the project (excluding build/, .git, __pycache__).
Usage: python tools/package.py [--out grand-city-6-engine.zip]
"""
import argparse, os, zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP = {"build", ".git", "__pycache__", ".vs"}
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(ROOT), "grand-city-6-engine.zip"))
    args = ap.parse_args()
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, dirnames, filenames in os.walk(ROOT):
            dirnames[:] = [d for d in dirnames if d not in SKIP]
            for fn in filenames:
                p = os.path.join(dirpath, fn)
                z.write(p, os.path.relpath(p, os.path.dirname(ROOT)))
    print("Wrote", args.out, os.path.getsize(args.out), "bytes")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
