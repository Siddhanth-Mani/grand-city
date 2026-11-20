#!/usr/bin/env python3
"""One-command configure + build wrapper.
Usage: python tools/build.py [--renderer d3d12|vulkan|null] [--config Release|Debug] [--run] [--headless]
On Windows the default renderer is d3d12; elsewhere it is null.
"""
import argparse, os, platform, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit(r.returncode)
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--renderer", default="d3d12" if platform.system() == "Windows" else "null")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--frames", type=int, default=600)
    args = ap.parse_args()
    build = os.path.join(ROOT, "build")
    run(["cmake", "-S", ROOT, "-B", build, f"-DGC6_RENDERER={args.renderer}",
         f"-DCMAKE_BUILD_TYPE={args.config}"])
    run(["cmake", "--build", build, "--config", args.config, "-j", str(os.cpu_count() or 4)])
    print("Build OK:", build)
    if args.run:
        exe = os.path.join(build, "grand-city-6.exe" if platform.system() == "Windows" else "grand-city-6")
        cmd = [exe]
        if args.headless:
            cmd += ["--headless", "--frames", str(args.frames)]
        run(cmd, cwd=ROOT)
    return 0
if __name__ == "__main__":
    sys.exit(main())
