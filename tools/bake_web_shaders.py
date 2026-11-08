"""Bake the browser player's shader cache (WebGPU RHI spec decisions 7 and 13).

Runs a private copy of the desktop Vulkan player under the WebGPU caps profile (PHASMA_CAPS_PROFILE=webgpu) at
PHASMA_SPIRV_TARGET=1.2, so it compiles the same shader variants under the same keys the browser computes, then
converts its ShaderCache to WGSL with tools/convert_web_shaders.py and copies _spv and _wgsl into <out>/ShaderCache.
The wasm-player build preloads that folder into MEMFS at /ShaderCache.

A key covers the expanded source, entry point, defines and SPIR-V target, not the file path, so a desktop run started
like the browser (same project, same startup scene) compiles what the browser asks for. A shader the run never
compiles stays missing and the browser player names its key: run longer, or open the same scene.

With --gi (needs --project and global_illumination on in the scene), a first run with ray tracing writes <scene>.pegi
beside the project's startup scene: the static probes the browser's lighting reads. Moving lights or geometry afterwards
needs a new bake.

Usage: python tools/bake_web_shaders.py [--build build-p1-desktop/Release] [--project <dir>] [--seconds 40]
                                        [--out build-wasm-player/web-shader-cache] [--gi] [--gi-seconds 240]
Exit 0 when every compiled module converts and any requested probe bake is written, 1 otherwise (the converted ones are
still copied).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def private_player(build, work, name="player"):
    """The desktop player with its own root: exe, DLLs, engine assets and Chrome's limits; no ShaderCache."""
    player = work / name
    player.mkdir()
    shutil.copy2(build / "PhasmaPlayer.exe", player)
    for dll in build.glob("*.dll"):
        if "_live_" not in dll.name:
            shutil.copy2(dll, player)

    for folder in ("RuntimeAssets", "MaterialX"):
        if (build / folder).exists():
            shutil.copytree(build / folder, player / folder)

    if (build / "webgpu_limits.json").exists():
        shutil.copy2(build / "webgpu_limits.json", player)

    return player


def bake_probes(player, project, seconds):
    """Runs the player with ray tracing until GlobalIlluminationPass logs its bake of the startup scene."""
    settings = {"project_manifest": str(Path(project).resolve() / "phasma_project.json"), "bake_gi": True}
    (player / "phasma_settings.json").write_text(json.dumps(settings), encoding="utf-8")
    env = dict(os.environ)
    env.pop("PHASMA_CAPS_PROFILE", None)
    env.pop("PHASMA_SPIRV_TARGET", None)
    log_path = player / "PhasmaEngine.log"
    proc = subprocess.Popen([str(player / "PhasmaPlayer.exe"), "--api", "vulkan", "--display", "1"], cwd=player, env=env)
    try:
        deadline = time.time() + seconds
        while time.time() < deadline and proc.poll() is None:
            log = log_path.read_text(errors="replace") if log_path.exists() else ""
            for line in log.splitlines():
                if "[GI bake]" in line:
                    print(line.strip())
                    return "[GI bake] wrote" in line

            time.sleep(1)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(10)

    print(f"FAIL: no probe bake within {seconds} s; is global_illumination on in this project on this GPU?")
    return False


def run_player(player, project, seconds):
    if project:
        settings = {"project_manifest": str(Path(project).resolve() / "phasma_project.json")}
        (player / "phasma_settings.json").write_text(json.dumps(settings), encoding="utf-8")

    env = dict(os.environ, PHASMA_CAPS_PROFILE="webgpu", PHASMA_SPIRV_TARGET="1.2")
    proc = subprocess.Popen([str(player / "PhasmaPlayer.exe"), "--api", "vulkan", "--display", "1"], cwd=player, env=env)
    try:
        deadline = time.time() + seconds
        while time.time() < deadline and proc.poll() is None:
            time.sleep(1)

        if proc.poll() is not None:
            print(f"FAIL: the desktop player exited early (code 0x{proc.returncode & 0xffffffff:08X})")
            return False
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait(10)

    log = (player / "PhasmaEngine.log").read_text(errors="replace")
    if "[WebGPU profile] on" not in log:
        print("FAIL: the desktop player did not run under the WebGPU caps profile")
        return False

    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", default=str(ROOT / "build-p1-desktop" / "Release"))
    parser.add_argument("--project", default="")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--out", default=str(ROOT / "build-wasm-player" / "web-shader-cache"))
    parser.add_argument("--gi", action="store_true", help="bake static GI probes for the project's startup scene")
    parser.add_argument("--gi-seconds", type=int, default=240)
    args = parser.parse_args()
    if args.gi and not args.project:
        sys.exit("--gi needs --project: the bake is saved beside the project's scene")

    build = Path(args.build)
    if not (build / "PhasmaPlayer.exe").exists():
        sys.exit(f"no desktop player at {build}")

    work = Path(tempfile.mkdtemp(prefix="pe-web-bake-"))
    try:
        probes_baked = not args.gi or bake_probes(private_player(build, work, "gi"), args.project, args.gi_seconds)
        player = private_player(build, work)
        if not run_player(player, args.project, args.seconds):
            Path(args.out).mkdir(parents=True, exist_ok=True)
            if (player / "PhasmaEngine.log").exists():
                shutil.copy2(player / "PhasmaEngine.log", Path(args.out) / "bake.log")
                print(f"[bake] the desktop player's log: {Path(args.out) / 'bake.log'}")

            return 1

        cache = player / "ShaderCache"
        convert = subprocess.run([sys.executable, str(ROOT / "tools" / "convert_web_shaders.py"), str(cache)],
                                 capture_output=True, text=True)
        print(convert.stdout.strip())
        if convert.returncode != 0:
            print(convert.stderr.strip())

        out = Path(args.out) / "ShaderCache"
        if out.exists():
            shutil.rmtree(out)

        for folder in ("_spv", "_wgsl"):
            shutil.copytree(cache / folder, out / folder)

        # The log maps each key to its source, for reading the converter's failures.
        shutil.copy2(player / "PhasmaEngine.log", Path(args.out) / "bake.log")

        modules = len(list((out / "_wgsl").glob("*.bindings")))
        print(f"[bake] {modules} modules in {out}")
        return convert.returncode if probes_baked else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
