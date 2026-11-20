#!/usr/bin/env python3
"""Compile HLSL shaders with dxc/fxc and GLSL with glslangValidator when available.
Skips gracefully when a compiler is missing (CI/Linux) — the game still runs
on the null renderer without compiled shaders.
Usage: python tools/compile_shaders.py [--out build/shaders]
"""
import argparse, os, shutil, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def run(cmd):
    print("+", " ".join(cmd))
    return subprocess.run(cmd).returncode == 0
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "shaders"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    dxc = shutil.which("dxc") or shutil.which("fxc")
    if dxc:
        for name, profile, entry in [("lit", "vs_6_0", "VSMain"), ("lit", "ps_6_0", "PSMain"),
                                     ("sky", "vs_6_0", "VSMain"), ("sky", "ps_6_0", "PSMain"),
                                     ("unlit", "vs_6_0", "VSMain"), ("unlit", "ps_6_0", "PSMain")]:
            src = os.path.join(ROOT, "shaders", name + ".hlsl")
            dst = os.path.join(args.out, f"{name}_{profile.replace('_6_0','')}.cso")
            ok = run([dxc, "-T", profile, "-E", entry, src, "-Fo", dst])
            print(("OK  " if ok else "FAIL") + f" {name} {profile}")
    else:
        print("dxc/fxc not found — skipping HLSL compile (fine on Linux/CI).")
    glslang = shutil.which("glslangValidator")
    if glslang:
        for f in ["lit.vert", "lit.frag"]:
            src = os.path.join(ROOT, "shaders", "glsl", f)
            dst = os.path.join(args.out, f + ".spv")
            ok = run([glslang, "-V", src, "-o", dst])
            print(("OK  " if ok else "FAIL") + f" {f}")
    else:
        print("glslangValidator not found — skipping SPIR-V compile.")
    return 0
if __name__ == "__main__":
    sys.exit(main())
