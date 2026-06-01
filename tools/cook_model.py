#!/usr/bin/env python3
"""Cook a source model (glTF/FBX/OBJ/...) into the engine's portable ".pemesh" format using the
desktop PhasmaPlayer cook host. Cross-platform (Windows/Linux).

The player (desktop and Android) loads only ".pemesh"; Assimp is editor/host-only and is used here,
on the desktop, purely to import the source model and cook it. This mirrors tools/bake_android_shaders.ps1:
a desktop host produces an asset the Assimp-less, runtime-compiler-less device consumes.

Steps:
  1. (optional) Build the desktop PhasmaPlayer (the cook host) with --build.
  2. Run PhasmaPlayer in cook mode (PHASMA_COOK_MODEL / PHASMA_COOK_OUT) to import <source> via Assimp
     and write <name>.pemesh plus referenced textures in a self-contained layout.

Cooked assets are build artifacts and never live under PhasmaEditor/Assets (the source tree).

Usage:
  python tools/cook_model.py <source> [--name NAME] [--build-dir DIR] [--config CFG]
                             [--out-dir DIR] [--build]
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
# TODO: bump if the local Visual Studio install path changes.
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"


def build_player(build_dir: str, config: str) -> bool:
    target = ["cmake", "--build", build_dir, "--config", config, "--target", "PhasmaPlayer"]
    if os.name == "nt" and os.path.isfile(VCVARS):
        # Wrap in the VS dev environment so cl/link/ninja are on PATH.
        cmd = f'call "{VCVARS}" >nul 2>&1 && ' + " ".join(target)
        return subprocess.run(cmd, cwd=str(REPO_ROOT), shell=True).returncode == 0
    return subprocess.run(target, cwd=str(REPO_ROOT)).returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Cook a source model into .pemesh via the PhasmaPlayer cook host.")
    ap.add_argument("source", help="Path to the source model file (e.g. .../Sponza.gltf)")
    ap.add_argument("--name", default="", help="Cooked model name (folder + .pemesh stem). Defaults to the source stem.")
    ap.add_argument("--build-dir", default="build-ninja-full")
    ap.add_argument("--config", default="Release")
    # Cooked models are BUILD ARTIFACTS, not source, so they live in a gitignored staging folder — never
    # under PhasmaEditor/Assets. Default is the Android APK staging; pass e.g.
    # build-ninja-full/Release/Assets/Models to cook for the desktop player's config folder.
    ap.add_argument("--out-dir", default="PhasmaPlayer/android/prebaked/Models",
                    help="Where cooked models land (gitignored staging).")
    ap.add_argument("--build", action="store_true", help="Rebuild the desktop PhasmaPlayer cook host first.")
    args = ap.parse_args()

    source = Path(args.source)
    if not source.is_file():
        print(f"[cook] ERROR: source not found: {source}", file=sys.stderr)
        return 1
    name = args.name or source.stem

    bin_dir = REPO_ROOT / args.build_dir / args.config
    player = bin_dir / ("PhasmaPlayer.exe" if os.name == "nt" else "PhasmaPlayer")
    models_dir = REPO_ROOT / args.out_dir / name
    out_pemesh = models_dir / f"{name}.pemesh"

    print(f"[cook] source : {source.resolve()}")
    print(f"[cook] name   : {name}")
    print(f"[cook] output : {out_pemesh}")

    if args.build:
        print("[cook] building cook host (PhasmaPlayer)...")
        if not build_player(args.build_dir, args.config):
            print("[cook] ERROR: build failed", file=sys.stderr)
            return 1

    if not player.is_file():
        print(f"[cook] ERROR: PhasmaPlayer not found (use --build): {player}", file=sys.stderr)
        return 1

    models_dir.mkdir(parents=True, exist_ok=True)
    if out_pemesh.exists():
        out_pemesh.unlink()

    # Cook via the desktop player's cook mode.
    env = dict(os.environ)
    env["PHASMA_COOK_MODEL"] = str(source.resolve())
    env["PHASMA_COOK_OUT"] = str(out_pemesh)
    print("[cook] running cook host...")
    try:
        proc = subprocess.run([str(player), "--api", "vulkan"], cwd=str(bin_dir), env=env, timeout=600)
    except subprocess.TimeoutExpired:
        print("[cook] ERROR: cook host timed out", file=sys.stderr)
        return 1

    if proc.returncode != 0 or not out_pemesh.exists():
        print(f"[cook] ERROR: cook failed (exit {proc.returncode}). See {bin_dir / 'PhasmaEngine.log'}", file=sys.stderr)
        return 1

    size_kb = out_pemesh.stat().st_size / 1024.0
    print(f"[cook] cooked {out_pemesh} ({size_kb:.1f} KB); referenced textures copied by the cook host")
    print(f"[cook] done: {args.out_dir}/{name}/ is self-contained ({name}.pemesh + textures).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
