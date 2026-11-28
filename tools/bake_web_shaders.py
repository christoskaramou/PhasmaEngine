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

With --editor (needs --project), the run also compiles the variants only the web editor can reach: LightingPS for every
shadow map size and cascade count the web profile allows (kWebShadowMapSizes, SceneRenderTargets.h), about 0.5 MB each,
and the grid and AABB overlays (IsWebEditorPass, SceneRenderGraph.cpp). The default editor output is
build-wasm-editor/web-shader-cache; the player default remains build-wasm-player/web-shader-cache.

Usage: python tools/bake_web_shaders.py [--build build-p1-desktop/Release] [--project <dir>] [--seconds 40]
                                        [--out build-wasm-player/web-shader-cache] [--gi] [--gi-seconds 240] [--editor]
Outputs replace the published cache only after every requested step succeeds.
"""
import argparse
from contextlib import contextmanager
from functools import lru_cache
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "Phasma/Runtime/RuntimeAssets/Shaders"
EDITOR_MANIFEST = "web-editor-bake.json"
# PeShaderStageFlags in API/RHITypes.h; the flag itself enters the native cache key.
VERTEX, FRAGMENT, COMPUTE = 1, 16, 32


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def editor_inputs(project):
    roots = [SHADERS, SHADERS.parent / "PassInfo", ROOT / "Phasma/Runtime/Code/Render",
             ROOT / "Phasma/Runtime/Code/RenderPasses"]
    inputs = {p.relative_to(ROOT).as_posix(): digest(p) for folder in roots for p in folder.rglob("*") if p.is_file()}
    for name in ("Phasma/Core/Code/API/Shader.cpp", "Phasma/Core/Code/API/ShaderCache.cpp",
                 "Phasma/Core/Code/API/RHI.cpp", "Phasma/Core/Code/API/Pipeline.cpp", "Phasma/Core/Code/Base/Hash.h",
                 "tools/convert_web_shaders.py", "Phasma/WebGPU/SpvToWgsl/src/main.rs",
                 "Phasma/WebGPU/SpvToWgsl/Cargo.lock"):
        inputs[name] = digest(ROOT / name)
    if project:
        manifest = Path(project) / "phasma_project.json"
        inputs["project_manifest"] = digest(manifest)
        scene = json.loads(manifest.read_text(encoding="utf-8-sig")).get("startup_scene")
        if scene:
            inputs["startup_scene"] = digest(manifest.parent / scene)
        for path in manifest.parent.rglob("*"):
            relative = path.relative_to(manifest.parent)
            if ("Shaders" in relative.parts or path.suffix.lower() in {".hlsl", ".hlsli", ".slang", ".wgsl"}) and path.is_file():
                inputs["project/" + relative.as_posix()] = digest(path)
    return inputs


def required_editor_variants():
    # The web profile's built-in passes, including IsWebSettingsPass and the editor overlays.
    # Paths can alias: AABBsPS and LinesPS have the same expanded code, entry and stage.
    for stage, entry, paths in [
        (VERTEX, "mainVS", "Common/Quad Depth/DepthVS Utilities/AABBsVS Utilities/LinesVS "
                      "Utilities/SpriteOutlineVS Utilities/AdditiveVS Shadows/ShadowsVS Voxel/VoxelShadowVS "
                      "Particle/ParticleVS"),
        (FRAGMENT, "mainPS", "Depth/DepthPS Utilities/GridPS Utilities/LinesPS Utilities/AABBsPS "
                       "Utilities/AdditivePS Outline/OutlineMaskPS Outline/OutlineCompositePS Perception/ObjectIdPS Tonemap/TonemapPS "
                       "SSR/SSRPS Utilities/UpsamplePS FXAA/FXAAPS Bloom/BrightFilterPS Bloom/GaussianBlurHPS "
                       "Bloom/GaussianBlurVPS ColorGrading/ColorGradingPS DepthOfField/DOFPS MotionBlur/MotionBlurPS "
                       "Particle/ParticlePS"),
        (COMPUTE, "mainCS", "Compute/ShadowCullCS Compute/BitonicSortCS Compute/SSAOCS Particle/ParticleCS "
                       "DepthOfField/DOFPrepassCS DepthOfField/DOFTileDilateCS"),
        (COMPUTE, "main", "Sharpen/RCAS TAA/TAA"),
        (COMPUTE, "clearCS", "Perception/ObjectIdClearCS"),
        (COMPUTE, "reduceCS", "Perception/ObjectIdReduceCS"),
    ]:
        for path in paths.split():
            yield path, stage, entry, ()

    yield "Compute/CullingCS", COMPUTE, "mainCS", (("VOXEL_HIZ", "1"),)
    yield "TAA/TAA", COMPUTE, "main", (("HDR_SCENE", "1"),)
    for define in ("SSAO_BLUR_PASS", "SSAO_UPSAMPLE_PASS"):
        yield "Compute/SSAOCS", COMPUTE, "mainCS", ((define, "1"),)

    # PassInfo::Apply and GbufferPass::ApplyGBufferSurface select these material stages.
    for asset in ("PassInfo/standard_pbr.pass", "Shaders/Voxel/voxel_gbuffer.passinfo",
                  "Shaders/Field/hinge_gbuffer.passinfo", "Shaders/Terrain/terrain_gbuffer.passinfo"):
        material = json.loads((SHADERS.parent / asset).read_text(encoding="utf-8-sig"))
        for name in ("surface", "transparent"):
            if name not in material:
                continue
            surface = material[name]
            vertex = Path(surface["vertexShader"]).relative_to("Shaders").with_suffix("").as_posix()
            fragment = Path(surface["fragmentShader"]).relative_to("Shaders").with_suffix("").as_posix()
            yield vertex, VERTEX, "mainVS", ()
            yield fragment, FRAGMENT, "mainPS", ()
            yield fragment, FRAGMENT, "mainPS", (("GBUFFER_NO_VELOCITY", "1"),)
            if name == "transparent" and asset == "PassInfo/standard_pbr.pass":
                yield vertex, VERTEX, "mainVS", (("GBUFFER_TRANSPARENT", "1"),)
    for stage, entry in ((VERTEX, "mainVS"), (FRAGMENT, "mainTintPS")):
        yield "Shadows/ShadowsVS", stage, entry, (("PE_SHADOW_TINT", "1"),)

    # LightPass::LightingDefines, ForwardPlusLightCullingPass and SceneRenderTargets.h.
    render = ROOT / "Phasma/Runtime/Code"
    limits = (render / "Render/SceneRenderTargets.h").read_text()
    sizes = re.search(r"kWebShadowMapSizes\s*\{([\d,\s]+)\}", limits)[1]
    cascades = int(re.search(r"kWebMaxShadowCascades\s*=\s*(\d+);", limits)[1])
    forward = (render / "RenderPasses/ForwardPlusLightCullingPass.h").read_text()
    tiles = (("FORWARD_PLUS_TILE_SIZE", re.search(r"\bTileSize\s*=\s*(\d+);", forward)[1]),
             ("FORWARD_PLUS_MAX_LIGHTS_PER_TILE", re.search(r"\bMaxLightsPerTile\s*=\s*(\d+);", forward)[1]))
    yield "Compute/ForwardPlusLightCullingCS", COMPUTE, "mainCS", tiles
    for cascade in range(1, cascades + 1):
        for size in map(int, sizes.split(",")):
            yield "Gbuffer/LightingPS", FRAGMENT, "mainPS", (
                ("SHADOWMAP_CASCADES", str(cascade)), ("SHADOWMAP_SIZE", f"{size:.6f}"),
                ("SHADOWMAP_TEXEL_SIZE", f"{1 / size:.6f}"), *tiles)


def expanded_shader(path):
    # ShaderCache::ParseShader replaces a quoted include's line but retains its newline.
    source = path.read_bytes()
    lines = source.split(b"\n")
    for index, line in enumerate(lines):
        include = re.fullmatch(rb'\s*#include\s*"([^"]+)"\s*', line.split(b"//", 1)[0])
        if include:
            lines[index] = expanded_shader(path.parent / include[1].decode())
    return b"\n".join(lines)


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def combine(seed, value):
    return (seed ^ (value + 0x9e3779b9 + (seed << 6) + (seed >> 2))) & 0xffffffffffffffff


def variant_key(source_hash, stage, entry, defines):
    # Hash.h, Shader::Create and ShaderCache::Init, using Vulkan at SPIR-V target 1.2.
    options = 0
    for name, value in (("PE_WEB_PROFILE", "1"), *defines):
        options = combine(combine(options, fnv(name.encode())), fnv(value.encode()))
    key = combine(combine(0, source_hash), fnv(entry.encode()))
    key = combine(key, combine(options, stage))
    return str(combine(combine(key, 0), (1 << 22) | (2 << 12)))


def compiled_variant(module):
    # DXC embeds the expanded source and its exact invocation, even for a define optimized out of the code.
    blob = module.read_bytes()
    if len(blob) % 4 or len(blob) < 20 or blob[:4] != b"\x03\x02\x23\x07":
        raise ValueError(f"invalid SPIR-V header in {module}")
    words = memoryview(blob).cast("I")
    i = 5
    source = bytearray()
    while i < len(words):
        size = words[i] >> 16
        if not size or i + size > len(words):
            break
        op = words[i] & 0xffff
        if op == 3 and size > 4:  # OpSource (language, version, file, source string)
            source.extend(bytes(words[i + 4:i + size]).rstrip(b"\0"))
        elif op == 2:  # OpSourceContinued
            source.extend(bytes(words[i + 1:i + size]).rstrip(b"\0"))
        elif op == 330:  # OpModuleProcessed
            text = bytes(words[i + 1:i + size]).rstrip(b"\0")
            if text.startswith(b"dxc-cl-option:"):
                args = text.decode().split()
                pairs = list(zip(args, args[1:]))
                profiles = [v.partition("_")[0] for k, v in pairs if k == "-T"]
                entries = [v for k, v in pairs if k == "-E"]
                defines = tuple(v for k, v in pairs if k == "-D")
                if len(profiles) == 1 and len(entries) == 1:
                    return fnv(source), profiles[0], entries[0], defines
                break
        i += size
    raise ValueError(f"missing DXC variant metadata in {module}")


def editor_modules(groups, cache):
    keys = [key for variants in groups.values() for key in variants]
    if len(keys) != len(set(keys)) or not all(re.fullmatch(r"[0-9]+", key) for key in keys):
        raise ValueError("duplicate or invalid shader cache key")

    @lru_cache(None)
    def source_hash(name):
        return fnv(expanded_shader(SHADERS / name))

    owners = {}
    expected = {}
    for path, stage, entry, defines in required_editor_variants():
        name = path + ".hlsl"
        key = variant_key(source_hash(name), stage, entry, defines)
        owners.setdefault(key, set()).add(name)
        options = ("PE_WEB_PROFILE=1", *(f"{k}={v}" for k, v in defines))
        expected[key] = (source_hash(name), {VERTEX: "vs", FRAGMENT: "ps", COMPUTE: "cs"}[stage], entry, options)
    for key, variant in expected.items():
        if not any(key in groups.get(name, ()) for name in owners[key]):
            raise ValueError(f"incomplete editor bake: missing {sorted(owners[key])} {variant} (key {key})")
        if compiled_variant(cache / "_spv" / key) != variant:
            raise ValueError(f"incorrect compiled source, stage, entry or defines for {sorted(owners[key])} (key {key})")
    return sorted(keys)


def editor_files(cache, keys):
    return {name: digest(cache / name) for key in keys
            for name in (f"_spv/{key}", f"_wgsl/{key}", f"_wgsl/{key}.bindings")}


def verify_editor(cache, project):
    if not (cache / EDITOR_MANIFEST).is_file():
        raise ValueError(f"no verified editor bake at {cache}; run tools/bake_web_shaders.py --editor --project <dir>")
    manifest = json.loads((cache / EDITOR_MANIFEST).read_text())
    host = manifest.get("native_host", {})
    if set(host) != {"PhasmaPlayer.exe", "PhasmaCore.dll"} or not all(
            re.fullmatch(r"[0-9a-f]{64}", value) for value in host.values()):
        raise ValueError("editor bake lacks verified native host provenance; rebake it")
    inputs = editor_inputs(project)
    recorded = manifest["inputs"]
    if not project:
        recorded = {key: value for key, value in recorded.items()
                    if key not in ("project_manifest", "startup_scene") and not key.startswith("project/")}
    if recorded != inputs:
        raise ValueError("stale editor bake: shader, renderer, or project inputs changed")
    keys = editor_modules(manifest["modules"], cache)
    if editor_files(cache, keys) != manifest["files"]:
        raise ValueError("incomplete or modified editor shader cache")


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


def build_editor_host(build):
    tree = build.parent
    config = build.name
    cache = tree / "CMakeCache.txt"
    if not cache.is_file():
        raise ValueError(f"--editor needs a configured native build of this checkout, not {build}")
    config_vars = dict(re.findall(r"^(\w+):\w+=(.*)$", cache.read_text(encoding="utf-8"), re.M))
    source = config_vars.get("CMAKE_HOME_DIRECTORY")
    location = config_vars.get("CMAKE_CACHEFILE_DIR")
    if not source or Path(source).resolve() != ROOT or not location or Path(location).resolve() != tree:
        raise ValueError(f"native build {tree} is not configured for this checkout")
    configs = config_vars.get("CMAKE_CONFIGURATION_TYPES") or config_vars.get("CMAKE_BUILD_TYPE", "")
    config = next((name for name in configs.split(";") if os.path.normcase(name) == os.path.normcase(config)), None)
    if not config:
        raise ValueError(f"{build.name} is not a configured native build configuration at {tree}")

    subprocess.run(["cmake", "--build", str(tree), "--config", config, "--target", "PhasmaPlayer"], check=True)
    hashes = {}
    for name in ("PhasmaCore.dll", "PhasmaPlayer.exe"):
        binary = build / name
        stamp = tree / "CMakeFiles" / f"web-bake-{binary.stem}-{config}.sha256"
        checksum, path = stamp.read_text(encoding="utf-8").strip().split(maxsplit=1)
        if Path(path).resolve() != binary.resolve() or digest(binary) != checksum:
            raise ValueError(f"{binary} does not match this checkout's linked {config} target")
        hashes[name] = checksum
    return hashes


def bake_probes(player, project, seconds):
    """Runs the player with ray tracing until GlobalIlluminationPass logs its bake of the startup scene."""
    settings = {"project_manifest": str(Path(project).resolve() / "phasma_project.json"), "bake_gi": True, "audio_muted": True}
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


def run_player(player, project, seconds, editor=False):
    if project:
        # A copy without Assets/Save, which the exporter leaves out too: the developer's saved game settings (a
        # bloom or shadow toggle) would hide passes whose shaders the browser, starting from defaults, then misses.
        clean = player.parent / "project"
        saves = Path(project).resolve() / "Assets"
        shutil.copytree(project, clean, ignore=lambda folder, names: ["Save"] if Path(folder).resolve() == saves else [])
        settings = {"project_manifest": str(clean / "phasma_project.json"), "audio_muted": True,
                    "bake_web_shaders": True, "bake_web_editor_shaders": editor}
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


@contextmanager
def publication_lock(out):
    # OS locks are released even when a baker is killed; never unlink an active lock inode.
    with (out.parent / f".{out.name}.lock").open("a+b") as lock:
        lock.seek(0)
        if os.name == "nt":
            import msvcrt
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def move_directory(source, target):
    # Windows can briefly deny a directory rename just after another handle closes.
    for attempt in range(6):
        try:
            source.rename(target)
            return
        except PermissionError:
            if os.name != "nt" or attempt == 5:
                raise
            time.sleep(0.05 * 2**attempt)


def recover_publication(out):
    # An abrupt exit between the two Windows directory renames leaves the complete old tree here.
    previous = out.with_name(f".{out.name}.previous")
    if previous.exists():
        if out.exists():
            shutil.rmtree(previous)
        else:
            move_directory(previous, out)
    return previous


def publish(cache, out, keep, manifest, project, log=None):
    out.parent.mkdir(parents=True, exist_ok=True)
    with publication_lock(out), tempfile.TemporaryDirectory(dir=out.parent, prefix=".web-bake-") as work:
        previous = recover_publication(out)
        staged = Path(work) / "ShaderCache"
        if keep and out.exists():
            shutil.copytree(out, staged)
        for folder in ("_spv", "_wgsl"):
            shutil.copytree(cache / folder, staged / folder, dirs_exist_ok=True)
        if manifest:
            (staged / EDITOR_MANIFEST).write_text(json.dumps(manifest), encoding="utf-8")
            verify_editor(staged, project)
        elif (staged / EDITOR_MANIFEST).exists():
            # A player --keep must not invalidate a previously verified editor cache.
            verify_editor(staged, None)
        if log:
            shutil.copy2(log, Path(work) / "bake.log")

        try:
            if out.exists():
                move_directory(out, previous)
            move_directory(staged, out)
        finally:
            if not out.exists() and previous.exists():
                move_directory(previous, out)
        if previous.exists():
            # Publication is already committed. A locked old file can be cleaned on the next run.
            shutil.rmtree(previous, ignore_errors=True)
        if log:
            try:
                (Path(work) / "bake.log").replace(out.parent / "bake.log")
            except OSError as error:
                print(f"[bake] cache published; could not replace diagnostic log: {error}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build", default=str(ROOT / "build-p1-desktop" / "Release"))
    parser.add_argument("--project", default="")
    parser.add_argument("--keep", action="store_true",
                        help="add to the modules already in --out (keys cover source and defines, so old ones are inert)")
    parser.add_argument("--seconds", type=int, default=40)
    parser.add_argument("--out")
    parser.add_argument("--verify-editor", type=Path, metavar="ShaderCache")
    parser.add_argument("--gi", action="store_true", help="bake static GI probes for the project's startup scene")
    parser.add_argument("--gi-seconds", type=int, default=240)
    parser.add_argument("--editor", action="store_true",
                        help="also bake the variants only the web editor can reach (every allowed shadow size and cascade count)")
    args = parser.parse_args()
    if args.verify_editor:
        if args.verify_editor.with_name(f".{args.verify_editor.name}.previous").exists():
            with publication_lock(args.verify_editor):
                recover_publication(args.verify_editor)
        verify_editor(args.verify_editor, args.project)
        return 0
    if not args.out:
        target = "build-wasm-editor" if args.editor else "build-wasm-player"
        args.out = ROOT / target / "web-shader-cache"
    if args.gi and not args.project:
        sys.exit("--gi needs --project: the bake is saved beside the project's scene")
    if args.editor and not args.project:
        sys.exit("--editor needs --project: the bake flags go in the settings written for the project")

    out = Path(args.out).resolve() / "ShaderCache"
    if out.with_name(f".{out.name}.previous").exists():
        with publication_lock(out):
            recover_publication(out)
    build = Path(args.build).resolve()
    inputs = editor_inputs(args.project) if args.editor else None
    host = build_editor_host(build) if args.editor else None
    if not (build / "PhasmaPlayer.exe").exists():
        sys.exit(f"no desktop player at {build}")

    work = Path(tempfile.mkdtemp(prefix="pe-web-bake-"))
    try:
        if args.gi and not bake_probes(private_player(build, work, "gi"), args.project, args.gi_seconds):
            return 1
        player = private_player(build, work)
        if host and any(digest(player / name) != checksum for name, checksum in host.items()):
            raise ValueError("native bake host changed while being copied")
        if not run_player(player, args.project, args.seconds, args.editor):
            Path(args.out).mkdir(parents=True, exist_ok=True)
            if (player / "PhasmaEngine.log").exists():
                shutil.copy2(player / "PhasmaEngine.log", out.parent / "failed-bake.log")
                print(f"[bake] the desktop player's log: {out.parent / 'failed-bake.log'}")

            return 1

        cache = player / "ShaderCache"
        if args.editor:
            for folder in (SHADERS, SHADERS.parent / "PassInfo"):
                for source in folder.rglob("*"):
                    runtime_source = player / "RuntimeAssets" / source.relative_to(SHADERS.parent)
                    if source.is_file() and digest(source) != digest(runtime_source):
                        raise ValueError(f"desktop runtime shader or pass declaration is stale: {source}")
            log = (player / "PhasmaEngine.log").read_text(errors="replace")
            project = json.loads((Path(args.project) / "phasma_project.json").read_text(encoding="utf-8-sig"))
            scene = project.get("startup_scene")
            loaded = re.findall(r"Scene loaded from: (.+)", log)
            if scene and not any(Path(path.strip()).as_posix().endswith("/" + Path(scene).as_posix()) for path in loaded):
                raise ValueError(f"editor bake never loaded the expected scene: {scene}")
            groups = {}
            for source, key in re.findall(r"Compile\(hlsl\) -> source='[^']*/Shaders/([^']+)'.*?cache_hash=(\d+)", log):
                groups.setdefault(source, []).append(key)
            keys = editor_modules(groups, cache)
            if set(keys) != {module.name for module in (cache / "_spv").iterdir() if module.is_file()}:
                raise ValueError("editor shader log does not match the compiled modules")
        if args.editor:
            subprocess.run(["cargo", "build", "--release", "--locked"], cwd=ROOT / "Phasma/WebGPU/SpvToWgsl", check=True)
        convert = subprocess.run([sys.executable, str(ROOT / "tools" / "convert_web_shaders.py"), str(cache)],
                                 capture_output=True, text=True)
        print(convert.stdout.strip())
        if convert.returncode != 0:
            print(convert.stderr.strip())
            return convert.returncode

        manifest = None
        if args.editor:
            if inputs != editor_inputs(work / "project"):
                raise ValueError("renderer, project shader or startup inputs changed during the editor bake")
            manifest = {"native_host": host, "inputs": inputs, "modules": groups, "files": editor_files(cache, keys)}

        publish(cache, out, args.keep, manifest, args.project, player / "PhasmaEngine.log")

        modules = len(list((out / "_wgsl").glob("*.bindings")))
        print(f"[bake] {modules} modules in {out}")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f"[bake] {error}")
