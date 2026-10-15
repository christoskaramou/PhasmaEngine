# OpenPBR via MaterialX — Phases 0–2 Implementation Plan (gates, visibility buffer, material passes)

> **Executor:** Codex `gpt-6.1-sol` at max effort, running alone. Read **Executor protocol** first and follow it on every task. Do not use any other process skill; this plan is the process.

**Goal:**
- **Phase 0:** prove every risky seam of the MaterialX + visibility-buffer design.
- **Phase 1:** ship the visibility buffer with a quad-exact compute debug view that reconstructs the raster G-buffer's albedo and depth.
- **Phase 2:** shade rigid opaque meshes that use the default (untextured) OpenPBR material with MaterialX's own generated code, under PE's lights, shadows and environment. Verify it against MaterialX's own GLSL renderer under one directional light.

**Architecture:**
- **Generation.** PhasmaCook links MaterialX 1.39.5 and turns `.mtlx` into Slang through MaterialX's Slang generator, with three PE overrides:
  - a surface node that runs PE's light loop;
  - an environment file shadowed by search-path order;
  - a bindless `mx_texture.slang`.
- **Material ABI.** An ABI transform turns MaterialX's `cbuffer pixelCB` into per-pixel statics, loaded from PE's material bytes by a generated, line-exact loader.
- **Visibility payload.** With `openpbr` on, the depth prepass also writes an RGBA32_UINT `visibility` target: `drawId + 1` plus the triangle's three global vertex indices, via `GetAttributeAtVertex`.
- **Quad-exact resolve.** Compute passes evaluate each pixel in a 2×2 quad on its own triangle, so derivatives are equivalent to a pixel shader's within a measured tolerance.
- **Two material passes:**
  - Pass 1 writes the thin G-buffer, so GI, SSAO and the rest keep working.
  - Pass 2 overwrites the lit colour of MaterialX pixels with MaterialX's shading.
- **Off switch.** Everything is behind `openpbr` (default false). Off is byte-identical to the pre-change binary.

**Tech stack:**
- C++20, CMake FetchContent, MaterialX 1.39.5 (Apache-2.0).
- Slang (`slangc` through `Phasma/Core/Code/API/SlangCompiler.cpp`), DXC HLSL, Vulkan 1.2+ and DX12.
- Python pool tests over the editor MCP; the MaterialX 1.39.5 Python wheel for the reference renderer.

**Spec:** `docs/superpowers/specs/2026-10-13-openpbr-design.md` (revision 2). Read it before Task 0.

**Provenance.** Phases 0–1 passed three Codex `gpt-6-astra` xhigh review rounds (2026-10-13), then were concretised. These parts were prototyped outside the repository on 2026-10-13:

| Part | Prototype result |
|---|---|
| Generator code (Task 2) | Compiled against MaterialX 1.39.5 and run on `open_pbr_default`, `carpaint`, `glass`, `velvet`, the textured fixture and PE's default document. Every module passes the Task 2 loader checker, compiles with `slangc` to SPIR-V (`spirv-val` clean) and to DXIL `sm_6_6`. |
| `VisibilityDebug.slang`, `MaterialThin.slang`, `MaterialShade.slang` | Compiled through a reproduction of PE's Slang path: quoted-`#include` expansion, the DX12 `[[vk::binding]]` → `register()` rewrite, the same `slangc` arguments. SPIR-V and DXIL both compile; `spirv-val` is clean. |
| `DepthVS` (both variants), `VisibilityPS` | Compiled with DXC to SPIR-V (`SPV_KHR_fragment_shader_barycentric`, `PerVertexKHR`) and to DXIL `vs_6_6`/`ps_6_6`. |

The prototype also caught one error in the reviewed plan: MaterialX library includes are rooted at the folder that *contains* `libraries/`. PE's overrides therefore live under `pe_overrides/libraries/...`, and the search path is `pe_overrides`, then the MaterialX root (Task 2).

---

## Executor protocol (Sol)

1. **One task per run.** Do Task N completely (every step, its tests, `clang-format`, its report), then stop and report. Do not start Task N+1 in the same run. Tasks are numbered in dependency order. Never skip one.
2. **Before each task:**
   - Read that task and the **Global Constraints**.
   - `grep` the symbols the task names. Line numbers in this plan are "today's" hints (2026-10-13); the **anchor text** (function name, unique line) is authoritative. If an anchor is not found exactly, stop and report.
3. **Scope.** Edit only files the task lists, or a file the task's text names explicitly. Never edit `third_party/`, `Phasma/Core/third_party/`, MaterialX sources, or `.fetchcontent-*`.
4. **No git writes.** Never run `git commit`, `git checkout`, `git reset`, `git restore`, `git clean`, `git stash` or `git add`. Leave every change unstaged on the current branch.
5. **Build** with the command in Global Constraints. A failed build is not a stop condition: fix it within the task's files. If the fix needs a file the task does not list, stop and report.
6. **Format.** Run `clang-format -i` on every `.cpp`/`.h` you touched. Never add `#include <std...>` to a `.cpp`/`.h`: the PCH provides the standard library.
7. **Logs.** After every editor or player launch, read `build-ninja-physics/Release/PhasmaEngine.log`. Explain every `[WARN]` and `[ERROR]` line in the report. A new warning caused by your change is a failure to fix.
   **Known pre-existing lines** (verified 2026-10-13 in Task 1; not caused by this plan): report them, do not stop on them.
   - `DX12 debug layer warning [category=9 id=820]: ID3D12CommandList::ClearRenderTargetView: The clear values do not match…`. Logged right after each `decode_camera_view` MCP call, from the editor's object-ID perception pass (`Phasma/Runtime/Code/Render/ScenePerception.cpp`).
   - `cooked_clusters.py`'s deliberate truncated-mesh rejection: `[ModelAssetCooked] Invalid cluster hierarchy: …truncated.pemesh` and the following `[MCP] [http-tool] <- load_cooked_mesh: error`.
   - Anything else is new.
8. **Test processes:**
   - Run pool tests from `.testpool/tests/`.
   - GPU test processes are force-killed when a test ends (the harness does it; verify with `tasklist | findstr Phasma`).
   - Keep windows on display 1.
   - If another process launched an editor you did not launch, never kill it: match PIDs first.
9. **Stop and report** immediately, without working around it, when:
   - (a) a step says **STOP**;
   - (b) a gate in Task 2 Step 5, Task 6 Step 5/5b or Task 12 fails after a faithful implementation;
   - (c) a Vulkan validation or DX12 debug-layer message appears that you cannot attribute to your change and fix;
   - (d) the off-identity check (Task 4, Task 7, Task 13) shows any difference;
   - (e) a step needs a download, a `pip install`, or network access that the user has not approved **in this plan's approval record** (below);
   - (f) the code you would write departs from this plan or the spec in behaviour, not just in spelling or line position.
10. **Report format** at the end of each task:
    - `Task N: DONE | STOPPED (reason)`;
    - the files changed, with line counts;
    - each command you ran, with its result (pass/fail and the key output lines);
    - log findings;
    - deviations, each with its reason;
    - a last line `Evidence: …` (what you observed) or `Unverified: …` (what remains unchecked).

**Approval record** (filled in by the user before the run; Sol never assumes it):
- [x] MaterialX v1.39.5 FetchContent download from GitHub (Task 1). Approved by the user in chat, 2026-10-13; not needed: the source is local (Task 1 Step 0).
- [x] `pip install MaterialX==1.39.5` into `.testpool/venv-mtlx` with Python 3.12 (Task 12). Approved by the user in chat, 2026-10-13.

## Global Constraints

- **Default off.** `openpbr` defaults to false. Off, rendered output is byte-identical to the pre-change binary (Task 0 baseline): no `visibility` target and no new pass enabled.
- **Caps gate.** `SceneUsesOpenPBR()` is true only for: setting AND not `PE_ANDROID` AND fragment barycentrics AND compute derivatives (quads) AND `maxColorAttachments >= 7`.
  - Otherwise it issues one `PE_WARN` and returns false.
  - `geometryShader` is not required: the payload uses no `SV_PrimitiveID`, and `PerVertexKHR` needs only `FragmentBarycentricKHR`.
- **MaterialX pin.** **v1.39.5** (tag commit `7b64921e`), generator-only build, linked into PhasmaCook only.
- **Overrides.**
  - PE overrides live in `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/…`, mirroring MaterialX's `libraries/` tree.
  - The generator registers the search paths `<exe>/MaterialX/pe_overrides`, then `<exe>/MaterialX` (which contains `libraries/`), then the `.mtlx` folder.
  - MaterialX includes are root-relative (`libraries/stdlib/...`); first match wins.
- **Slang sources:**
  - `.slang` sources compile with `slangc` under every compiler setting (Task 6 Step 0 routing).
  - Write every resource declaration on **one line**: `[[vk::image_format("…")]][[vk::binding(N, S)]] Type name;` (`Dx12ShaderImpl.cpp:531-586` rewrites per line).
  - Only these declaration forms are reflected on DX12 (`Dx12ShaderImpl.cpp:850-851`): `(RW|Append|Consume)?StructuredBuffer<T>`, `(RW)?Texture*`/`ByteAddressBuffer`/`Buffer`, `SamplerState`, `SamplerComparisonState`, `ConstantBuffer`, `cbuffer`.
  - Use only quoted `#include`. Never use Slang `import`: `ShaderCache` does not expand it, hash it, or rewrite its bindings.
  - At most **4 descriptor sets (0–3)**: `DX12_DESCRIPTOR_SPACE_COUNT = 4` (`Dx12RootSignature.h:10`).
  - An unbounded array must be the highest-numbered binding of its class in its set.
- **Compute derivatives** (verified 2026-10-13 with slangc 2026.8 and 2026.19):
  - SPIR-V: `[DerivativeGroupQuad]` emits `OpCapability ComputeDerivativeGroupQuadsKHR` plus `SPV_KHR_compute_shader_derivatives`, and keeps `OpImageSampleImplicitLod`. slangc prints warning E41012 ("profile implicitly upgraded"). That warning is expected; `SlangCompiler.cpp` fails only on a non-zero exit.
  - DX12: `sm_6_6` defines compute derivatives on 2×2 quads.
  - `numthreads` x and y must be even.
- **HLSL:** stays SM 6.3 on Vulkan DXC and `*_6_6` on DX12, warnings as errors, 32-bit types.
- **Commits:** never commit; leave changes unstaged. `clang-format -i` every touched `.cpp`/`.h`. No std headers in `.cpp`/`.h`.
- **Build:** write `%TEMP%\pe_build_openpbr.bat` with these two lines:
  ```bat
  call C:\Users\Christos\repos\PhasmaEngine\.testpool\tests\vsenv.cmd
  cmake --build C:\Users\Christos\repos\PhasmaEngine\build-ninja-physics --config Release --target PhasmaEditor PhasmaEditorModule PhasmaCook
  ```
  Run it with `cmd //c "%TEMP%\pe_build_openpbr.bat"`. From Task 8 on, add the target `PeOpenPBRDefaultModule` to that line.
- **Logs:** after every editor run, read `build-ninja-physics/Release/PhasmaEngine.log` for `[WARN]`/`[ERROR]` and explain each.
- **Pool tests:**
  - They live in `.testpool/tests/` (gitignored). Run them on `--api vulkan` and `--api dx12`, with `PE_DX12_DEBUG=1` (the harness sets it).
  - Register them in `.testpool/catalog.json` with 1-space indent.
  - Test-only shaders and stubs live there too, never in the repo.
- **Protected lines:** do not edit the lines other pool tests byte-match. The list is in the spec's §Carried over. The source of truth is the grep set in `.testpool/tests/{ssr_channels,gi_diffuse_reference,gi_screen_probes,gi_cascades,gi_reflections}.py`.
- **Android:** none of this runs there. Still rebake the shader cache once (Task 7), because shared HLSL headers change.

## Review Focus

1. **A legacy mesh in front of a visibility mesh.** The nearer surface must win the visibility target (write 0). Pinned in Task 6 by the depth-match check on Sponza: every pixel with x ≠ 0 must have depth error ≤ 1e-5.
2. **Terrain or voxels drawn after the prepass.** Their pipelines write `visibility = 0` (Task 5). Pinned in Task 6's voxel-occluder check.
3. **LOD, clusters and two-phase Hi-Z all on.** The triangle must still be recovered (the payload carries global vertex indices). Pinned in Task 6 with `lod_enabled`, `cluster_geometry` and `occlusion_culling` on and off.
4. **DX12 `SV_VertexID` excludes the base vertex.** Pinned in Task 6's depth-match check on both APIs.
5. **Off-identity after toggling on and off.** Pinned in Task 4 (`check_toggle_identity`).
6. **`openpbr` on with no MaterialX material** (every existing scene): no new work should be visible. Pinned in Task 13 (`check_on_without_materialx_identity`): Sponza with `openpbr` on equals `openpbr` off byte for byte, with GI and TAA off.
7. **A mesh without tangents** (every PE primitive stores zero tangents). MaterialX's tangent frame would turn into NaN. `PeSurfaceAt` derives the UV tangent as MaterialX's own mesh loader does. Pinned in Task 12: the plane primitive's anisotropy case must match.

---

## File Map

| File | Responsibility | Task |
|---|---|---|
| `CMakeLists.txt` (root) | MaterialX FetchContent; editor and player depend on the generated default module | 1, 8 |
| `Phasma/Cook/CMakeLists.txt` | Link MaterialX; copy `libraries/` and `pe_overrides/`; generate the default module at build time | 1, 2, 8 |
| `Phasma/Cook/Code/MaterialX/MaterialXGen.{h,cpp}` (new) | `.mtlx` → PE Slang module + `.json` layout | 2 |
| `Phasma/Cook/Code/MaterialX/PeSurfaceNodeSlang.{h,cpp}` (new) | PE light loop (`HwSurfaceNode::emitLightLoop`) | 2 |
| `Phasma/Cook/main.cpp` | `--mtlx <in.mtlx> <outDir>` | 2 |
| `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/pbrlib/genglsl/lib/mx_environment_prefilter.glsl` (new) | PE environment hooks | 2 |
| `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/stdlib/genslang/lib/mx_texture.slang` (new) | Bindless `SamplerTexture2D` | 2 |
| `Phasma/Runtime/RuntimeAssets/MaterialX/pe_default_openpbr.mtlx` (new) | PE's default OpenPBR material document | 8 |
| `Phasma/Core/Code/API/RHI.{h,cpp}` | Caps: query/enable fragment barycentrics and compute derivatives; log | 3 |
| `Phasma/Core/Code/API/Shader.{h,cpp}`, `ShaderCache.cpp`, `Vulkan/VulkanShaderImpl.cpp`, `DX12/Dx12ShaderImpl.cpp` | Per-source Slang routing | 6 |
| `Phasma/Core/Code/Base/Settings.h`, `Phasma/Runtime/Code/Script/Bindings/*/SettingsBindings.cpp`, `SceneSerializer.cpp`, `PipelineControls.cpp`, `GlobalIlluminationPass.cpp` | `openpbr`, `openpbr_debug` settings | 3 |
| `Phasma/Runtime/Code/Render/SceneRenderTargets.{h,cpp}`, `SceneRendererCore.{h,cpp}` | `SceneUsesOpenPBR[Debug]`, `visibility`/`visibility_debug` targets, rebuild flag | 3, 4 |
| `Phasma/Runtime/RuntimeAssets/Shaders/Common/Structures.hlsl` | `VISIBILITY` members; `GBUFFER_VISIBILITY` output; `PE_OPENPBR_SHADE` push constant | 5, 11 |
| `Shaders/Depth/DepthVS.hlsl`, `Shaders/Depth/VisibilityPS.hlsl` (new) | Visibility payload | 5 |
| `Phasma/Runtime/Code/RenderPasses/DepthPass.{h,cpp}`, `DepthLatePass.{h,cpp}`, `GbufferPass.{h,cpp}` | Attach and write `visibility` | 5 |
| `Shaders/Voxel/VoxelGBufferPS.hlsl`, `Shaders/Terrain/TerrainGBufferPS.hlsl` | Clear the payload where they win depth | 5 |
| `Shaders/MaterialX/VisibilityCommon.slang` (new) | Triangle reconstruction, `PeSurfaceAt` | 6 |
| `Shaders/MaterialX/VisibilityDebug.slang` (new), `RenderPasses/VisibilityDebugPass.{h,cpp}` (new) | Debug view | 6 |
| `Phasma/Runtime/Code/Render/SceneRenderGraph.{h,cpp}` | Register VisibilityDebug (305), MaterialThin (307), MaterialShade (510) | 6, 10, 11 |
| `Phasma/Runtime/Code/Scene/OpenPBRMaterial.{h,cpp}` (new) | Default layout (`.json`), byte packing, legacy mirror | 9 |
| `Phasma/Runtime/Code/Scene/Material.{h,cpp}`, `SceneBuffers.cpp` | `openpbr` flag, byte path, `editorFlags` bit 32 | 9 |
| `Phasma/Runtime/Code/Script/Bindings/Material/MaterialBindings.cpp` | `material.use_openpbr` / `set_openpbr` / `get_openpbr` | 9 |
| `Shaders/MaterialX/MaterialThin.slang` (new), `RenderPasses/MaterialThinPass.{h,cpp}` (new) | Material pass 1 | 10 |
| `Shaders/MaterialX/PePrelude.slang`, `MaterialShade.slang` (new), `RenderPasses/LightPass.{h,cpp}` | Material pass 2 | 11 |
| `.testpool/tests/openpbr_baseline.py`, `mtlx_codegen.py`, `mtlx_gate.slang`, `openpbr_vis.py`, `mtlx_reference.py` (new, gitignored) | Pool tests | 0, 2–13 |
| `docs/wiki/architecture/rendering.md`, the spec | Knowledge | 7, 13 |

---

## Phase 0 — gates

### Task 0: Baselines before any change

**Files:** Create `.testpool/tests/openpbr_baseline.py`. Output goes to `.testpool/baselines/openpbr/{vulkan,dx12}-{sponza,mix}.png` and `perf/openpbr/baseline/`.

**Precondition:** the binary is pre-change. `git status --short -- Phasma CMakeLists.txt cmake` must not list any file from the File Map (documentation under `docs/` and tests under `.testpool/` do not count). If it does, **STOP**.

- [ ] **Step 1: Capture script.** It reuses the linear-colour capture, which renders Sponza and the mix scene with every setting at its default.
```python
"""OpenPBR off-identity baseline: Sponza + the mix scene rendered by the pre-change binary (openpbr false), captured at
full viewport resolution (linear_color_baseline's 640-px capture is resized and cannot prove byte identity).
Also the shared log check: every editor launch rewrites PhasmaEngine.log, so each session is checked before the next."""
import argparse, os, tempfile, time
from pathlib import Path
from PIL import Image
from pe_editor import ROOT, editor_scene, lua, tool
from pe_player import gpu_problems
from linear_color_baseline import MIX, SPONZA, STATIC

OUT = ROOT / ".testpool/baselines/openpbr"
LOG = ROOT / "build-ninja-physics/Release/PhasmaEngine.log"


def log_problems():
    lines = LOG.read_text(errors="replace").splitlines()
    return sorted(set(l for l in lines if "[WARN]" in l or "[ERROR]" in l)) + list(gpu_problems(lines) or [])


def check_log():
    bad = log_problems()
    assert not bad, "\n".join(bad[:20])


def full_shot():
    r = tool("take_scene_screenshot", {"target": "viewport", "max_width": 16384})
    assert not r.get("error"), r
    return Image.open(r["path"]).convert("RGB")


def load_mix():
    lua(MIX); lua(STATIC + ' return "ok"')
    tool("set_camera", {"position": [0, 3, 6], "target": [0, 0.4, 0], "projection": "perspective"}); time.sleep(4)


def capture(api, after_session=check_log):
    """after_session runs once after each editor session closes, before the next launch rewrites the log."""
    shots = {}
    assert (SPONZA / "Scenes/sponza.pescene").exists(), f"required Sponza fixture missing: {SPONZA}"
    with editor_scene(api, SPONZA):
        lua('scene.load("sponza.pescene") return "ok"'); time.sleep(18)
        lua(STATIC + ' return "ok"'); time.sleep(3)
        shots["sponza"] = full_shot()
    after_session()
    with tempfile.TemporaryDirectory(prefix="pe-openpbr-base-") as d:
        assets = Path(d) / "Assets"; (assets / "Agent").mkdir(parents=True)
        (assets / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
        with editor_scene(api, assets):
            load_mix()
            shots["mix"] = full_shot()
        after_session()
    return shots


if __name__ == "__main__":
    p = argparse.ArgumentParser(); p.add_argument("--api", default="vulkan"); p.add_argument("--out", default=str(OUT))
    a = p.parse_args(); out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    found = []
    def record():  # the pre-change binary's own warnings are recorded, not asserted
        found.extend(log_problems())
    for name, im in capture(a.api, after_session=record).items():
        im.save(out / f"{a.api}-{name}.png"); print("saved", out / f"{a.api}-{name}.png", im.size)
    (out / f"{a.api}-log.txt").write_text("\n".join(found), encoding="utf-8")
    print("pre-change log problems:", len(found))
```
If `take_scene_screenshot` caps `max_width` below the viewport width, read its schema (`EditorToolCatalog.cpp`, tool `take_scene_screenshot`) and use its full-resolution option. The capture must not be resampled.

- [ ] **Step 2: Capture twice per API and check determinism.**
  1. For each API (`vulkan`, then `dx12`): run `cd .testpool/tests && python openpbr_baseline.py --api <api>`, then `python openpbr_baseline.py --api <api> --out %TEMP%\pe-openpbr-recapture`.
  2. Compare each pair with numpy (four images per API pair: sponza and mix).

  Expected: identical, maximum difference 0, for both APIs. If not, **STOP**: the baseline is not deterministic, and every later off-identity check would be meaningless.
  Report the contents of `.testpool/baselines/openpbr/<api>-log.txt`: warnings the pre-change binary already logs. The later log checks assert a clean log, so pre-existing warnings must be explained in the report before Task 1.
- [ ] **Step 3: Perf baseline**, per the `CLAUDE.md` perf rules:
  1. Run `tasklist | findstr /I PhasmaEditor.exe`. `tools/perf_cycle.sh` force-kills every PhasmaEditor. If any editor is running that this run did not start, **STOP** and ask.
  2. Run `tools/perf_cycle.sh Release C:/Users/Christos/repos/PhasmaEngine/build-ninja-physics`. The absolute build directory is mandatory: the script defaults to `build`, and it changes directory before resolving a relative one (`tools/perf_cycle.sh:40-43`).
  3. Run `scene.load("Scenes/sponza.pescene")`.
  4. Set present mode to immediate and confirm fps is not capped by vsync.
  5. Take a screenshot.
  6. Take 10 `profiler_snapshot`s, 1 s apart, into `perf/openpbr/baseline/`.
  7. Force-kill the editor this run started, matching its PID, then read `build-ninja-physics/Release/PhasmaEngine.log`.

### Task 1: MaterialX dependency

**Files:**
- Modify the root `CMakeLists.txt`: add a block after the Box2D block (anchor: the line that ends the Box2D `FetchContent_MakeAvailable` block, about line 220), following the Jolt/Box2D pattern.
- Modify `Phasma/Cook/CMakeLists.txt`.
- Create the empty stubs `Phasma/Cook/Code/MaterialX/{MaterialXGen,PeSurfaceNodeSlang}.{h,cpp}`; Task 2 fills them.

- [ ] **Step 0: Local MaterialX source, no download.** The MaterialX source is already at `.fetchcontent-ninja/materialx-src`, a clean clone at tag `v1.39.5`, commit `7b64921ef1d42f2d57871e9d2c43dc11f041f26b` (gitignored).
  1. Verify it: `git -C .fetchcontent-ninja/materialx-src log -1 --format=%H` must print that commit, and `git -C .fetchcontent-ninja/materialx-src status --short` must print nothing. Otherwise **STOP**.
  2. Do not download anything: no network access is needed in this task.
  3. FetchContent must use the local copy. Before the first build of Step 3, run this inside the vsenv environment (one line in a `.bat` that first calls `.testpool\tests\vsenv.cmd`):
     ```bat
     cmake -DFETCHCONTENT_SOURCE_DIR_MATERIALX=C:/Users/Christos/repos/PhasmaEngine/.fetchcontent-ninja/materialx-src C:\Users\Christos\repos\PhasmaEngine\build-ninja-physics
     ```
     This only sets a cache variable. `FetchContent_Declare` keeps its GitHub URL for other machines, but in this build tree it populates from the local source.
  4. If the configure output shows a `git clone` or any network access for MaterialX, **STOP**.
- [ ] **Step 1: Root CMake.**
```cmake
# --- MaterialX (OpenPBR shader generation; PhasmaCook only) ---
option(PE_MATERIALX "Build MaterialX shader generation into PhasmaCook" ON)
if(PE_MATERIALX AND NOT ANDROID)
    include(FetchContent)
    foreach(mx_off GEN_GLSL GEN_OSL GEN_MDL GEN_MSL RENDER TESTS VIEWER GRAPH_EDITOR PYTHON DOCS OIIO OCIO)
        set(MATERIALX_BUILD_${mx_off} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(MATERIALX_BUILD_GEN_SLANG ON CACHE BOOL "" FORCE)
    set(MATERIALX_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(MaterialX
        GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/MaterialX.git
        GIT_TAG        v1.39.5
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(MaterialX)
    set(PE_MATERIALX_LIBRARIES_DIR "${materialx_SOURCE_DIR}/libraries" CACHE INTERNAL "")
endif()
```
- [ ] **Step 2: Link and copy data.** In `Phasma/Cook/CMakeLists.txt`, after the `target_link_libraries(PhasmaCook PRIVATE …)` block:
```cmake
if(TARGET MaterialXGenSlang)
    target_link_libraries(PhasmaCook PRIVATE MaterialXGenSlang MaterialXFormat)
    target_compile_definitions(PhasmaCook PRIVATE PE_MATERIALX)
    target_sources(PhasmaCook PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/MaterialXGen.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/PeSurfaceNodeSlang.cpp")
    # Generation reads MaterialX's library data (<exe>/MaterialX/libraries) and PE's overrides at cook time.
    add_custom_command(TARGET PhasmaCook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PE_MATERIALX_LIBRARIES_DIR}" "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/libraries")
endif()
```
  - The stub `.cpp` files contain only `#include "MaterialXGen.h"` and `#include "PeSurfaceNodeSlang.h"` respectively.
  - The stub headers contain only `#pragma once`.
  - MaterialX sets C++17 inside its own scope; PhasmaCook stays C++20.
- [ ] **Step 3: Configure and build** with the build command. Expected:
  - the MaterialX targets build (about 18 s measured);
  - `build-ninja-physics/Release/MaterialX/libraries/bxdf/open_pbr_surface.mtlx` exists;
  - PhasmaEditor and PhasmaCook still build;
  - the existing pool tests `cooked-clusters-*` pass.

  If MaterialX headers clash with the PCH (macro or name conflicts in `MaterialXGen.cpp`), add this in the same `if(TARGET MaterialXGenSlang)` block and record it in the report (the ImGui TUs use the same escape):
```cmake
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/MaterialXGen.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Code/MaterialX/PeSurfaceNodeSlang.cpp" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
```
  The files then include `<filesystem>`, `<fstream>`, `<map>`, `<regex>`, `<sstream>`, `<string>`, `<vector>` and `"Base/Path.h"` themselves. That is the only allowed exception to the no-std-headers rule.

### Task 2: PE MaterialX generator and overrides

**Files:**
- Fill `Phasma/Cook/Code/MaterialX/{MaterialXGen,PeSurfaceNodeSlang}.{h,cpp}`.
- Create `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/pbrlib/genglsl/lib/mx_environment_prefilter.glsl` and `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/stdlib/genslang/lib/mx_texture.slang`.
- Modify `Phasma/Cook/main.cpp` (anchor: `int main(int argc, char *argv[])`).
- Modify `Phasma/Cook/CMakeLists.txt` (Step 4b).
- Test: `.testpool/tests/mtlx_codegen.py` and `.testpool/tests/mtlx_gate.slang`.

**Interfaces (produced):**
- `bool pe::GenerateMaterialXSlang(const std::filesystem::path &mtlx, const std::filesystem::path &outDir, std::string &error)`. For each renderable material named `<M>` it writes:
  - `<outDir>/<M>.slang`: the PE module;
  - `<outDir>/<M>.json`: `{"material", "inputs":[{"name","input","node","type","offset","default":[…]}], "vertexData":[…], "byteSize":N, "thin":bool}`. `name` is the generated static; `input` is the MaterialX input name on its node; `default` holds numbers (booleans as 1/0, textures as `[]`).
- The module defines:
  - `void pe_load_material(ByteAddressBuffer b, uint o)`;
  - `void pe_fill_vertex_data(PeSurfacePoint sp)`;
  - `float4 pe_material_shade()`: the surface colour under PE lighting;
  - when `"thin": true`, also `struct PeThin { float3 normal; float3 albedo; float roughness; float metalness; }` and `PeThin pe_material_thin(PeSurfacePoint sp)`. These are emitted only when `base_color`, `base_weight`, `specular_roughness` and `base_metalness` are unconnected and `geometry_normal` is unconnected. Phase 3 lifts that restriction.
- **PE prelude contract.** The including shader defines these before `#include`-ing the module:
  - `struct PeSurfacePoint { float4 svPosition; float3 P; float3 N; float3 T; float3 B; float2 uv0; float4 color0; }` (`svPosition` = pixel centre xy, NDC depth, 1/w);
  - `#define PE_REFRACTION_TWO_SIDED false`;
  - `uint pe_light_count()`;
  - `void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)`;
  - `float3 pe_env_radiance(float3 N, float3 V, float3 L, float2 alpha)`;
  - `float3 pe_env_irradiance(float3 N)`;
  - `Texture2D pe_textures[]`, `SamplerState pe_samplers[]`;
  - `float3 pe_view_position()`.

- [ ] **Step 1: Failing test.** Create `.testpool/tests/mtlx_codegen.py`:
```python
"""MaterialX -> PE Slang codegen: generate MaterialX's own OpenPBR examples with PE's overrides and compile them through
slangc for SPIR-V (spirv-val) and DXIL cs_6_6 inside a quad-derivative compute harness (mtlx_gate.slang)."""
import json, os, subprocess, sys, tempfile
from pathlib import Path
from PIL import Image
from pe_editor import ROOT

COOK = ROOT / "build-ninja-physics/Release/PhasmaCook.exe"
SLANGC = Path(os.environ.get("PE_SLANGC", r"C:\Users\Christos\repos\slang-bin\bin\slangc.exe"))
SDK = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.350.0"))
EXAMPLES = ROOT / ".fetchcontent-ninja/materialx-src/resources/Materials/Examples/OpenPbr"
CASES = ["open_pbr_default.mtlx", "open_pbr_carpaint.mtlx", "open_pbr_glass.mtlx", "open_pbr_velvet.mtlx"]
# The examples have no image node: a textured OpenPBR fixture is mandatory (int/bool/texture uniforms, UV geomprop).
TEXTURED = """<?xml version="1.0"?>
<materialx version="1.39">
  <image name="tex_base" type="color3"><input name="file" type="filename" value="checker.png" colorspace="srgb_texture"/></image>
  <image name="tex_rough" type="float"><input name="file" type="filename" value="checker.png"/></image>
  <open_pbr_surface name="pe_textured_surface" type="surfaceshader">
    <input name="base_color" type="color3" nodename="tex_base"/>
    <input name="specular_roughness" type="float" nodename="tex_rough"/>
    <input name="geometry_thin_walled" type="boolean" value="true"/>
  </open_pbr_surface>
  <surfacematerial name="PeTextured" type="material"><input name="surfaceshader" type="surfaceshader" nodename="pe_textured_surface"/></surfacematerial>
</materialx>
"""
WIDTH = {"float": "", "float2": "2", "float3": "3", "float4": "4"}


def expected_loader_lines(meta):
    """The exact assignment lines the generator must emit (Task 2 Step 4 'Loader' format), one per line."""
    out = []
    for i in meta["inputs"]:
        n, ty, off = i["name"], i["type"], i["offset"]
        if ty in WIDTH:
            out.append(f"{n} = asfloat(b.Load{WIDTH[ty]}(o + {off}));")
        elif ty == "int":
            out.append(f"{n} = asint(b.Load(o + {off}));")
        elif ty == "uint":
            out.append(f"{n} = b.Load(o + {off});")
        elif ty == "bool":
            out.append(f"{n} = b.Load(o + {off}) != 0u;")
        elif ty == "float3x3":
            out += [f"{n}[{r}] = asfloat(b.Load3(o + {off + 12 * r}));" for r in range(3)]
        elif ty == "float4x4":
            out += [f"{n}[{r}] = asfloat(b.Load4(o + {off + 16 * r}));" for r in range(4)]
        elif ty == "SamplerTexture2D":
            out += [f"{n}.textureIndex = b.Load(o + {off});", f"{n}.samplerIndex = b.Load(o + {off + 4});"]
        else:
            raise AssertionError(f"unsupported input type {ty} for {n}")
    return out


def loader_body(text):
    start = text.index("void pe_load_material(ByteAddressBuffer b, uint o)")
    body = text[text.index("{", start) + 1:]
    depth, end = 1, 0
    for k, ch in enumerate(body):
        depth += (ch == "{") - (ch == "}")
        if depth == 0:
            end = k; break
    return [l.strip() for l in body[:end].splitlines() if l.strip()]


def check_loader(text, meta, label):
    got, want = sorted(loader_body(text)), sorted(expected_loader_lines(meta))
    assert got == want, f"{label}: loader mismatch\n missing {sorted(set(want) - set(got))}\n extra {sorted(set(got) - set(want))}\n dupes {len(got) - len(set(got))}"


def must_fail(fn, what):
    try:
        fn()
    except AssertionError:
        return
    raise AssertionError(f"loader checker accepted a malformed loader: {what}")


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    assert r.returncode == 0, " ".join(map(str, cmd)) + "\n" + r.stdout[-3000:] + r.stderr[-3000:]
    return r


def main():
    missing = [c for c in CASES if not (EXAMPLES / c).exists()]
    assert not missing, f"MaterialX examples missing (did FetchContent use another dir?): {missing}"
    env = dict(os.environ, PATH=str(SDK / "Bin") + os.pathsep + os.environ["PATH"])
    with tempfile.TemporaryDirectory(prefix="pe-mtlx-") as d:
        out = Path(d)
        (out / "textured.mtlx").write_text(TEXTURED, encoding="utf-8")
        Image.new("RGBA", (2, 2), (200, 120, 60, 255)).save(out / "checker.png")
        for case in [EXAMPLES / c for c in CASES] + [out / "textured.mtlx"]:
            run([COOK, "--mtlx", case, out])
        modules = sorted(p for p in out.glob("*.slang") if not p.name.startswith("gate_"))
        assert len(modules) >= len(CASES) + 1, f"expected a module per material, got {[m.name for m in modules]}"
        for m in modules:
            text = m.read_text(encoding="utf-8")
            assert "pe_light_sample(" in text, f"{m.name}: PE light loop missing"
            for gone in ("u_lightData", "u_numActiveLightSources", "LightData", "u_envRadiance", "u_envIrradiance",
                         "u_viewPosition", "cbuffer pixelCB", "[shader(\"fragment\")]"):
                assert gone not in text, f"{m.name}: {gone} survived the ABI transform"
            assert "pe_env_radiance(" in text, f"{m.name}: environment override not picked up"
            meta = json.loads(m.with_suffix(".json").read_text(encoding="utf-8"))
            assert meta["byteSize"] > 0 and meta["inputs"], f"{m.name}: empty material layout"
            # Structural loader check: exactly the expected assignment lines, no duplicates, no extras.
            # Values are proven at runtime by Phase 2's non-default-input match test (accepted in review).
            check_loader(text, meta, m.name)
            first = expected_loader_lines(meta)[0]
            must_fail(lambda: check_loader(text.replace(first, first + "\n    " + first, 1), meta, "dup"), "duplicate assignment")
            off0 = str(meta["inputs"][0]["offset"])
            must_fail(lambda: check_loader(text.replace(first, first.replace("o + " + off0, "o + " + str(int(off0) + 36)), 1), meta, "off"), "wrong offset")
            tex = [i for i in meta["inputs"] if i["type"] == "SamplerTexture2D"]
            if tex:
                line = f"{tex[0]['name']}.samplerIndex = b.Load(o + {tex[0]['offset'] + 4});"
                must_fail(lambda: check_loader(text.replace(line, "", 1), meta, "tex"), "texture without samplerIndex")
            f3 = [i for i in meta["inputs"] if i["type"] == "float3"]
            if f3:
                line = f"{f3[0]['name']} = asfloat(b.Load3(o + {f3[0]['offset']}));"
                must_fail(lambda: check_loader(text.replace(line, line.replace("Load3", "Load"), 1), meta, "width"), "float3 loaded with Load")
            textured = m.stem.startswith("PeTextured")
            if textured:
                assert any(i["type"] == "SamplerTexture2D" for i in meta["inputs"]), "textured fixture has no texture input"
                assert any(i["type"] == "bool" for i in meta["inputs"]) and any(i["type"] == "int" for i in meta["inputs"]), "textured fixture lacks int/bool inputs"
            assert meta["thin"] == (not textured), f"{m.name}: thin={meta['thin']} (connected base_color must disable it)"
            assert ("pe_material_thin(" in text) == meta["thin"], f"{m.name}: pe_material_thin presence disagrees with the layout"
            gate = out / f"gate_{m.stem}.slang"
            gate.write_text((ROOT / ".testpool/tests/mtlx_gate.slang").read_text().replace("MODULE_PATH", m.name), encoding="utf-8")
            run([SLANGC, gate, "-target", "spirv", "-profile", "spirv_1_6", "-entry", "gateCS", "-stage", "compute", "-o", out / f"{m.stem}.spv"], cwd=out, env=env)
            run([SDK / "Bin/spirv-val.exe", out / f"{m.stem}.spv"])
            run([SLANGC, gate, "-target", "dxil", "-profile", "sm_6_6", "-entry", "gateCS", "-stage", "compute", "-o", out / f"{m.stem}.dxil"], cwd=out, env=env)
    print("mtlx codegen ok")


if __name__ == "__main__":
    main()
```
  Create `.testpool/tests/mtlx_gate.slang`, the test-only stub prelude plus a quad compute entry:
```slang
// Test-only: PE prelude stubs + a quad-derivative compute entry that calls one generated material.
[[vk::binding(0, 0)]] RWTexture2D<float4> gateOut;
[[vk::binding(1, 0)]] ByteAddressBuffer gateBytes;
[[vk::binding(2, 0)]] Texture2D pe_textures[];
[[vk::binding(3, 0)]] SamplerState pe_samplers[];
struct PeSurfacePoint { float4 svPosition; float3 P; float3 N; float3 T; float3 B; float2 uv0; float4 color0; };
#define PE_REFRACTION_TWO_SIDED false
uint pe_light_count() { return 1; }
void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{ L = normalize(float3(0.3, 1.0, 0.2)); radiance = float3(1, 1, 1); occlusion = 1.0; }
float3 pe_env_radiance(float3 N, float3 V, float3 L, float2 alpha) { return float3(0.2, 0.2, 0.2); }
float3 pe_env_irradiance(float3 N) { return float3(0.2, 0.2, 0.2); }
float3 pe_view_position() { return float3(0, 0, 5); }
#include "MODULE_PATH"

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void gateCS(uint3 id : SV_DispatchThreadID)
{
    PeSurfacePoint sp;
    sp.svPosition = float4(id.xy + 0.5, 0.5, 1.0);
    sp.P = float3(id.xy * 0.01, 0); sp.N = float3(0, 0, 1); sp.T = float3(1, 0, 0); sp.B = float3(0, 1, 0);
    sp.uv0 = id.xy / 64.0; sp.color0 = float4(1, 1, 1, 1);
    pe_load_material(gateBytes, 0);
    pe_fill_vertex_data(sp);
    gateOut[id.xy] = pe_material_shade();
}
```
  If the MaterialX tree does not ship those exact example names, list `resources/Materials/Examples/OpenPbr/` and use the files there that contain `open_pbr_surface`.

  Run `cd .testpool/tests && python mtlx_codegen.py`. Expected: FAIL (`--mtlx` is unknown, so PhasmaCook falls into its UI path or errors).

- [ ] **Step 2: Overrides.** Create `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/pbrlib/genglsl/lib/mx_environment_prefilter.glsl`. It is GLSL syntax, which MaterialX translates to Slang, and it shadows MaterialX's file of the same path:
```glsl
// PhasmaEngine: MaterialX's prefiltered-environment maths with PE as the radiance source (IBL, GI probes,
// Lumen reflections). Shadows pbrlib/genglsl/lib/mx_environment_prefilter.glsl through the source search path
// (PE registered first). The include is root-relative (libraries/...): a local include would resolve next to this file.
#include "libraries/pbrlib/genglsl/lib/mx_microfacet_specular.glsl"

vec3 mx_environment_radiance(vec3 N, vec3 V, vec3 X, vec2 alpha, int distribution, FresnelData fd)
{
    N = mx_forward_facing_normal(N, V);
    vec3 L = fd.refraction ? mx_refraction_solid_sphere(-V, N, fd.ior.x) : -reflect(V, N);
    float NdotV = clamp(dot(N, V), M_FLOAT_EPS, 1.0);
    float avgAlpha = mx_average_alpha(alpha);
    vec3 FG = mx_ggx_dir_albedo(NdotV, avgAlpha, fd);
    FG = fd.refraction ? vec3(1.0) - FG : FG;
    return pe_env_radiance(N, V, L, alpha) * FG;
}

vec3 mx_environment_irradiance(vec3 N)
{
    return pe_env_irradiance(N);
}
```
  The weighting mirrors MaterialX's file at the tag (`libraries/pbrlib/genglsl/lib/mx_environment_prefilter.glsl`): forward-facing normal, directional albedo `FG`, and `1 − FG` for refraction. Only the lat-long lookup and `$envLightIntensity` are replaced, by `pe_env_radiance`/`pe_env_irradiance`, which carry the engine's intensity.

  Create `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/stdlib/genslang/lib/mx_texture.slang`, with every member and function MaterialX's own file declares:
```slang
// PhasmaEngine: MaterialX textures sample PE's bindless table. Implicit-LOD sampling is kept on purpose:
// the resolve evaluates each pixel in a 2x2 quad on its own triangle, so derivatives are equivalent to a
// pixel shader's (within measured tolerance). Keeps every member MaterialX's own struct exposes.
struct SamplerTexture2D
{
    uint textureIndex;
    uint samplerIndex;

    int get_width(int mipLevel = 0)
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
        return int(w);
    }

    int get_height(int mipLevel = 0)
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
        return int(h);
    }

    int get_num_mip_levels()
    {
        uint w, h, levels;
        pe_textures[NonUniformResourceIndex(textureIndex)].GetDimensions(0u, w, h, levels);
        return int(levels);
    }
};

float4 textureLod(SamplerTexture2D tex, float2 uv, float lod)
{
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].SampleLevel(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv, lod);
}

float4 texture(SamplerTexture2D tex, float2 uv)
{
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].Sample(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv);
}

float4 textureGrad(SamplerTexture2D tex, float2 uv, float2 ddx_, float2 ddy_)
{
    return pe_textures[NonUniformResourceIndex(tex.textureIndex)].SampleGrad(pe_samplers[NonUniformResourceIndex(tex.samplerIndex)], uv, ddx_, ddy_);
}

int2 textureSize(SamplerTexture2D tex, int mipLevel)
{
    uint w, h, levels;
    pe_textures[NonUniformResourceIndex(tex.textureIndex)].GetDimensions(uint(mipLevel), w, h, levels);
    return int2(w, h);
}
```

- [ ] **Step 3: Surface node.** `Phasma/Cook/Code/MaterialX/PeSurfaceNodeSlang.h`:
```cpp
#pragma once
#include <MaterialXGenHw/Nodes/HwSurfaceNode.h>

namespace pe
{
    // MaterialX's surface node with PE's light loop: lights, attenuation and shadows come from the engine
    // (pe_light_sample); MaterialX's BSDF closures are evaluated unchanged. Same seam as Hydra Storm / Maya.
    class PeSurfaceNodeSlang : public MaterialX::HwSurfaceNode
    {
    public:
        static MaterialX::ShaderNodeImplPtr create() { return std::make_shared<PeSurfaceNodeSlang>(); }
        void emitLightLoop(const MaterialX::ShaderNode &node, MaterialX::GenContext &context, MaterialX::ShaderStage &stage,
                           const std::string &outColor) const override;
    };
} // namespace pe
```
  `Phasma/Cook/Code/MaterialX/PeSurfaceNodeSlang.cpp`:
```cpp
#include "PeSurfaceNodeSlang.h"
#include <MaterialXGenHw/HwConstants.h>
#include <MaterialXGenHw/HwShaderGenerator.h>

namespace pe
{
    namespace mx = MaterialX;

    // Mirrors HwSurfaceNode::emitLightLoop (MaterialX 1.39.5, HwSurfaceNode.cpp:263-313) with PE's lights in place of
    // MaterialX's LightData array. The BSDF responses already fold in N.L, so the light only supplies radiance.
    void PeSurfaceNodeSlang::emitLightLoop(const mx::ShaderNode &node, mx::GenContext &context, mx::ShaderStage &stage,
                                           const std::string &outColor) const
    {
        const auto &shadergen = static_cast<const mx::HwShaderGenerator &>(context.getShaderGenerator());
        const mx::ShaderInput *bsdfInput = node.getInput(getBsdfInputName());
        const mx::ShaderNode *bsdf = bsdfInput ? bsdfInput->getConnectedSibling() : nullptr;
        if (!bsdf)
            return;
        const std::string prefix = shadergen.getVertexDataPrefix(stage.getInputBlock(mx::HW::VERTEX_DATA));

        shadergen.emitComment("PhasmaEngine light loop", stage);
        shadergen.emitLine("for (uint peLight = 0; peLight < pe_light_count(); ++peLight)", stage, false);
        shadergen.emitScopeBegin(stage);
        shadergen.emitLine("float3 peRadiance", stage);
        shadergen.emitLine("pe_light_sample(peLight, " + prefix + mx::HW::T_POSITION_WORLD + ", L, peRadiance, occlusion)", stage);
        if (bsdf->hasClassification(mx::ShaderNode::Classification::BSDF_R))
        {
            shadergen.emitLine("ClosureData closureData = makeClosureData(CLOSURE_TYPE_REFLECTION, L, V, N, P, occlusion)", stage);
            shadergen.emitFunctionCall(*bsdf, context, stage);
        }
        else
        {
            shadergen.emitLineBegin(stage);
            shadergen.emitOutput(bsdf->getOutput(), true, true, context, stage);
            shadergen.emitLineEnd(stage);
        }
        shadergen.emitLine(outColor + " += peRadiance * " + bsdf->getOutput()->getVariable() + ".response", stage);
        shadergen.emitLine("occlusion = 1.0", stage);
        shadergen.emitScopeEnd(stage);
    }
} // namespace pe
```
  **Why `hwMaxActiveLightSources = 0` (set in Step 4):**
  - `HwSurfaceNode::emitFunctionCall` calls the virtual `emitLightLoop` unconditionally (`source/MaterialXGenHw/Nodes/HwSurfaceNode.cpp:153`). The `hwMaxActiveLightSources > 0` test lives only inside the base implementation (`:275`).
  - With 0, MaterialX emits no `LightData`, `u_lightData` or `numActiveLightSources` helpers, and PE's loop still runs.
  - `occlusion` is the variable the base `emitFunctionCall` declares. PE's shadow factor goes there, as MaterialX's own `mx_shadow_occlusion` does, and is reset after each light as the base loop resets it.

- [ ] **Step 4: Generator + material ABI.** `Phasma/Cook/Code/MaterialX/MaterialXGen.h`:
```cpp
#pragma once

namespace pe
{
    // .mtlx -> PE Slang module (+ .json layout) for each renderable material, generated by MaterialX 1.39.5's
    // Slang generator with PE's overrides. Returns false and sets error on any failure.
    bool GenerateMaterialXSlang(const std::filesystem::path &mtlx, const std::filesystem::path &outDir, std::string &error);
} // namespace pe
```
  `Phasma/Cook/Code/MaterialX/MaterialXGen.cpp` (prototype-verified; copy it exactly):
```cpp
#include "MaterialXGen.h"
#include "PeSurfaceNodeSlang.h"
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <MaterialXGenHw/HwConstants.h>
#include <MaterialXGenShader/DefaultColorManagementSystem.h>
#include <MaterialXGenShader/Shader.h>
#include <MaterialXGenShader/UnitSystem.h>
#include <MaterialXGenShader/Util.h>
#include <MaterialXGenSlang/SlangShaderGenerator.h>

namespace pe
{
    namespace mx = MaterialX;

    namespace
    {
        // One material input as PE stores it: a static in the module, loaded from the material's byte range.
        struct PeMaterialInput
        {
            std::string var;   // generated static name, e.g. open_pbr_surface_surfaceshader_base_color
            std::string input; // input name on its node, e.g. base_color
            std::string node;  // owning node path
            std::string type;  // Slang type name from the generator's syntax
            uint32_t offset = 0;
            std::string value; // MaterialX value string, empty when the port has none
        };

        uint32_t PeTypeSize(const std::string &type)
        {
            if (type == "float" || type == "int" || type == "uint" || type == "bool")
                return 4;
            if (type == "float2")
                return 8;
            if (type == "float3")
                return 12;
            if (type == "float4")
                return 16;
            if (type == "float3x3")
                return 36;
            if (type == "float4x4")
                return 64;
            if (type == "SamplerTexture2D")
                return 8;
            return 0;
        }

        // The exact lines of pe_load_material for one input (format pinned by .testpool/tests/mtlx_codegen.py).
        std::vector<std::string> LoaderLines(const PeMaterialInput &in)
        {
            const std::string &n = in.var;
            const std::string &t = in.type;
            auto at = [&](uint32_t extra) { return "o + " + std::to_string(in.offset + extra); };
            if (t == "float")
                return {n + " = asfloat(b.Load(" + at(0) + "));"};
            if (t == "float2" || t == "float3" || t == "float4")
                return {n + " = asfloat(b.Load" + t.substr(5) + "(" + at(0) + "));"};
            if (t == "int")
                return {n + " = asint(b.Load(" + at(0) + "));"};
            if (t == "uint")
                return {n + " = b.Load(" + at(0) + ");"};
            if (t == "bool")
                return {n + " = b.Load(" + at(0) + ") != 0u;"};
            if (t == "SamplerTexture2D")
                return {n + ".textureIndex = b.Load(" + at(0) + ");", n + ".samplerIndex = b.Load(" + at(4) + ");"};
            std::vector<std::string> rows;
            const bool m3 = t == "float3x3";
            for (uint32_t r = 0; r < (m3 ? 3u : 4u); ++r)
                rows.push_back(n + "[" + std::to_string(r) + "] = asfloat(b.Load" + (m3 ? "3" : "4") + "(" + at(r * (m3 ? 12u : 16u)) + "));");
            return rows;
        }

        bool References(const std::string &text, const std::string &name)
        {
            return std::regex_search(text, std::regex("\\b" + name + "\\b"));
        }

        // [first, last] of "<header> ... { ... }" with balanced braces, or npos when absent.
        std::pair<size_t, size_t> FindBlock(const std::string &text, const std::string &header)
        {
            const size_t start = text.find(header);
            if (start == std::string::npos)
                return {std::string::npos, std::string::npos};
            size_t i = text.find('{', start);
            int depth = 0;
            for (; i < text.size(); ++i)
            {
                depth += text[i] == '{' ? 1 : text[i] == '}' ? -1 : 0;
                if (depth == 0 && text[i] == '}')
                    return {start, i};
            }
            return {std::string::npos, std::string::npos};
        }

        // "0.8, 0.8, 0.8" -> [0.8,0.8,0.8]; booleans -> [1]/[0]; textures -> [].
        std::string JsonDefault(const PeMaterialInput &in)
        {
            if (in.type == "SamplerTexture2D" || in.value.empty())
                return "[]";
            if (in.type == "bool")
                return in.value == "true" ? "[1]" : "[0]";
            std::string out = "[";
            std::stringstream ss(in.value);
            std::string item;
            bool first = true;
            while (std::getline(ss, item, ','))
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.9g", std::stod(item));
                out += (first ? "" : ",") + std::string(buf);
                first = false;
            }
            return out + "]";
        }

        bool Connected(const mx::InputPtr &in)
        {
            return in && (in->hasNodeName() || in->hasNodeGraphString() || in->hasInterfaceName() || in->hasOutputString());
        }
    } // namespace

    bool GenerateMaterialXSlang(const std::filesystem::path &mtlx, const std::filesystem::path &outDir, std::string &error)
    {
        try
        {
            const std::filesystem::path mxRoot = std::filesystem::path(Path::ExecutablePath()) / "MaterialX";
            mx::FileSearchPath libraryPath(mx::FilePath(mxRoot.string()));
            mx::DocumentPtr libs = mx::createDocument();
            mx::loadLibraries({"libraries"}, libraryPath, libs);

            mx::DocumentPtr doc = mx::createDocument();
            mx::FileSearchPath docPath(mx::FilePath(mtlx.parent_path().string()));
            mx::readFromXmlFile(doc, mx::FilePath(mtlx.string()), docPath);
            doc->setDataLibrary(libs);
            std::string message;
            if (!doc->validate(&message))
            {
                error = mtlx.string() + " is not a valid MaterialX document: " + message;
                return false;
            }

            mx::ShaderGeneratorPtr gen = mx::SlangShaderGenerator::create();
            gen->registerImplementation("IM_surface_" + mx::SlangShaderGenerator::TARGET, PeSurfaceNodeSlang::create);
            mx::GenContext ctx(gen);
            // Library includes are root-relative ("libraries/stdlib/..."), first match wins: PE's overrides
            // (pe_overrides/libraries/...) shadow MaterialX's files of the same relative path.
            ctx.registerSourceCodeSearchPath(mx::FilePath((mxRoot / "pe_overrides").string()));
            ctx.registerSourceCodeSearchPath(mx::FilePath(mxRoot.string()));
            ctx.registerSourceCodeSearchPath(mx::FilePath(mtlx.parent_path().string()));
            mx::GenOptions &options = ctx.getOptions();
            options.shaderInterfaceType = mx::SHADER_INTERFACE_COMPLETE;
            options.hwSpecularEnvironmentMethod = mx::SPECULAR_ENVIRONMENT_PREFILTER;
            options.hwTransmissionRenderMethod = mx::TRANSMISSION_REFRACTION;
            options.hwMaxActiveLightSources = 0; // PeSurfaceNodeSlang emits PE's own loop
            options.targetColorSpaceOverride = "lin_rec709";
            options.targetDistanceUnit = "meter";
            options.fileTextureVerticalFlip = false;

            mx::DefaultColorManagementSystemPtr cms = mx::DefaultColorManagementSystem::create(gen->getTarget());
            cms->loadLibrary(libs);
            gen->setColorManagementSystem(cms);
            mx::UnitSystemPtr units = mx::UnitSystem::create(gen->getTarget());
            mx::UnitConverterRegistryPtr registry = mx::UnitConverterRegistry::create();
            for (const char *unitType : {"distance", "angle"})
            {
                mx::UnitTypeDefPtr def = libs->getUnitTypeDef(unitType);
                if (def)
                    registry->addUnitConverter(def, mx::LinearUnitConverter::create(def));
            }
            units->loadLibrary(libs);
            units->setUnitConverterRegistry(registry);
            gen->setUnitSystem(units);

            std::vector<mx::TypedElementPtr> elements = mx::findRenderableElements(doc);
            if (elements.empty())
            {
                error = mtlx.string() + " has no renderable material";
                return false;
            }
            std::filesystem::create_directories(outDir);

            for (const mx::TypedElementPtr &elem : elements)
            {
                const std::string name = elem->getName();
                mx::ShaderPtr shader = gen->generate(name, elem, ctx);
                if (!shader)
                {
                    error = name + ": MaterialX generated no shader";
                    return false;
                }
                std::string text = shader->getSourceCode(mx::Stage::PIXEL);
                const mx::ShaderStage &pixel = shader->getStage(mx::Stage::PIXEL);

                // --- Material inputs: the public uniforms, in generator order, tightly packed. ---
                std::vector<PeMaterialInput> inputs;
                std::vector<std::string> dropped; // aggregate members (surfaceshader, displacementshader)
                uint32_t offset = 0;
                const mx::VariableBlock &pub = pixel.getUniformBlock(mx::HW::PUBLIC_UNIFORMS);
                for (size_t i = 0; i < pub.size(); ++i)
                {
                    const mx::ShaderPort *port = pub[i];
                    PeMaterialInput in;
                    in.var = port->getVariable();
                    in.type = gen->getSyntax().getTypeName(port->getType());
                    const std::string path = port->getPath();
                    const size_t slash = path.rfind('/');
                    in.input = slash == std::string::npos ? path : path.substr(slash + 1);
                    in.node = slash == std::string::npos ? std::string() : path.substr(0, slash);
                    in.value = port->getValue() ? port->getValue()->getValueString() : std::string();
                    const uint32_t size = PeTypeSize(in.type);
                    if (size == 0)
                    {
                        dropped.push_back(in.var);
                        continue;
                    }
                    in.offset = offset;
                    offset += size;
                    inputs.push_back(in);
                }

                // --- Engine-owned private uniforms. ---
                bool refractionTwoSided = false;
                const mx::VariableBlock &priv = pixel.getUniformBlock(mx::HW::PRIVATE_UNIFORMS);
                for (size_t i = 0; i < priv.size(); ++i)
                {
                    const std::string var = priv[i]->getVariable();
                    if (var == "u_refractionTwoSided")
                        refractionTwoSided = true;
                    else if (var != "u_viewPosition")
                        dropped.push_back(var);
                }

                // --- Replace the uniform block with statics. ---
                const auto [cbStart, cbEnd] = FindBlock(text, "cbuffer pixelCB");
                if (cbStart == std::string::npos)
                {
                    error = name + ": generated pixel stage has no 'cbuffer pixelCB' block";
                    return false;
                }
                std::string statics;
                for (const PeMaterialInput &in : inputs)
                    statics += "static " + in.type + " " + in.var + ";\n";
                if (refractionTwoSided)
                    statics += "static const bool u_refractionTwoSided = PE_REFRACTION_TWO_SIDED;\n";
                text.replace(cbStart, cbEnd - cbStart + 1, statics);
                text = std::regex_replace(text, std::regex("\\bu_viewPosition\\b"), "pe_view_position()");
                for (const std::string &var : dropped)
                {
                    if (References(text, var))
                    {
                        error = name + ": '" + var + "' is still referenced after the ABI transform (unsupported input or override missing)";
                        return false;
                    }
                }

                // --- The fragment entry becomes a function the resolve calls. ---
                const std::regex entry(R"(\[shader\("fragment"\)\]\s*float4\s+fragmentMain\s*\(\s*VertexData\s+_vd\s*\)\s*:\s*SV_Target)");
                if (!std::regex_search(text, entry))
                {
                    error = name + ": fragmentMain signature not found";
                    return false;
                }
                text = std::regex_replace(text, entry, "float4 pe_material_fragment(VertexData _vd)");

                // --- Loader, vertex data, shade, thin values. ---
                std::string tail = "\nvoid pe_load_material(ByteAddressBuffer b, uint o)\n{\n";
                for (const PeMaterialInput &in : inputs)
                    for (const std::string &line : LoaderLines(in))
                        tail += "    " + line + "\n";
                tail += "}\n\nvoid pe_fill_vertex_data(PeSurfacePoint sp)\n{\n";
                if (text.find("SV_Position : SV_Position") != std::string::npos)
                    tail += "    vd.SV_Position = sp.svPosition;\n";
                std::vector<std::string> vertexData;
                const mx::VariableBlock &vdBlock = pixel.getInputBlock(mx::HW::VERTEX_DATA);
                for (size_t i = 0; i < vdBlock.size(); ++i)
                {
                    const std::string var = vdBlock[i]->getVariable();
                    const std::string type = gen->getSyntax().getTypeName(vdBlock[i]->getType());
                    static const std::map<std::string, std::string> kSource = {
                        {"positionWorld", "sp.P"}, {"normalWorld", "sp.N"}, {"tangentWorld", "sp.T"},
                        {"bitangentWorld", "sp.B"}, {"texcoord_0", "sp.uv0"}, {"color_0", "sp.color0"}};
                    auto it = kSource.find(var);
                    if (it == kSource.end() || (var == "texcoord_0" && type != "float2"))
                    {
                        error = name + ": unsupported vertex data '" + var + "' (" + type + ")";
                        return false;
                    }
                    const std::string swizzle = var == "color_0" && type == "float3" ? ".rgb" : "";
                    tail += "    vd." + var + " = " + it->second + swizzle + ";\n";
                    vertexData.push_back(var);
                }
                tail += "}\n\nfloat4 pe_material_shade()\n{\n    return pe_material_fragment(vd);\n}\n";

                // Pass 1 (thin G-buffer) values: only from unconnected inputs until Phase 3 evaluates subgraphs.
                bool thin = false;
                mx::NodePtr material = elem->asA<mx::Node>();
                mx::NodePtr surface = material ? material->getConnectedNode("surfaceshader") : nullptr;
                if (surface && surface->getCategory() == "open_pbr_surface")
                {
                    auto var = [&](const std::string &input) -> std::string
                    {
                        for (const PeMaterialInput &in : inputs)
                            if (in.node == surface->getNamePath() && in.input == input)
                                return in.var;
                        return {};
                    };
                    const std::string baseColor = var("base_color"), baseWeight = var("base_weight"),
                                      roughness = var("specular_roughness"), metalness = var("base_metalness");
                    thin = !baseColor.empty() && !baseWeight.empty() && !roughness.empty() && !metalness.empty() &&
                           !Connected(surface->getInput("geometry_normal"));
                    if (thin)
                        tail += "\nstruct PeThin\n{\n    float3 normal;\n    float3 albedo;\n    float roughness;\n    float metalness;\n};\n\n"
                                "PeThin pe_material_thin(PeSurfacePoint sp)\n{\n    PeThin t;\n    t.normal = normalize(sp.N);\n"
                                "    t.albedo = " + baseColor + " * " + baseWeight + ";\n    t.roughness = " + roughness +
                                ";\n    t.metalness = " + metalness + ";\n    return t;\n}\n";
                }
                text += tail;

                std::string json = "{\"material\":\"" + name + "\",\"inputs\":[";
                for (size_t i = 0; i < inputs.size(); ++i)
                {
                    const PeMaterialInput &in = inputs[i];
                    json += std::string(i ? "," : "") + "{\"name\":\"" + in.var + "\",\"input\":\"" + in.input + "\",\"node\":\"" +
                            in.node + "\",\"type\":\"" + in.type + "\",\"offset\":" + std::to_string(in.offset) +
                            ",\"default\":" + JsonDefault(in) + "}";
                }
                json += "],\"vertexData\":[";
                for (size_t i = 0; i < vertexData.size(); ++i)
                    json += std::string(i ? "," : "") + "\"" + vertexData[i] + "\"";
                json += "],\"byteSize\":" + std::to_string(offset) + ",\"thin\":" + (thin ? "true" : "false") + "}\n";

                std::ofstream(outDir / (name + ".slang"), std::ios::binary) << text;
                std::ofstream(outDir / (name + ".json"), std::ios::binary) << json;
            }
            return true;
        }
        catch (const std::exception &e)
        {
            error = std::string("MaterialX: ") + e.what();
            return false;
        }
    }
} // namespace pe
```
  **What the transform guarantees** (the test checks all of it):
  - The `cbuffer pixelCB` block (no trailing semicolon in MaterialX's output) is replaced by `static` declarations.
  - `u_viewPosition` → `pe_view_position()`, and `u_refractionTwoSided` → `PE_REFRACTION_TWO_SIDED`.
  - Every other private uniform (`u_env*`) and every aggregate public member (`surfaceshader backsurfaceshader`, `displacementshader displacementshader1`) must be unreferenced; otherwise generation fails and names it.
  - **Layout:** tightly packed, 4-byte aligned. `float3` = 12 bytes, `bool` = one `uint`, matrices row by row, `SamplerTexture2D` = two `uint`s.
  - **Loader lines:**

    | Type | Emitted line(s) |
    |---|---|
    | `float` / `float2` / `float3` / `float4` | `name = asfloat(b.Load(o + OFF));` / `Load2` / `Load3` / `Load4` |
    | `int` | `name = asint(b.Load(o + OFF));` |
    | `uint` | `name = b.Load(o + OFF);` |
    | `bool` | `name = b.Load(o + OFF) != 0u;` |
    | `float3x3` | `name[r] = asfloat(b.Load3(o + OFF+12r));` for r = 0..2 |
    | `float4x4` | `name[r] = asfloat(b.Load4(o + OFF+16r));` for r = 0..3 |
    | `SamplerTexture2D` | `name.textureIndex = b.Load(o + OFF);` and `name.samplerIndex = b.Load(o + OFF+4);` |

  - **Vertex data** is filled by member name: `positionWorld`←`sp.P`, `normalWorld`←`sp.N`, `tangentWorld`←`sp.T`, `bitangentWorld`←`sp.B`, `texcoord_0`←`sp.uv0`, `color_0`←`sp.color0`, and `SV_Position`←`sp.svPosition`. Any other member is an error.
  - **UV geomprop index uniforms** (`geomprop_UV0_index`, an `int`) stay ordinary inputs; PE stores 0.

  `Phasma/Cook/main.cpp`:
  1. Near the top, after the existing includes:
```cpp
#ifdef PE_MATERIALX
#include "MaterialX/MaterialXGen.h"
#endif
```
  2. In `main`, on the Windows path, before `if (args.size() >= 3 && args[1] == L"--batch")`:
```cpp
#ifdef PE_MATERIALX
    if (args.size() >= 4 && args[1] == L"--mtlx")
    {
        std::string error;
        if (!pe::GenerateMaterialXSlang(std::filesystem::path(args[2]), std::filesystem::path(args[3]), error))
        {
            std::fprintf(stderr, "[Cook] MaterialX generation failed: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
#endif
```
  3. On the POSIX path, before `if (argc >= 3 && std::strcmp(argv[1], "--batch") == 0)`:
```cpp
#ifdef PE_MATERIALX
    if (argc >= 4 && std::strcmp(argv[1], "--mtlx") == 0)
    {
        std::string error;
        if (!pe::GenerateMaterialXSlang(std::filesystem::path(reinterpret_cast<const char8_t *>(argv[2])),
                                        std::filesystem::path(reinterpret_cast<const char8_t *>(argv[3])), error))
        {
            std::fprintf(stderr, "[Cook] MaterialX generation failed: %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
#endif
```

- [ ] **Step 4b: Ship the overrides next to PhasmaCook.** Add a second command to the same `POST_BUILD` from Task 1:
```cmake
    add_custom_command(TARGET PhasmaCook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides" "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/pe_overrides")
```
- [ ] **Step 5: Build, run, pass.** Run `python mtlx_codegen.py`. Expected: `mtlx codegen ok`. This proves:
  - search-path order picks up the overrides;
  - the light loop is PE's;
  - the ABI transform runs;
  - the generated code compiles with quad derivatives to SPIR-V (`spirv-val`) and to DXIL `sm_6_6`.

  **If a gate fails: STOP** and report which one. The design does not hold without it. For example, if an override is not picked up because MaterialX includes that file through a local path, the fallback is `registerImplementation` for the node that includes it. That is a change of approach and needs the user.
- [ ] **Step 6:** Register `mtlx-codegen` in the catalog (cost "cheap", requires PhasmaCook). Run `clang-format -i` on the four C++ files and `main.cpp`.

### Task 3: Capabilities and the `openpbr` switch

**Files:**
- `Phasma/Core/Code/API/RHI.h` (struct `GpuFeatureSupport`)
- `Phasma/Core/Code/API/RHI.cpp`: `RHI::CreateDevice` (Vulkan) and the DX12 branch of `RHI::Init`
- Settings plumbing (Appendix A.2): `Phasma/Core/Code/Base/Settings.h`, `Phasma/Runtime/Code/Script/Bindings/Settings/SettingsBindings.cpp`, `Phasma/Runtime/Code/Scene/SceneSerializer.cpp`, `Phasma/Editor/Code/GUI/Widgets/PipelineControls.cpp`, `Phasma/Runtime/Code/RenderPasses/GlobalIlluminationPass.cpp`
- `Phasma/Runtime/Code/Render/SceneRenderTargets.{h,cpp}`

**Interfaces (produced):**
- `RHII.GetGpuFeatureSupport().fragmentBarycentric`, `.computeDerivativeQuads` (bool): true only when the feature is **enabled** on the device.
- `bool SceneUsesOpenPBR();` and `bool SceneUsesOpenPBRDebug();`, declared in `SceneRenderTargets.h`.
- Settings `openpbr`, `openpbr_debug` (bool, default false).

- [ ] **Step 1: Failing test.** Create `.testpool/tests/openpbr_vis.py` from Appendix A.1, and add:
```python
def check_caps_logged():
    lines = LOG.read_text(errors="replace").splitlines()
    caps = [l for l in lines if "[OpenPBR]" in l and "caps" in l]
    assert caps, "no [OpenPBR] caps line in the log"
    assert "barycentrics=1" in caps[-1] and "derivatives=1" in caps[-1], f"desktop GPU lacks a required cap: {caps[-1]}"
```
  Set `CHECKS = [check_setting_roundtrip, check_caps_logged]`. Run `python openpbr_vis.py --api vulkan`. Expected: FAIL (no setting, no caps line).

- [ ] **Step 2: `GpuFeatureSupport`.** In `RHI.h`, after `bool geometryShader = false;` inside `struct GpuFeatureSupport`:
```cpp
        bool fragmentBarycentric = false;    // enabled: GetAttributeAtVertex (VK_KHR_fragment_shader_barycentric / D3D12 OPTIONS3)
        bool computeDerivativeQuads = false; // enabled: [DerivativeGroupQuad] compute (VK_KHR_compute_shader_derivatives / SM 6.6)
```
- [ ] **Step 3: Vulkan.** All edits are in `RHI::CreateDevice` (`RHI.cpp`, starts about line 1594). It owns the final enabled state, so the caps are set there; `FindGpu` is not touched.
  1. After `const bool depthClipExtAvailable = …;` (anchor `IsDeviceExtensionValid(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME)`):
```cpp
        const bool barycentricExtAvailable =
            optionalExtensionsAllowed && IsDeviceExtensionValid(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
        const bool derivativesExtAvailable =
            optionalExtensionsAllowed && IsDeviceExtensionValid(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
```
  2. After the block that chains `depthClipFeatures` into `featureQueryChain` (anchor: `vk::PhysicalDeviceDepthClipEnableFeaturesEXT depthClipFeatures{};` and its `if (depthClipExtAvailable)` block):
```cpp
        vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentricFeatures{};
        if (barycentricExtAvailable)
        {
            barycentricFeatures.pNext = featureQueryChain;
            featureQueryChain = &barycentricFeatures;
        }
        vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeFeatures{};
        if (derivativesExtAvailable)
        {
            derivativeFeatures.pNext = featureQueryChain;
            featureQueryChain = &derivativeFeatures;
        }
```
  3. After `if (depthClipExtAvailable && depthClipFeatures.depthClipEnable) deviceExtensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);` (about line 1784):
```cpp
        const bool barycentricEnabled = barycentricExtAvailable && barycentricFeatures.fragmentShaderBarycentric;
        const bool derivativeQuadsEnabled = derivativesExtAvailable && derivativeFeatures.computeDerivativeGroupQuads;
        if (barycentricEnabled)
            deviceExtensions.push_back(VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME);
        if (derivativeQuadsEnabled)
            deviceExtensions.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
```
  4. After the device-create chain block that prepends `depthClipFeatures` to `deviceFeatureChain` (about line 1907-1911). Enable only what PE uses: quad grouping, not linear:
```cpp
        vk::PhysicalDeviceFragmentShaderBarycentricFeaturesKHR barycentricEnable{};
        barycentricEnable.fragmentShaderBarycentric = VK_TRUE;
        if (barycentricEnabled)
        {
            barycentricEnable.pNext = deviceFeatureChain;
            deviceFeatureChain = &barycentricEnable;
        }
        vk::PhysicalDeviceComputeShaderDerivativesFeaturesKHR derivativeEnable{};
        derivativeEnable.computeDerivativeGroupQuads = VK_TRUE;
        if (derivativeQuadsEnabled)
        {
            derivativeEnable.pNext = deviceFeatureChain;
            deviceFeatureChain = &derivativeEnable;
        }
```
  5. After `vk->m_device = vk->m_gpu.createDevice(deviceCreateInfo);`:
```cpp
        m_gpuFeatureSupport.fragmentBarycentric = barycentricEnabled;
        m_gpuFeatureSupport.computeDerivativeQuads = derivativeQuadsEnabled;
        PE_INFO("[OpenPBR] caps barycentrics=%d derivatives=%d", barycentricEnabled ? 1 : 0, derivativeQuadsEnabled ? 1 : 0);
```
  The bundled header (`VK_HEADER_VERSION 350`) declares both `vk::` structs and both extension-name macros.
- [ ] **Step 4: DX12.** In `RHI::Init`, inside `if (api == PE_GRAPHICS_API_DX12)`, after `m_gpuFeatureSupport.queryPoolHostReset = false;`:
```cpp
            D3D12_FEATURE_DATA_D3D12_OPTIONS3 options3{};
            const bool barycentrics =
                SUCCEEDED(dx->GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &options3, sizeof(options3))) &&
                options3.BarycentricsSupported;
            m_gpuFeatureSupport.fragmentBarycentric = barycentrics;
            m_gpuFeatureSupport.computeDerivativeQuads = true; // Dx12RhiImpl::Init requires SM 6.6: compute derivatives on 2x2 quads
            PE_INFO("[OpenPBR] caps barycentrics=%d derivatives=1", barycentrics ? 1 : 0);
```
- [ ] **Step 5: Settings.** Apply Appendix A.2 for `openpbr`, then the same for `openpbr_debug`, each line after the previous one.
- [ ] **Step 6: `SceneUsesOpenPBR()`.**
  - In `SceneRenderTargets.h`, after `bool SceneUsesLinearColor();`:
```cpp
    // openpbr is on and this platform/GPU runs the MaterialX visibility-buffer path (warns once when it cannot).
    bool SceneUsesOpenPBR();
    bool SceneUsesOpenPBRDebug();
```
  - In `SceneRenderTargets.cpp`, after `SceneUsesLinearColor()`:
```cpp
    bool SceneUsesOpenPBR()
    {
        if (!Settings::Get<SceneSettings>().openpbr)
            return false;
#if defined(PE_ANDROID)
        static bool warnedAndroid = false;
        if (!warnedAndroid)
            PE_WARN("openpbr: the visibility-buffer path is desktop-only; Android renders the legacy material path");
        warnedAndroid = true;
        return false;
#else
        const auto &f = RHII.GetGpuFeatureSupport();
        const bool ok = f.fragmentBarycentric && f.computeDerivativeQuads && RHII.GetGpuLimits().maxColorAttachments >= 7;
        static bool warned = false;
        if (!ok && !warned)
            PE_WARN("openpbr: this GPU lacks fragment barycentrics or compute quad derivatives; using the legacy material path");
        warned = warned || !ok;
        return ok;
#endif
    }

    bool SceneUsesOpenPBRDebug()
    {
        return SceneUsesOpenPBR() && Settings::Get<SceneSettings>().openpbr_debug;
    }
```
- [ ] **Step 7:** Build, then run `openpbr_vis.py` on both APIs. Expected: `openpbr ok vulkan` / `dx12`. Read the log. Run `clang-format`, and register `openpbr-vis-vulkan` / `openpbr-vis-dx12` in the catalog.

---

## Phase 1 — visibility buffer + debug view

### Task 4: `visibility` target lifecycle

**Files:** `Phasma/Runtime/Code/Render/SceneRenderTargets.cpp`, `Phasma/Runtime/Code/Render/SceneRendererCore.{h,cpp}`.

**Interfaces (produced):**
- Render target `"visibility"`, `PE_FORMAT_R32G32B32A32_UINT`, present iff `SceneUsesOpenPBR()`.
- Render target `"visibility_debug"`, `PE_FORMAT_R8G8B8A8_UNORM` with `PE_IMAGE_USAGE_TRANSFER_SRC`, present iff `SceneUsesOpenPBRDebug()`.
- A toggle sets `SceneRendererCore::m_colorFormatChanged`, so every pass rebuilds its pipelines.

- [ ] **Step 1: Failing test.** Add to `openpbr_vis.py`:
```python
def image_names():
    r = tool("list_image_resources", {}); assert not r.get("error"), r
    return {i.get("name") for i in r["resources"]}


def assert_settles_to(before, what):
    # A toggle rebuilds the targets and the lighting history takes a few seconds to settle back (measured 2026-10-14:
    # 6 pixels 1/255 off at 5 s, identical by 10 s). Exact identity must return within 20 s; a lasting change never does.
    from openpbr_baseline import full_shot
    for _ in range(20):
        now = np.asarray(full_shot(), np.int16)
        if now.shape == before.shape and np.abs(now - before).max() == 0:
            return
        time.sleep(1)
    diff = np.abs(now - before).max(-1) if now.shape == before.shape else None
    raise AssertionError(f"{what}: " + (f"{int((diff > 0).sum())} pixels, max {int(diff.max())}/255 after 20 s"
                                        if diff is not None else "image size changed"))


def check_toggle_identity():
    # Same editor, same fixed scene: off pixels after on->off (plain, and with a render-scale change landing in the
    # same frame) must equal the off pixels before the first toggle, at full resolution.
    from openpbr_baseline import full_shot, load_mix
    new_scene(); load_mix()
    lua('settings.set("openpbr",false) settings.set("render_scale",1) return "ok"'); time.sleep(5)  # default is 0.75
    before = np.asarray(full_shot(), np.int16)
    setting("openpbr", True, 5); assert "visibility" in image_names(), "openpbr on: no visibility target"
    setting("openpbr", False, 5); assert "visibility" not in image_names(), "openpbr off: visibility target kept"
    assert_settles_to(before, "off pixels changed after an on/off toggle")
    lua('settings.set("render_scale",0.9) settings.set("openpbr",true) return "ok"'); time.sleep(5)
    assert "visibility" in image_names(), "openpbr + render_scale in one frame: no visibility target"
    lua('settings.set("render_scale",1) settings.set("openpbr",false) return "ok"'); time.sleep(5)
    assert_settles_to(before, "off pixels changed after a toggle+resize cycle")
```
  Append `check_toggle_identity` to `CHECKS`. Fresh-start identity against `BASELINE` stays Appendix A.1's `check_off_identity`, which runs after the editor closes. Run the test: it fails ("no visibility target").

- [ ] **Step 2: Create.** In `CreateDefaultSceneRenderTargets` (`SceneRenderTargets.cpp`), after the `"transparency"` line:
```cpp
        if (SceneUsesOpenPBR())
            CreateSceneRenderTarget(renderTargets, "visibility", PE_FORMAT_R32G32B32A32_UINT);
        if (SceneUsesOpenPBRDebug())
            CreateSceneRenderTarget(renderTargets, "visibility_debug", PE_FORMAT_R8G8B8A8_UNORM, PE_IMAGE_USAGE_TRANSFER_SRC);
```
  `CreateSceneRenderTarget`'s default clear colour is `Color::Transparent`, all zero bits, which is a `uint4(0)` clear: "no visibility surface".
- [ ] **Step 3: Sync.** Replace `SyncOptionalSceneRenderTargets` (`SceneRenderTargets.cpp`) with:
```cpp
    bool SyncOptionalSceneRenderTargets(SceneRenderTargetMap &renderTargets, bool hasRayTracingGeometry)
    {
        const bool needVelocity = SceneNeedsVelocityRT(hasRayTracingGeometry);
        const ::PeFormat velocityFormat = SceneVelocityRTFormat();
        Image *velocity = GetSceneRenderTarget(renderTargets, "velocity");
        const ::PeFormat normalFormat = SceneNormalRTFormat();
        Image *normal = GetSceneRenderTarget(renderTargets, "normal");
        Image *visibility = GetSceneRenderTarget(renderTargets, "visibility");
        Image *visibilityDebug = GetSceneRenderTarget(renderTargets, "visibility_debug");
        const bool velocityChanged = needVelocity ? !velocity || velocity->GetFormat() != velocityFormat : velocity != nullptr;
        const bool normalChanged = !normal || normal->GetFormat() != normalFormat;
        const bool visibilityChanged = SceneUsesOpenPBR() != (visibility != nullptr);
        const bool visibilityDebugChanged = SceneUsesOpenPBRDebug() != (visibilityDebug != nullptr);
        if (!velocityChanged && !normalChanged && !visibilityChanged && !visibilityDebugChanged)
            return false;

        RHII.WaitDeviceIdle();

        if (velocityChanged)
        {
            if (velocity)
                DestroySceneRenderTarget(renderTargets, "velocity");
            if (needVelocity)
                CreateSceneRenderTarget(renderTargets, "velocity", velocityFormat);
        }

        if (normalChanged)
        {
            if (normal)
                DestroySceneRenderTarget(renderTargets, "normal");
            CreateSceneRenderTarget(renderTargets, "normal", normalFormat);
        }

        if (visibilityChanged)
        {
            if (visibility)
                DestroySceneRenderTarget(renderTargets, "visibility");
            else
                CreateSceneRenderTarget(renderTargets, "visibility", PE_FORMAT_R32G32B32A32_UINT);
        }

        if (visibilityDebugChanged)
        {
            if (visibilityDebug)
                DestroySceneRenderTarget(renderTargets, "visibility_debug");
            else
                CreateSceneRenderTarget(renderTargets, "visibility_debug", PE_FORMAT_R8G8B8A8_UNORM, PE_IMAGE_USAGE_TRANSFER_SRC);
        }

        return true;
    }
```
- [ ] **Step 4: Rebuild flag.** Apply Appendix A.3 to `SceneRendererCore.{h,cpp}`.
- [ ] **Step 5:** Build, run `openpbr_vis.py` on both APIs (expected: pass), read the log, run `clang-format`.

### Task 5: Visibility payload in the prepass

**Files:**
- `Shaders/Common/Structures.hlsl`: `VS_INPUT_Depth`, `VS_OUTPUT_Position_Uv_ID`, `PS_INPUT_Position_Uv_ID`, `PS_OUTPUT_Gbuffer`
- `Shaders/Depth/DepthVS.hlsl`; `Shaders/Depth/VisibilityPS.hlsl` (new)
- `Phasma/Runtime/Code/RenderPasses/DepthPass.{h,cpp}`, `DepthLatePass.{h,cpp}`
- `Phasma/Runtime/Code/RenderPasses/GbufferPass.{h,cpp}`
- `Shaders/Voxel/VoxelGBufferPS.hlsl`, `Shaders/Terrain/TerrainGBufferPS.hlsl`

(All shader paths are under `Phasma/Runtime/RuntimeAssets/`.)

**Interfaces (produced):**
- Each `visibility` texel is `uint4(drawId + 1, v0, v1, v2)`, where `v*` are global indices into the scene vertex store (`pe::Vertex`, 96 bytes, at `Scene::GetVerticesOffset()`). `x = 0` means not a visibility surface.
- The prepass position stream and the vertex store are index-parallel: `Scene.cpp:768-772` gives both the same `mi->vertexOffset`, and every draw uses one `vertexOffset` for both.
- `BlendState VisibilityBlendState(bool write)` is declared in `DepthPass.h` and used by `GbufferPass.cpp`.

- [ ] **Step 1: Test.** Task 6's depth-match check is this task's test. Here, run `cooked-clusters-*`, `linear-color-*`, `depth-equal-positions`, `renderer-features-shaders` and `openpbr_vis.py` (which includes the off-identity check) after Step 8.
- [ ] **Step 2: Structures.** In `Structures.hlsl`:
  - In `struct VS_INPUT_Depth`, after `uint id : PE_DRAW_ID;`:
```hlsl
#ifdef VISIBILITY
    uint vertexId : SV_VertexID;
#endif
```
  - In both `struct VS_OUTPUT_Position_Uv_ID` and `struct PS_INPUT_Position_Uv_ID`, between `float spriteBlend : TEXCOORD4;` and `float4 position : SV_POSITION;`. The member order must be identical in both: SPIR-V locations follow declaration order.
```hlsl
#ifdef VISIBILITY
    nointerpolation uint vertexId : TEXCOORD5;
    nointerpolation uint visEligible : TEXCOORD6;
#endif
```
  - Replace `struct PS_OUTPUT_Gbuffer` with:
```hlsl
struct PS_OUTPUT_Gbuffer
{
    float4 normal : SV_TARGET0;
    float4 albedo : SV_TARGET1;
    float4 metRough : SV_TARGET2;
#ifdef GBUFFER_NO_VELOCITY
    float4 emissive : SV_TARGET3;
    float transparency : SV_TARGET4;
#ifdef GBUFFER_VISIBILITY
    uint4 visibility : SV_TARGET5;
#endif
#else
    float2 velocity : SV_TARGET3;
    float4 emissive : SV_TARGET4;
    float transparency : SV_TARGET5;
#ifdef GBUFFER_VISIBILITY
    uint4 visibility : SV_TARGET6;
#endif
#endif
};
```
- [ ] **Step 3: DepthVS.** In `DepthVS.hlsl`, after `[[vk::binding(1, 0)]] StructuredBuffer<Mesh_Constants> constants;`:
```hlsl
#if defined(VISIBILITY) && defined(PE_DX12)
struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

// Scene::GetIndirectAll(): one command per Mesh_Constants entry. Set 0, next to data and constants: in set 1 it
// would share DepthPS's table with the unbounded textures[] (Dx12RootSignature.cpp) and alias textures[1].
[[vk::binding(2, 0)]] StructuredBuffer<DrawIndexedIndirectCommand> IndirectAll;
#endif
```
  After `output.alphaFactor = constants[id].baseColorAlpha;`:
```hlsl
#ifdef VISIBILITY
    // Global vertex index: Vulkan's VertexIndex includes the draw's base vertex, D3D12's SV_VertexID does not.
#ifdef PE_DX12
    output.vertexId = input.vertexId + uint(IndirectAll[id].vertexOffset);
#else
    output.vertexId = input.vertexId;
#endif
    // Vertices skinned above, and animated sprites, keep the raster path until Phase 4 pre-skins them. Same test as the
    // skinning branch: static meshes cooked before boneless meshes got zero weights carry weight 1.
    const float weightSum = input.weights[0] + input.weights[1] + input.weights[2] + input.weights[3];
    output.visEligible = (spriteFrameBlend || (pc.jointsCount > 0 && weightSum > 0.0)) ? 0u : 1u;
#endif
```
  Amended 2026-10-13 after Task 6's first run. The original test, `weightSum > 0` on its own, gave Sponza 0% coverage: its `Sponza.pemesh` was cooked on 2026-07-19, before `eaa05195` (2026-09-06) gave boneless meshes zero weights, so every static vertex carries weight 1. Eligibility must match the condition under which DepthVS actually skins.
  The `precise` position maths is untouched. Task 6 verifies the base-vertex semantics on both APIs. If Vulkan's DXC arguments include `-fvk-support-nonzero-base-vertex` (grep `VulkanShaderImpl.cpp`), apply the `IndirectAll` add on Vulkan too, and report it.
- [ ] **Step 4: VisibilityPS.hlsl** (new, `Shaders/Depth/VisibilityPS.hlsl`). Its discard is DepthPS's, expression for expression, so coverage and depth match the depth buffer texel for texel:
```hlsl
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"

// Visibility payload for the MaterialX resolve: draw id + the triangle's three global vertex indices
// (GetAttributeAtVertex on a nointerpolation index), so LOD, clusters and instancing need no command table.
// Alpha-cut mirrors DepthPS exactly, so coverage matches the depth buffer texel for texel.

[[vk::binding(0, 1)]] StructuredBuffer<Mesh_Constants> constants : register(t0, space1);
[[vk::binding(1, 1)]] SamplerState material_sampler : register(s1, space1);
[[vk::binding(2, 1)]] Texture2D textures[] : register(t2, space1);

float4 SampleArray(float2 uv, uint index)
{
    if (index == 0xFFFFFFFF)
        return float4(1.0, 1.0, 1.0, 1.0);
    return textures[NonUniformResourceIndex(index)].Sample(material_sampler, uv);
}

uint4 mainPS(PS_INPUT_Position_Uv_ID input) : SV_TARGET0
{
    const uint id = input.id;
    float4 sampledBase = HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT) ? SampleArray(input.uv, constants[id].meshImageIndex[0]) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float spriteBlend = saturate(input.spriteBlend);
    if (spriteBlend > 0.0f && HasTexture(constants[id].textureMask, TEX_BASE_COLOR_BIT))
        sampledBase = lerp(sampledBase, SampleArray(input.nextUv, constants[id].meshImageIndex[0]), spriteBlend);
    if (sampledBase.a * input.alphaFactor < constants[id].alphaCut)
        discard;

    const uint e0 = GetAttributeAtVertex(input.visEligible, 0);
    const uint e1 = GetAttributeAtVertex(input.visEligible, 1);
    const uint e2 = GetAttributeAtVertex(input.visEligible, 2);
    if ((e0 & e1 & e2) == 0u)
        return uint4(0u, 0u, 0u, 0u);
    return uint4(id + 1u, GetAttributeAtVertex(input.vertexId, 0), GetAttributeAtVertex(input.vertexId, 1),
                 GetAttributeAtVertex(input.vertexId, 2));
}
```
  Prototype result (2026-10-13): DXC compiles this to SPIR-V (`SPV_KHR_fragment_shader_barycentric`, `PerVertexKHR`) and to DXIL `ps_6_6`.
- [ ] **Step 5: DepthPass.**
  - `DepthPass.h`:
    - add `struct BlendState;` to the forward declarations;
    - after the class, still inside `namespace pe`, add:
```cpp
    // The visibility target is RGBA32_UINT: integer attachments overwrite, never blend. write = false keeps the
    // attachment bound without touching it (the G-buffer's standard pipelines; the prepass already wrote it).
    BlendState VisibilityBlendState(bool write);
```
    - add the member `Image *m_visibility = nullptr;` after `Image *m_depthStencil;`.
  - `DepthPass.cpp`: add `#include "Render/SceneRenderTargets.h"`, then replace `Init`, `UpdatePassInfo`, `Resize`, and the empty-scene branch, and extend `Update`:
```cpp
    BlendState VisibilityBlendState(bool write)
    {
        return BlendState(false, PE_BLEND_FACTOR_ONE, PE_BLEND_FACTOR_ZERO, PE_BLEND_OP_ADD, PE_BLEND_FACTOR_ONE,
                          PE_BLEND_FACTOR_ZERO, PE_BLEND_OP_ADD,
                          write ? PE_COLOR_COMPONENT_RGBA : static_cast<PeColorComponentFlags>(0));
    }

    void DepthPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");
        m_visibility = SceneUsesOpenPBR() ? rs->GetRenderTarget("visibility") : nullptr;

        // Colour attachments first, depth last (BuildGBufferAttachments' order). Off: exactly the depth attachment.
        m_attachments.clear();
        if (m_visibility)
        {
            Attachment visibility{};
            visibility.image = m_visibility;
            visibility.loadOp = PE_LOAD_OP_CLEAR;
            visibility.storeOp = PE_STORE_OP_STORE;
            m_attachments.push_back(visibility);
        }
        Attachment depth{};
        depth.image = m_depthStencil;
        depth.loadOp = PE_LOAD_OP_CLEAR;
        depth.storeOp = PE_STORE_OP_STORE;
        m_attachments.push_back(depth);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
    }

    void DepthPass::UpdatePassInfo()
    {
        const bool visibility = m_visibility != nullptr;
        auto configureDepthPass = [visibility](PassInfo &passInfo, const std::string &name, PeCullMode cullMode)
        {
            const std::vector<Define> defines = visibility ? std::vector<Define>{Define{"VISIBILITY", "1"}} : std::vector<Define>{};
            passInfo.name = name;
            passInfo.pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = defines});
            passInfo.pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + (visibility ? "Shaders/Depth/VisibilityPS.hlsl" : "Shaders/Depth/DepthPS.hlsl"), .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = defines});
            passInfo.dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            passInfo.cullMode = cullMode;
            passInfo.colorFormats = visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT} : std::vector<::PeFormat>{};
            passInfo.colorBlendAttachments = visibility ? std::vector<BlendState>{VisibilityBlendState(true)} : std::vector<BlendState>{};
            passInfo.depthFormat = RHII.GetDepthFormat();
            passInfo.depthTestEnable = true;
            passInfo.depthWriteEnable = true;
            passInfo.Update();
        };

        configureDepthPass(*m_passInfo, "DepthPrePass_pipeline", PE_CULL_MODE_FRONT);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
        configureDepthPass(*m_passInfoDS, "DepthPrePass_DS_pipeline", PE_CULL_MODE_NONE);
    }

    void DepthPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        // A visibility toggle changes the shaders and colour formats: rebuild, as GbufferOpaquePass::Resize does.
        // (ResizeInitializedSceneRenderGraphPassComponents only rebuilds passes that already had colour formats.)
        const std::vector<::PeFormat> wanted = m_visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT}
                                                            : std::vector<::PeFormat>{};
        if (m_passInfo && m_passInfo->colorFormats != wanted)
        {
            for (PassInfo *pi : GetPassInfos())
                if (pi)
                    pi->DestroyShaders();
            UpdatePassInfo();
        }
        UpdateDescriptorSets();
    }
```
  - In `DepthPass::Update`, in the per-frame set-0 block, replace the three lines `setUniforms->SetBuffer(0, …); setUniforms->SetBuffer(1, …); setUniforms->Update();` with:
```cpp
                setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                setUniforms->SetBuffer(1, scene.GetMeshConstants());
                if (setUniforms->HasBinding(2)) // DX12 VISIBILITY variant only: DepthVS's IndirectAll
                    setUniforms->SetBuffer(2, scene.GetIndirectAll());
                setUniforms->Update();
```
  - In `DepthPass::ExecutePass`:
    - replace `cmd->BeginPass(1, m_attachments.data(), "DepthPass");` with `cmd->BeginPass(static_cast<uint32_t>(m_attachments.size()), m_attachments.data(), "DepthPass");`;
    - replace the empty-scene branch `ClearDepthStencil(cmd);` with:
```cpp
            ClearDepthStencil(cmd);
            if (m_visibility) // the resolve must never read last frame's IDs
                cmd->ClearColors({m_visibility});
```
- [ ] **Step 6: DepthLatePass.**
  - `DepthLatePass.h`: add the member `Image *m_visibility = nullptr;` after `Image *m_depthStencil = nullptr;`.
  - `DepthLatePass.cpp`: add `#include "Render/SceneRenderTargets.h"`, then replace `Init`, `UpdatePassInfo` and `Resize` with:
```cpp
    void DepthLatePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");
        m_visibility = SceneUsesOpenPBR() ? rs->GetRenderTarget("visibility") : nullptr;

        // LOAD both: set A's depth and payload from DepthPass@200 are kept; set B adds the nearest of A and B.
        m_attachments.clear();
        if (m_visibility)
        {
            Attachment visibility{};
            visibility.image = m_visibility;
            visibility.loadOp = PE_LOAD_OP_LOAD;
            visibility.storeOp = PE_STORE_OP_STORE;
            m_attachments.push_back(visibility);
        }
        Attachment depth{};
        depth.image = m_depthStencil;
        depth.loadOp = PE_LOAD_OP_LOAD; // preserve set-A depth from DepthPass@200
        depth.storeOp = PE_STORE_OP_STORE;
        m_attachments.push_back(depth);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
    }

    void DepthLatePass::UpdatePassInfo()
    {
        const bool visibility = m_visibility != nullptr;
        auto configureDepthPass = [visibility](PassInfo &passInfo, const std::string &name, PeCullMode cullMode)
        {
            const std::vector<Define> defines = visibility ? std::vector<Define>{Define{"VISIBILITY", "1"}} : std::vector<Define>{};
            passInfo.name = name;
            passInfo.pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = defines});
            passInfo.pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + (visibility ? "Shaders/Depth/VisibilityPS.hlsl" : "Shaders/Depth/DepthPS.hlsl"), .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = defines});
            passInfo.dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            passInfo.cullMode = cullMode;
            passInfo.colorFormats = visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT} : std::vector<::PeFormat>{};
            passInfo.colorBlendAttachments = visibility ? std::vector<BlendState>{VisibilityBlendState(true)} : std::vector<BlendState>{};
            passInfo.depthFormat = RHII.GetDepthFormat();
            passInfo.depthTestEnable = true;
            passInfo.depthWriteEnable = true;
            passInfo.Update();
        };

        configureDepthPass(*m_passInfo, "DepthLatePass_pipeline", PE_CULL_MODE_FRONT);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
        configureDepthPass(*m_passInfoDS, "DepthLatePass_DS_pipeline", PE_CULL_MODE_NONE);
    }

    void DepthLatePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        const std::vector<::PeFormat> wanted = m_visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT}
                                                            : std::vector<::PeFormat>{};
        if (m_passInfo && m_passInfo->colorFormats != wanted)
        {
            for (PassInfo *pi : GetPassInfos())
                if (pi)
                    pi->DestroyShaders();
            UpdatePassInfo();
        }
        UpdateDescriptorSets();
    }
```
  - In `DepthLatePass::Update`, in the per-frame set-0 block, add the same lines as DepthPass, between `setUniforms->SetBuffer(1, scene.GetMeshConstants());` and `setUniforms->Update();`:
```cpp
                if (setUniforms->HasBinding(2)) // DX12 VISIBILITY variant only: DepthVS's IndirectAll
                    setUniforms->SetBuffer(2, scene.GetIndirectAll());
```
  - In `DepthLatePass::ExecutePass`, replace `cmd->BeginPass(1, m_attachments.data(), "DepthLatePass");` with `cmd->BeginPass(static_cast<uint32_t>(m_attachments.size()), m_attachments.data(), "DepthLatePass");`.
  - The empty-scene early return stays as is: DepthPass already cleared both targets.
- [ ] **Step 7: G-buffer pass.** Apply Appendix A.4 to `GbufferPass.{h,cpp}`. That covers `ApplyGBufferSurface` with the visibility attachment, the call sites, the attachment builders, the formats, and `m_visibilityRT`. Then, in `VoxelGBufferPS.hlsl` and `TerrainGBufferPS.hlsl`, after `output.transparency = …;`:
```hlsl
#ifdef GBUFFER_VISIBILITY
    output.visibility = uint4(0, 0, 0, 0); // drawn after the prepass: clear any payload this surface covers
#endif
```
- [ ] **Step 8:** Build. Run `cooked-clusters-*`, `linear-color-*`, `depth-equal-positions`, `renderer-features-shaders` and `openpbr_vis.py` on both APIs. Expected: all pass, with no validation messages. Read the log, then run `clang-format`.

### Task 6: Quad-exact compute debug view (Slang routing + bindless + derivative gate)

**Files:**
- `Phasma/Core/Code/API/Shader.{h,cpp}`, `ShaderCache.cpp`, `Vulkan/VulkanShaderImpl.cpp`, `DX12/Dx12ShaderImpl.cpp` (Step 0)
- `Shaders/MaterialX/VisibilityCommon.slang`, `Shaders/MaterialX/VisibilityDebug.slang` (new)
- `Phasma/Runtime/Code/RenderPasses/VisibilityDebugPass.{h,cpp}` (new)
- `Phasma/Runtime/Code/Render/SceneRenderGraph.{h,cpp}`

**Interfaces (produced):**
- `static bool Shader::UsesSlang(const std::string &sourcePath)`: true when the global compiler is Slang **or** `sourcePath` ends in `.slang`.
- `VisibilityCommon.slang`:
  - `PE_EDITOR_FLAG_MATERIALX = 32u` and `PE_OPENPBR_INPUTS_OFFSET = 80u`;
  - `struct PeSurfacePoint`;
  - `PeLoadTriangle(geometry, frameData, constants, verticesOffset, vis)`;
  - `PeSurfaceAt(tri, pixelCentre, size)`: binding-free, each pass passes in its resources.
- Pass ID `SceneRenderGraphPassId::VisibilityDebug`, order 305, enabled by `SceneUsesOpenPBRDebug()`.
- `visibility_debug` texel codes (exact bytes, decoded by the test):

  | Channel | Meaning |
  |---|---|
  | `R` | Depth error code. 255 when the error is exactly 0; otherwise `clamp(floor(-log10(err) * 20), 0, 254)`. Any error above 1e-5 codes ≤ 99. Decode as an upper bound: `err ≤ 10^(-R/20)`. |
  | `G` | `min(255, abs(round(reconstructed × 255) − round(G-buffer × 255)))`, maximum over channels: an exact integer difference of the 8-bit values |
  | `B` | 255 on visibility pixels, else 0 |
  | `A` | 255 when the reconstructed depth and colour are finite, else 0 |

- [ ] **Step 0: Per-source Slang routing.** Without it, `.slang` sources reach DXC unless the global compiler is Slang.
  - `Shader.h`, after `static ShaderCompiler GetCompiler();`:
```cpp
        // .slang sources always compile with slangc (Slang syntax); everything else follows GetCompiler().
        static bool UsesSlang(const std::string &sourcePath);
```
  - `Shader.cpp`, after `Shader::GetCompiler()`:
```cpp
    bool Shader::UsesSlang(const std::string &sourcePath)
    {
        return GetCompiler() == ShaderCompiler::Slang || std::filesystem::path(sourcePath).extension() == ".slang";
    }
```
  - Replace `Shader::GetCompiler() == ShaderCompiler::Slang` at the three sites:
    - `ShaderCache.cpp:105`, inside `ShaderCache::Init`: `Shader::UsesSlang(m_sourcePath)`. This keys the cache per compiler.
    - `Vulkan/VulkanShaderImpl.cpp:249`: `Shader::UsesSlang(owner->GetCache().GetSourcePath())`.
    - `DX12/Dx12ShaderImpl.cpp:592`: the same call.
  - Existing `.hlsl` sources behave exactly as before. Step 5 is the test: `VisibilityDebug.slang` must compile through `Shader::Create` with the default (DXC) compiler setting, on both APIs.

- [ ] **Step 1: Failing test.** Checks take no arguments; `main` sets `API` and runs a second editor session on saved Sponza. Add to `openpbr_vis.py`:
```python
def debug_codes():
    r = tool("capture_image_resource", {"id": "rt:visibility_debug", "max_width": 16384}); assert not r.get("error"), r
    a = np.asarray(Image.open(r["path"]).convert("RGBA"), np.int32)
    vis = a[..., 2] == 255
    return a, vis


def assert_reconstruction(label):
    a, vis = debug_codes()
    assert vis.mean() > 0.05, f"{label}: too few visibility pixels ({vis.mean():.3f})"
    assert (a[..., 3][vis] == 255).all(), f"{label}: non-finite reconstruction"
    worst_depth = a[..., 0][vis].min()
    assert worst_depth >= 100, f"{label}: reconstructed depth off by up to 1e-{worst_depth / 20:.2f}"
    g = a[..., 1][vis]
    # Analytic barycentrics cannot reproduce the hardware interpolator and 16x anisotropic probe selection bit for
    # bit: ~1e-4 of pixels differ by 2-6/255. Reconstruction bugs (offsets, wrong vertices or mips) move far more.
    assert np.mean(g > 1) <= 1e-3, f"{label}: {np.mean(g > 1):.2e} of pixels off by more than 1/255 (max {g.max()})"
    assert g.max() <= 16, f"{label}: quad-exact albedo off by {g.max()}/255"
    return a, vis


def check_reconstruction_sponza():  # runs in main's Sponza session
    for lod, clusters, occlusion in ((False, False, False), (True, False, True), (True, True, True), (False, True, False)):
        lua(f'settings.set("lod_enabled",{str(lod).lower()}) settings.set("cluster_geometry",{str(clusters).lower()}) '
            f'settings.set("occlusion_culling",{str(occlusion).lower()}) settings.set("openpbr",true) settings.set("openpbr_debug",true) '
            'settings.set("taa",false) return "ok"'); time.sleep(6)
        assert_reconstruction(f"sponza lod={lod} clusters={clusters} occlusion={occlusion}")


def check_reconstruction_occluders():
    # A rigid cube half-covered by voxel ground: voxels draw after the prepass and must clear the payload they cover.
    new_scene()
    save_dir = str(ASSETS / "VoxelSave").replace(chr(92), "/")
    lua('settings.set("taa",false) local c=scene.add_empty_node("Cube") scene.attach_primitive(c,"cube") '
        'c:set_position(vec3(0,0.25,0)) c:set_scale(vec3(2,2,2)) '
        'voxel.create({world_radius=2, load_radius=2, streaming=false, noise_amplitude=0, caves=false, ground_y=0, '
        'save_dir="' + save_dir + '"}) scene.add_directional_light() '
        'settings.set("openpbr",true) settings.set("openpbr_debug",true) return "ok"')
    tool("set_camera", {"position": [0, 1, 6], "target": [0, 0.5, 0], "projection": "perspective"}); time.sleep(8)
    a, vis = assert_reconstruction("cube over voxel ground")
    h = a.shape[0]
    assert not vis[int(h * 0.85):, :].any(), "voxel ground (drawn after the prepass) kept a visibility payload"
    lua('voxel.destroy() settings.set("openpbr",false) return "ok"')
```
  - Append `check_reconstruction_sponza` to `SPONZA_CHECKS` and `check_reconstruction_occluders` to `CHECKS`.
  - If the `voxel.create` keys differ (read `Phasma/Runtime/Code/Script/Bindings/Voxel/VoxelBindings.cpp`), adjust the keys, not the assertions.
  - **Not covered by a test here:**
    - Terrain has no Lua creation API, so it is review-covered; it uses the same `ApplyGBufferSurface` call site as voxels.
    - Skinned meshes and blending sprites write 0 in the prepass. They are review-covered until Phase 4.

- [ ] **Step 2: `Shaders/MaterialX/VisibilityCommon.slang`** (new; prototype-compiled):
```slang
// Triangle reconstruction for the visibility-buffer resolve (debug view, material pass 1 and 2).
// Binding-free: each pass declares its resources and passes them in, so one file serves every set layout.
// Requires ../Common/Structures.hlsl (Mesh_Constants) before it.
#ifndef PE_VISIBILITY_COMMON
#define PE_VISIBILITY_COMMON

static const uint PE_EDITOR_FLAG_MATERIALX = 32u; // Mesh_Constants.editorFlags: the mesh has a MaterialX material
// A MaterialX material's byte range: the legacy MaterialGpuData record (80 bytes: raster G-buffer, RT hits and the
// off path read it), then the generated module's inputs (pe_load_material).
static const uint PE_OPENPBR_INPUTS_OFFSET = 80u;

struct PeSurfacePoint
{
    float4 svPosition; // pixel centre xy, NDC depth, 1/w (what SV_Position holds in a pixel shader)
    float3 P;
    float3 N;
    float3 T;
    float3 B;
    float2 uv0;
    float4 color0;
};

struct PeVertex
{
    float3 position;
    float2 uv;
    float3 normal;
    float4 tangent;
    float4 color;
};

struct PeTriangle
{
    PeVertex v0;
    PeVertex v1;
    PeVertex v2;
    float4 c0; // clip positions, DepthVS's maths (no viewport Y flip)
    float4 c1;
    float4 c2;
    float4x4 world;
};

float4x4 PeLoadMatrix(ByteAddressBuffer data, uint offset)
{
    float4x4 m;
    m[0] = asfloat(data.Load4(offset + 0));
    m[1] = asfloat(data.Load4(offset + 16));
    m[2] = asfloat(data.Load4(offset + 32));
    m[3] = asfloat(data.Load4(offset + 48));
    return m;
}

// Scene vertex store: pe::Vertex, 96 bytes (position 0, uv 12, normals 20, tangent 32, color 48, joints 64, weights 80).
PeVertex PeLoadVertex(ByteAddressBuffer geometry, uint verticesOffset, uint index)
{
    const uint o = verticesOffset + index * 96u;
    PeVertex v;
    v.position = asfloat(geometry.Load3(o + 0u));
    v.uv = asfloat(geometry.Load2(o + 12u));
    v.normal = asfloat(geometry.Load3(o + 20u));
    v.tangent = asfloat(geometry.Load4(o + 32u));
    v.color = asfloat(geometry.Load4(o + 48u));
    return v;
}

// vis = uint4(drawId + 1, v0, v1, v2); frame data: viewProjection at byte 0 (DepthVS's GetViewProjection).
PeTriangle PeLoadTriangle(ByteAddressBuffer geometry, ByteAddressBuffer frameData, StructuredBuffer<Mesh_Constants> constants,
                          uint verticesOffset, uint4 vis)
{
    PeTriangle t;
    const uint id = vis.x - 1u;
    t.world = PeLoadMatrix(frameData, constants[id].meshDataOffset);
    const float4x4 vp = PeLoadMatrix(frameData, 0u);
    t.v0 = PeLoadVertex(geometry, verticesOffset, vis.y);
    t.v1 = PeLoadVertex(geometry, verticesOffset, vis.z);
    t.v2 = PeLoadVertex(geometry, verticesOffset, vis.w);
    const float4x4 final = mul(t.world, vp); // DepthVS: mul(float4(p, 1), mul(world, viewProjection)), precise
    precise float4 c0 = mul(float4(t.v0.position, 1.0), final);
    precise float4 c1 = mul(float4(t.v1.position, 1.0), final);
    precise float4 c2 = mul(float4(t.v2.position, 1.0), final);
    t.c0 = c0;
    t.c1 = c1;
    t.c2 = c2;
    return t;
}

// Framebuffer pixel centre -> unflipped NDC. Vulkan rasterises NDC y = -1 at the top row; DX12 flips clip y in
// ApplyViewportYConvention and rasterises +1 at the top, so both map row y to (y / height) * 2 - 1 here.
float2 PeNdcFromPixel(float2 pixelCentre, float2 size)
{
    return pixelCentre / size * 2.0 - 1.0;
}

float3 PeSafeNormalize(float3 v)
{
    const float len2 = dot(v, v);
    return len2 > 1e-12 ? v * rsqrt(len2) : float3(0.0, 0.0, 0.0);
}

float PeCross2(float2 a, float2 b)
{
    return a.x * b.y - a.y * b.x;
}

// Screen-space barycentrics of ndc in the projected triangle (DAIS, Schied & Dachsbacher 2015, appendix):
// linear in screen space, so NDC depth interpolates with them directly; attributes use PePerspective().
float3 PeScreenBarycentrics(float4 c0, float4 c1, float4 c2, float2 ndc)
{
    const float2 n0 = c0.xy / c0.w;
    const float2 n1 = c1.xy / c1.w;
    const float2 n2 = c2.xy / c2.w;
    const float area = PeCross2(n1 - n0, n2 - n0);
    const float l1 = PeCross2(ndc - n0, n2 - n0) / area;
    const float l2 = PeCross2(n1 - n0, ndc - n0) / area;
    return float3(1.0 - l1 - l2, l1, l2);
}

// Perspective-correct weights for vertex attributes from screen-space barycentrics.
float3 PePerspective(float3 l, float4 c0, float4 c1, float4 c2)
{
    const float3 q = l / float3(c0.w, c1.w, c2.w);
    return q / (q.x + q.y + q.z);
}

float PeNdcDepth(float3 l, float4 c0, float4 c1, float4 c2)
{
    return l.x * (c0.z / c0.w) + l.y * (c1.z / c1.w) + l.z * (c2.z / c2.w);
}

// The surface a pixel shader rasterising tri would see at ndc: GBufferVS's world transforms (remove_scale3x3 for
// normals and tangents), perspective-correct attributes, SV_Position = (pixel centre, NDC depth, 1/w).
PeSurfacePoint PeSurfaceAt(PeTriangle t, float2 pixelCentre, float2 size)
{
    const float3 l = PeScreenBarycentrics(t.c0, t.c1, t.c2, PeNdcFromPixel(pixelCentre, size));
    const float3 b = PePerspective(l, t.c0, t.c1, t.c2);
    const float3x3 rot = float3x3(normalize(t.world[0].xyz), normalize(t.world[1].xyz), normalize(t.world[2].xyz));
    PeSurfacePoint sp;
    const float3 pos = t.v0.position * b.x + t.v1.position * b.y + t.v2.position * b.z;
    sp.P = mul(float4(pos, 1.0), t.world).xyz;
    // Per vertex, as GBufferVS does: transform and normalise, then interpolate, then normalise (GBufferPS).
    const float3 n0 = normalize(mul(t.v0.normal, rot)), n1 = normalize(mul(t.v1.normal, rot)), n2 = normalize(mul(t.v2.normal, rot));
    sp.N = normalize(n0 * b.x + n1 * b.y + n2 * b.z);
    // Meshes without tangents (PE primitives store zeros) get the triangle's UV tangent dP/du, as MaterialX's own
    // mesh loader generates them; normalising a zero tangent would feed NaN to MaterialX's tangent frame.
    const float3 t0 = PeSafeNormalize(mul(t.v0.tangent.xyz, rot)), t1 = PeSafeNormalize(mul(t.v1.tangent.xyz, rot)), t2 = PeSafeNormalize(mul(t.v2.tangent.xyz, rot));
    float3 T = t0 * b.x + t1 * b.y + t2 * b.z;
    float sign = t.v0.tangent.w * b.x + t.v1.tangent.w * b.y + t.v2.tangent.w * b.z;
    if (dot(T, T) < 1e-8)
    {
        const float3 e1 = mul(t.v1.position - t.v0.position, (float3x3)t.world);
        const float3 e2 = mul(t.v2.position - t.v0.position, (float3x3)t.world);
        const float2 d1 = t.v1.uv - t.v0.uv;
        const float2 d2 = t.v2.uv - t.v0.uv;
        const float det = d1.x * d2.y - d2.x * d1.y;
        T = (e1 * d2.y - e2 * d1.y) * (det < 0.0 ? -1.0 : 1.0);
        sign = det < 0.0 ? -1.0 : 1.0;
    }
    T -= sp.N * dot(sp.N, T);
    if (dot(T, T) < 1e-12) // no usable UVs either: any tangent perpendicular to N
        T = abs(sp.N.x) < 0.9 ? cross(sp.N, float3(1.0, 0.0, 0.0)) : cross(sp.N, float3(0.0, 1.0, 0.0));
    sp.T = normalize(T);
    sp.B = cross(sp.N, sp.T) * (sign < 0.0 ? -1.0 : 1.0);
    sp.uv0 = t.v0.uv * b.x + t.v1.uv * b.y + t.v2.uv * b.z;
    sp.color0 = t.v0.color * b.x + t.v1.color * b.y + t.v2.color * b.z;
    const float w = 1.0 / (l.x / t.c0.w + l.y / t.c1.w + l.z / t.c2.w);
    sp.svPosition = float4(pixelCentre, PeNdcDepth(l, t.c0, t.c1, t.c2), 1.0 / w);
    return sp;
}

#endif
```
- [ ] **Step 3: `Shaders/MaterialX/VisibilityDebug.slang`** (new; prototype-compiled):
```slang
// openpbr_debug: quad-exact reconstruction of the raster G-buffer from the visibility payload. Each output pixel
// is evaluated by a 2x2 quad whose four lanes all use the pixel's own triangle at their own pixel centres
// (extrapolated barycentrics, like hardware helper lanes), so ddx/ddy and implicit-LOD Sample() are equivalent
// to a pixel shader rasterising that triangle. Output codes are decoded by .testpool/tests/openpbr_vis.py.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "../Common/MaterialFlags.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_VisibilityDebug
{
    uint verticesOffset;
    uint linearColor;
};

[[vk::push_constant]] PushConstants_VisibilityDebug pc;
[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] Texture2D<float> pe_depth;
[[vk::binding(2, 0)]] Texture2D<float4> pe_albedo;
[[vk::binding(3, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(5, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(6, 0)]] StructuredBuffer<MaterialGpuData> pe_materialTable;
[[vk::binding(7, 0)]] ByteAddressBuffer pe_materialBytes;
[[vk::image_format("rgba8")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_debug;
[[vk::binding(0, 1)]] SamplerState pe_materialSampler;
[[vk::binding(1, 1)]] Texture2D pe_textures[];

// GBufferPS.hlsl's material record (byte path first, then the structured table).
MaterialGpuData PeMaterial(uint id)
{
    const uint byteOff = pe_constants[id].materialByteOffset;
    if (byteOff == 0xFFFFFFFF)
        return pe_materialTable[pe_constants[id].materialId];
    MaterialGpuData m;
    m.baseColorFactor = asfloat(pe_materialBytes.Load4(byteOff));
    m.emissiveTransmission = asfloat(pe_materialBytes.Load4(byteOff + 16));
    m.pbrParams = asfloat(pe_materialBytes.Load4(byteOff + 32));
    m.transmissionVolume = asfloat(pe_materialBytes.Load4(byteOff + 48));
    m.attenuationColor = asfloat(pe_materialBytes.Load4(byteOff + 64));
    return m;
}

// GBufferPS.hlsl's albedo output: base texture x vertex colour x factor, sRGB-encoded under linear_color.
float3 PeGBufferAlbedo(uint id, float2 uv, float4 vertexColor)
{
    const MaterialGpuData mat = PeMaterial(id);
    const uint index = uint(pe_constants[id].meshImageIndex[0]);
    float4 sampled = float4(1.0, 1.0, 1.0, 1.0);
    if (HasTexture(pe_constants[id].textureMask, TEX_BASE_COLOR_BIT) && index != 0xFFFFFFFF)
        sampled = pe_textures[NonUniformResourceIndex(index)].Sample(pe_materialSampler, uv);
    float4 authored = vertexColor * mat.baseColorFactor;
    if (pc.linearColor != 0)
        authored.rgb = SrgbToLinear(vertexColor.rgb) * SrgbToLinear(mat.baseColorFactor.rgb);
    const float4 combined = sampled * authored;
    return pc.linearColor != 0 ? LinearToSrgb(combined.rgb) : combined.rgb;
}

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy >> 1;           // 4 threads per output pixel (debug view: simple, 4x cost)
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane; // where this lane evaluates: its own position in pixel's 2x2 quad
    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    const bool inside = all(pixel < size);
    const uint4 vis = inside ? pe_visibility[pixel] : uint4(0, 0, 0, 0);
    const bool owner = all(lane == (pixel & 1u)); // the lane that sits on the output pixel
    // Quad-uniform control flow: the four lanes of a quad share pixel, so they share vis and exit together.
    if (vis.x == 0u)
    {
        if (inside && owner)
            pe_debug[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_constants, pc.verticesOffset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));
    const float3 albedo = PeGBufferAlbedo(vis.x - 1u, sp.uv0, sp.color0);
    if (!owner)
        return;
    const float3 mine = round(saturate(albedo) * 255.0);
    const float3 raster = round(pe_albedo[pixel].rgb * 255.0);
    const float depthErr = abs(sp.svPosition.z - pe_depth[pixel]);
    const float albedoErr = max(max(abs(mine.r - raster.r), abs(mine.g - raster.g)), abs(mine.b - raster.b));
    const float depthCode = depthErr == 0.0 ? 255.0 : clamp(floor(-log10(depthErr) * 20.0), 0.0, 254.0);
    const bool finite = !isnan(sp.svPosition.z) && !isinf(sp.svPosition.z) && !any(isnan(albedo)) && !any(isinf(albedo));
    pe_debug[pixel] = float4(depthCode, min(255.0, albedoErr), 255.0, finite ? 255.0 : 0.0) / 255.0;
}
```
- [ ] **Step 4: `VisibilityDebugPass`.** `Phasma/Runtime/Code/RenderPasses/VisibilityDebugPass.h`:
```cpp
#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    // openpbr_debug (desktop OpenPBR): reconstructs the G-buffer's albedo and depth from the visibility payload with
    // quad-exact compute evaluation and writes per-pixel error codes to visibility_debug (decoded by pool tests).
    class VisibilityDebugPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override { m_lastGeometryVersion = ~0ull; }
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_visibility = nullptr;
        Image *m_depth = nullptr;
        Image *m_albedo = nullptr;
        Image *m_debug = nullptr;
        uint64_t m_lastGeometryVersion = ~0ull;
        bool m_bound = false; // this frame's descriptors were written: ExecutePass may dispatch
    };
} // namespace pe
```
  `Phasma/Runtime/Code/RenderPasses/VisibilityDebugPass.cpp`:
```cpp
#include "VisibilityDebugPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct PushConstants_VisibilityDebug
        {
            uint32_t verticesOffset;
            uint32_t linearColor;
        };
    } // namespace

    void VisibilityDebugPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_visibility = rs->GetRenderTarget("visibility");
        m_depth = rs->GetDepthStencilTarget("depthStencil");
        m_albedo = rs->GetRenderTarget("albedo");
        m_debug = rs->GetRenderTarget("visibility_debug");
        m_lastGeometryVersion = ~0ull;
    }

    void VisibilityDebugPass::UpdatePassInfo()
    {
        m_passInfo->name = "VisibilityDebug_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/VisibilityDebug.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->Update();
    }

    void VisibilityDebugPass::Update()
    {
        m_bound = false;
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_depth || !m_albedo || !m_debug || !scene->GetBuffer() || !scene->GetMaterialTable() ||
            !scene->GetMaterialByteBuffer())
            return;

        // The bindless table changes with the geometry version, as DepthPass rebinds it.
        if (scene->GetGeometryVersion() != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = scene->GetGeometryVersion();
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
            {
                Descriptor *textures = m_passInfo->GetDescriptors(i)[1];
                textures->SetSampler(0, scene->GetDefaultSampler());
                textures->SetImageViews(1, scene->GetImageViews());
                textures->Update();
            }
        }

        const uint32_t frame = RHII.GetFrameIndex();
        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];
        set->SetImageView(0, m_visibility->GetSRV());
        set->SetImageView(1, m_depth->GetSRV());
        set->SetImageView(2, m_albedo->GetSRV());
        set->SetBuffer(3, scene->GetBuffer());
        set->SetBuffer(4, scene->GetUniforms(frame));
        set->SetBuffer(5, scene->GetMeshConstants());
        set->SetBuffer(6, scene->GetMaterialTable());
        set->SetBuffer(7, scene->GetMaterialByteBuffer());
        set->SetImageView(8, m_debug->GetUAV(0));
        set->Update();
        m_bound = true;
    }

    void VisibilityDebugPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility || !m_debug)
            return;
        builder.ReadCompute(m_visibility);
        builder.ReadCompute(m_depth);
        builder.ReadCompute(m_albedo);
        builder.WriteCompute(m_debug);
    }

    void VisibilityDebugPass::DeclareOutputs(RGBuilder &builder)
    {
        if (m_debug)
            builder.OutputCustom(m_debug, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void VisibilityDebugPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_debug || !m_bound)
            return;

        PushConstants_VisibilityDebug pc{};
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());
        pc.linearColor = SceneUsesLinearColor() ? 1u : 0u;

        cmd->BeginDebugRegion("VisibilityDebugPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        // Four threads per pixel (one 2x2 quad), 8x8 threads per group.
        cmd->Dispatch((m_debug->GetWidth() * 2u + 7u) / 8u, (m_debug->GetHeight() * 2u + 7u) / 8u, 1);
        cmd->EndDebugRegion();
    }

    void VisibilityDebugPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();
    }

    void VisibilityDebugPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        m_visibility = m_depth = m_albedo = m_debug = nullptr;
    }
} // namespace pe
```
  Every resource is bound to the frame being recorded, every frame. The two buffers that `Scene` reallocates (`GetBuffer`, `GetMaterialByteBuffer`) can therefore never be stale.
- [ ] **Step 4b: Register the pass** in `SceneRenderGraph.{h,cpp}`. There are 8 places; write each exactly:
  1. `SceneRenderGraph.h`, enum `SceneRenderGraphPassId`, after `Reflections, // …`: `VisibilityDebug, // openpbr_debug: quad-exact reconstruction check of the visibility payload`. `RendererSystem.h:56`'s GUI id shifts with `Count`; that is expected.
  2. `SceneRenderGraph.h`, struct `SceneRenderGraphPassComponents`, after `IRenderPassComponent *selectionOutline = nullptr;`: `IRenderPassComponent *visibilityDebug = nullptr;`.
  3. `SceneRenderGraph.cpp`:
     - add `#include "RenderPasses/VisibilityDebugPass.h"` after `#include "RenderPasses/UpsamplePass.h"`;
     - in `kSceneRenderGraphPasses`, after the `SSAO` row: `{SceneRenderGraphPassId::VisibilityDebug, 305, "VisibilityDebug", &SceneRenderGraphPassComponents::visibilityDebug},`.
  4. In `ShouldUpdateSceneRenderGraphPass`, before the final `return true;`:
```cpp
            if (component == components.visibilityDebug)
                return isPassEnabled(SceneRenderGraphPassId::VisibilityDebug);
```
  5. In `CreateSceneRenderGraphPassComponents`, after `CreateSceneRenderGraphPassComponent<SelectionOutlinePass>(renderPassComponents);` (outside the ray-tracing `if`): `CreateSceneRenderGraphPassComponent<VisibilityDebugPass>(renderPassComponents);`.
  6. In `GetGlobalSceneRenderGraphPassComponents`, before `return scenePasses;`: `scenePasses.visibilityDebug = GetGlobalComponent<VisibilityDebugPass>();`.
  7. In `UpdateSceneRenderGraphPassStates`:
     - in the DX12 branch, before its `return;`: `SetPassEnabled(passEnabled, SceneRenderGraphPassId::VisibilityDebug, SceneUsesOpenPBRDebug() && dx12RenderRaster);`;
     - at the end of the Vulkan path: `SetPassEnabled(passEnabled, SceneRenderGraphPassId::VisibilityDebug, SceneUsesOpenPBRDebug() && renderRaster);`.
  8. `SetSceneRenderGraphPassScene`: nothing (the pass has no `SetScene`).
- [ ] **Step 5: Build, run, pass on both APIs.** Expected: depth code ≥ 100 (error ≤ 1e-5), at most 0.1% of visibility pixels with albedo error > 1/255 and none above 16/255, for all four LOD/cluster/occlusion combinations, plus the voxel-occluder case.
  - **Depth fails on DX12 only:** the base-vertex add is wrong (Task 5 Step 3).
  - **Depth fails on both APIs:** check the NDC/viewport convention (`PeNdcFromPixel`) first, then the index-parallel stores (`SceneBuffers.cpp:215-259`).
  - **Albedo fails, depth passes:** the derivative path. Compare a `SampleLevel(…, 0)` variant locally to separate LOD from UV error.
  - If it still fails after one round of diagnosis: **STOP** and report the measurements.
  - Amended 2026-10-13: the albedo gate was `max ≤ 1/255`. The orchestrator measured Sponza with the shipped sampler (16× anisotropy):

    | API | Combinations | Coverage | Min depth code | Pixels > 1/255 | Max |
    |---|---|---:|---:|---:|---:|
    | Vulkan | all four | 92.5% | 110 | 1.0–1.4e-4 | 6/255 |
    | DX12 | all four | 92.5% | 113–117 | 0.97–1.5e-4 | 5/255 |

    About 10% of pixels differ by exactly 1/255. Control run with anisotropy off on the default sampler (Vulkan, combination 1, local only): 2 pixels at 2/255, nothing higher. The residual is hardware filtering that analytic barycentrics cannot reproduce bit for bit, not a reconstruction error.
- [ ] **Step 5b: Prove the gate can fail.** This is local and never kept.
  1. Temporarily inject, one at a time:
     - (a) depth + 1.1e-5, just over the threshold, so the code must drop to ≤ 99;
     - (b) reconstructed albedo + 2/255, so G = 2 (the fraction assertion must fail);
     - (c) a half-texel UV offset (the fraction assertion must fail);
     - (d) on DX12, dropping the base-vertex add.
  2. Run the check after each one. Expect a failure every time, (a) and (b) included: that proves the boundaries.
  3. Revert.
  4. Record both runs (failing and passing) in the task report.

  No test-only code stays in the repo.
- [ ] **Step 6:** Run `clang-format`, then register the `openpbr-vis` tests.

### Task 7: Perf, Android rebake, wiki (Phase 1 close)

- [ ] **Step 1: Perf, off vs baseline** (`perf/openpbr/off/`). Follow Task 0 Step 3 exactly: the editor check, then `tools/perf_cycle.sh Release C:/Users/Christos/repos/PhasmaEngine/build-ninja-physics`, then PID-matched cleanup. Expected: within the thresholds.
- [ ] **Step 2: Perf with `openpbr` on**, debug view off (`perf/openpbr/vis/`). Report the prepass cost of the extra RGBA32_UINT target. Not gated.
- [ ] **Step 3: Android rebake.** Run `powershell -File tools/bake_android_shaders.ps1 -BuildDir build-ninja-physics`. The script defaults to `build-ninja-full`; read that build directory's `PhasmaEngine.log` afterwards. `DepthVS` changed only under `#ifdef VISIBILITY`, so the default variant must bake unchanged. If the bake picks up Slang files, exclude `Shaders/MaterialX/**`: Android never builds them.
- [ ] **Step 4: Wiki.** Update "OpenPBR Materials" in `docs/wiki/architecture/rendering.md` with:
  - the visibility payload and its caps gate;
  - the base-vertex rule;
  - the quad-exact evaluation and the UV-tangent fallback;
  - the override layout (`pe_overrides/libraries/…`);
  - the measured costs and the Phase 0 gate results.

  Run `bash docs/wiki/tools/lint.sh`.
- [ ] **Step 5: Final log review** on both APIs. Leave everything unstaged.

---

## Phase 2 — material passes for the default OpenPBR material

**Scope.** Rigid opaque meshes whose material instance has `use_openpbr` on are shaded by PE's **default OpenPBR module**: all 37 constant inputs of `open_pbr_surface` (MaterialX 1.39.5), generated by PhasmaCook at build time.
- Pass 1 writes the thin G-buffer.
- Pass 2 writes MaterialX's colour under PE's lights, CSM shadows, probe and screen GI, Lumen reflections, IBL and fog.

**Out of scope until later phases** (spec §Phases):
- textures and connected inputs (Phase 3);
- persistence: `use_openpbr`/`set_openpbr` are not saved with the scene (Phase 3);
- skinned meshes and sprites (Phase 4);
- RT hits shade with the mirrored legacy values (Phase 5);
- transparency (Phase 6);
- binning and skipping legacy work (Phase 7).

**Decisions pinned here** (they replace the spec's "Phase 2's first task decides"; Task 13 updates the spec text):
1. **Pass 1 approach.** The generator emits `pe_material_thin()` from the module's own statics when `base_color`, `base_weight`, `specular_roughness`, `base_metalness` and `geometry_normal` are unconnected (Task 2). Pass 1 includes the full module with stub light/environment hooks and calls only the loader and `pe_material_thin`.
2. **What pass 1 writes:**
   - `normal`, `albedo` (`base_color × base_weight`, sRGB-encoded under `linear_color`), `srm` (1, roughness, metalness, 0), `emissive` = (0, 0, 0, 1).
   - `velocity` and `transparency` stay the raster G-buffer's: Phase 2 still rasterises these meshes into the G-buffer, and both values are correct for the same surface.
3. **Material storage reuses the byte buffer.** A MaterialX instance's byte range is the 80-byte legacy `MaterialGpuData` record (read by the raster G-buffer, RT hits and the off path), then the module inputs at `+80`. `Mesh_Constants.materialByteOffset` points at the record. `editorFlags` bit 32 marks the mesh.
4. **Which pixels are shaded.** All rigid meshes keep writing the visibility payload (Task 6's reconstruction test needs coverage). Passes 1 and 2 skip pixels whose mesh lacks bit 32.
5. **Lights.** Pass 2 loops every enabled light, Forward+ tiles unused: their culling only drops lights whose window is already 0. Area lights are their centre point with the point-light window (the spec's representative-point limit).
6. **Environment.**
   - `pe_env_irradiance`: LightingPS's probe/screen-probe term, plus IBL weighted by `1 − weight`.
   - `pe_env_radiance`: ReflectionPass's traced radiance where it has data, for every specular lobe (traced at pass 1's roughness); otherwise the cube at ComputeIBL_Common's mip for that lobe's roughness.
   - SSAO multiplies the environment terms only, as MaterialX's ambient occlusion does.

### Task 8: The default OpenPBR module at build time

**Files:**
- Create `Phasma/Runtime/RuntimeAssets/MaterialX/pe_default_openpbr.mtlx`.
- Modify `Phasma/Cook/CMakeLists.txt` (inside `if(TARGET MaterialXGenSlang)`) and the root `CMakeLists.txt` (the `if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")` runtime-assets block, about lines 591-608).
- Test: `.testpool/tests/mtlx_codegen.py`.

**Interfaces (produced):**
- `<config>/RuntimeAssets/Shaders/MaterialX/Generated/PeOpenPBRDefault.{slang,json}`, regenerated whenever PhasmaCook, the document or an override changes.
- CMake target `PeOpenPBRDefaultModule`; PhasmaEditor and PhasmaPlayer depend on it.
- Prototype values for MaterialX 1.39.5: 37 inputs, `byteSize` 220, types `float`/`float3`/`bool`, `"thin": true`, statics named `pe_openpbr_<input>`.

- [ ] **Step 1: Failing test.** In `mtlx_codegen.py`:
  - add `PE_DEFAULT = ROOT / "Phasma/Runtime/RuntimeAssets/MaterialX/pe_default_openpbr.mtlx"` and `GENERATED = ROOT / "build-ninja-physics/Release/RuntimeAssets/Shaders/MaterialX/Generated"`;
  - in `main`, add `PE_DEFAULT` to the list of documents run through `--mtlx`;
  - after the module loop (still inside the temporary-directory block), add:
```python
        meta = json.loads((out / "PeOpenPBRDefault.json").read_text(encoding="utf-8"))
        assert meta["thin"] and len(meta["inputs"]) == 37 and meta["byteSize"] == 220, (len(meta["inputs"]), meta["byteSize"])
        assert {i["type"] for i in meta["inputs"]} <= {"float", "float3", "bool"}, {i["type"] for i in meta["inputs"]}
        assert all(i["name"] == "pe_openpbr_" + i["input"] for i in meta["inputs"]), "default module statics renamed"
        for ext in (".slang", ".json"):
            built = GENERATED / ("PeOpenPBRDefault" + ext)
            assert built.exists(), f"build did not generate {built}"
            assert built.read_bytes() == (out / ("PeOpenPBRDefault" + ext)).read_bytes(), f"{built} is stale"
```
  Run it. Expected: FAIL (no document).
- [ ] **Step 2: The document** (`pe_default_openpbr.mtlx`):
```xml
<?xml version="1.0"?>
<materialx version="1.39">
  <!-- PhasmaEngine's default OpenPBR material: every input at its OpenPBR default and exposed as a uniform
       (SHADER_INTERFACE_COMPLETE), so every untextured OpenPBR material shares this one generated module. -->
  <open_pbr_surface name="pe_openpbr" type="surfaceshader" />
  <surfacematerial name="PeOpenPBRDefault" type="material">
    <input name="surfaceshader" type="surfaceshader" nodename="pe_openpbr" />
  </surfacematerial>
</materialx>
```
- [ ] **Step 3: Generate at build time.** In `Phasma/Cook/CMakeLists.txt`, at the end of the `if(TARGET MaterialXGenSlang)` block:
```cmake
    # PE's default OpenPBR module (every untextured OpenPBR material shares it), generated into the runtime assets
    # on every change. The overrides are copied first so a changed override regenerates with itself.
    file(GLOB_RECURSE PE_MATERIALX_OVERRIDE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/*")
    set(PE_OPENPBR_GENERATED_DIR "${PHASMA_CONFIG_OUTPUT_DIR}/RuntimeAssets/Shaders/MaterialX/Generated")
    add_custom_command(
        OUTPUT "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.slang" "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides" "$<TARGET_FILE_DIR:PhasmaCook>/MaterialX/pe_overrides"
        COMMAND "$<TARGET_FILE:PhasmaCook>" --mtlx "${CMAKE_SOURCE_DIR}/Phasma/Runtime/RuntimeAssets/MaterialX/pe_default_openpbr.mtlx" "${PE_OPENPBR_GENERATED_DIR}"
        DEPENDS PhasmaCook ${PE_MATERIALX_OVERRIDE_FILES}
        COMMENT "[Phasma] Generating the default OpenPBR module (PhasmaCook --mtlx)"
        VERBATIM)
    add_custom_target(PeOpenPBRDefaultModule ALL DEPENDS "${PE_OPENPBR_GENERATED_DIR}/PeOpenPBRDefault.slang")
    add_dependencies(PeOpenPBRDefaultModule PhasmaRuntimeAssets)
```
  `PHASMA_CONFIG_OUTPUT_DIR` (`cmake/PhasmaRuntimeLayout.cmake:8`) is included before `add_subdirectory(Phasma/Cook)`. `$<CONFIG>` in `OUTPUT` is supported by Ninja Multi-Config (CMake ≥ 3.20). `PhasmaRuntimeAssets` is defined later in the root file; `add_dependencies` resolves it at generate time.
- [ ] **Step 4: Runtime dependency.** In the root `CMakeLists.txt`, after the `foreach(pe_sdl_application IN LISTS PE_SDL_APPLICATION_TARGETS)` block that adds `PhasmaRuntimeAssets` dependencies:
```cmake
    # The editor and player compile PhasmaCook's generated default OpenPBR module from RuntimeAssets.
    if(TARGET PeOpenPBRDefaultModule)
        foreach(pe_openpbr_application PhasmaEditor PhasmaPlayer)
            if(TARGET ${pe_openpbr_application})
                add_dependencies(${pe_openpbr_application} PeOpenPBRDefaultModule)
            endif()
        endforeach()
    endif()
```
  Add `PeOpenPBRDefaultModule` to the build command's target list (Global Constraints).
- [ ] **Step 5:** Build, then run `mtlx_codegen.py`. Expected: `mtlx codegen ok`.

### Task 9: MaterialX material storage and the Lua API

**Files:**
- Create `Phasma/Runtime/Code/Scene/OpenPBRMaterial.{h,cpp}`. Runtime sources are globbed with `CONFIGURE_DEPENDS` (`Phasma/Runtime/CMakeLists.txt:14`), so no CMake edit is needed.
- Modify `Phasma/Runtime/Code/Scene/Material.{h,cpp}` (`MaterialInstance`), `Phasma/Runtime/Code/Scene/SceneBuffers.cpp`, and `Phasma/Runtime/Code/Script/Bindings/Material/MaterialBindings.cpp`.
- Test: `openpbr_vis.py`.

**Interfaces (produced):**
- `MaterialInstance::SetOpenPBR(bool)`, `MaterialInstance::GetOpenPBR()`.
- `pe::GetOpenPBRDefaultLayout()`, `pe::MeshUsesOpenPBR(const Mesh &)`, `pe::BuildOpenPBRBytes(const MaterialInstance &)`, `pe::ResolveOpenPBRInput(const MaterialInstance &, const OpenPBRInput &, float out[3])`, `pe::MirrorOpenPBRToLegacy(MaterialInstance &)`.
- `pe::kOpenPBRInputsOffset = 80` (equals `PE_OPENPBR_INPUTS_OFFSET` in `VisibilityCommon.slang`).
- `Mesh_Constants.editorFlags` bit 32 (`PE_EDITOR_FLAG_MATERIALX`).
- Lua, each with an optional `slot` after the node:
  - `material.use_openpbr(node, enabled)`;
  - `material.set_openpbr(node, input, value)`, where `value` is a number, a `vec3` or a boolean, by the input's type;
  - `material.get_openpbr(node, input)`.

  Unknown inputs and wrong value types raise a Lua error (trust boundary).

- [ ] **Step 1: Failing test.** Add to `openpbr_vis.py` and append to `CHECKS`:
```python
OPENPBR_PLANE = 'local n=scene.add_empty_node("OpenPBRPlane") scene.attach_primitive(n,"plane") material.use_openpbr(n,true) '
GENERATED = ROOT / "build-ninja-physics/Release/RuntimeAssets/Shaders/MaterialX/Generated"


def rt_pixel(name, visualize="linear"):
    r = tool("capture_image_resource", {"id": f"rt:{name}", "max_width": 16384, "visualize": visualize}); assert not r.get("error"), r
    a = np.asarray(Image.open(r["path"]).convert("RGBA"), np.int32)
    h, w = a.shape[:2]
    return a[h // 2 - 2:h // 2 + 3, w // 2 - 2:w // 2 + 3].reshape(-1, 4)  # 5x5 centre window


def srgb_hdr(linear):  # inverse of Common.hlsl SrgbToLinearHdr: what the legacy emissive factor must hold
    peak = max(max(linear), 1.0)
    return [peak * (1.055 * (c / peak) ** (1 / 2.4) - 0.055 if c / peak > 0.0031308 else 12.92 * c / peak) for c in linear]


def check_openpbr_emission():
    # The legacy mirror carries emission (above 1 too), updates on change, and the off path renders it.
    new_scene()
    lua('settings.set("openpbr",false) settings.set("linear_color",true) settings.set("taa",false) settings.set("render_scale",1) ' +
        OPENPBR_PLANE + 'material.set_openpbr(n,"emission_color",vec3(1,0.5,0.25)) material.set_openpbr(n,"emission_luminance",2) '
        '_G.pe_emission_plane=n return "ok"')
    e = [float(v) for v in lua('local e=material.get(_G.pe_emission_plane).emissive return string.format("%.5f,%.5f,%.5f", e.x, e.y, e.z)').split(",")]
    assert max(abs(a - b) for a, b in zip(e, srgb_hdr([2.0, 1.0, 0.5]))) <= 2e-3, (e, srgb_hdr([2.0, 1.0, 0.5]))
    lua('material.set_openpbr(_G.pe_emission_plane,"emission_luminance",0.5) return "ok"')  # dirty update
    tool("set_camera", {"position": [0, 3, 6], "target": [0, 0, 0], "projection": "perspective"}); time.sleep(5)
    emissive = rt_pixel("emissive")[:, :3]  # openpbr off: the raster G-buffer decodes the mirror -> (0.5, 0.25, 0.125)
    assert np.abs(emissive - [128, 64, 32]).max() <= 1, emissive[:3]


def check_openpbr_module_fallbacks():
    # Own editor sessions with the generated module removed or corrupted (restored afterwards): use_openpbr is rejected,
    # the material passes never compile, and the only log warnings are the [OpenPBR] fallback ones.
    import shutil
    backup = GENERATED.with_name("Generated.pe-test-backup")
    layout = json.loads((GENERATED / "PeOpenPBRDefault.json").read_text(encoding="utf-8"))
    fixtures = {
        "missing-json": lambda: (GENERATED / "PeOpenPBRDefault.json").unlink(),
        "missing-slang": lambda: (GENERATED / "PeOpenPBRDefault.slang").unlink(),
        "wrapped-offset": lambda: (GENERATED / "PeOpenPBRDefault.json").write_text(json.dumps(
            dict(layout, inputs=[dict(layout["inputs"][1], offset=4294967292)] + layout["inputs"][2:])), encoding="utf-8"),
        "huge-bytesize": lambda: (GENERATED / "PeOpenPBRDefault.json").write_text(json.dumps(dict(layout, byteSize=4294967200)), encoding="utf-8"),
        "undersized": lambda: (GENERATED / "PeOpenPBRDefault.json").write_text(json.dumps(
            dict(layout, byteSize=4, inputs=[layout["inputs"][0]])), encoding="utf-8"),
        "swapped-offsets": lambda: (GENERATED / "PeOpenPBRDefault.json").write_text(json.dumps(  # in bounds, same types, wrong place
            dict(layout, inputs=[dict(layout["inputs"][0], offset=layout["inputs"][2]["offset"]), layout["inputs"][1],
                                 dict(layout["inputs"][2], offset=layout["inputs"][0]["offset"])] + layout["inputs"][3:])), encoding="utf-8"),
    }
    shutil.copytree(GENERATED, backup)
    try:
        with tempfile.TemporaryDirectory(prefix="pe-openpbr-fallback-") as d:
            assets = Path(d) / "Assets"; (assets / "Agent").mkdir(parents=True)
            (assets / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
            for name, corrupt in fixtures.items():
                shutil.rmtree(GENERATED); shutil.copytree(backup, GENERATED); corrupt()
                with editor_scene(API, assets):
                    r = lua('settings.set("openpbr",true) local n=scene.add_empty_node("P") scene.attach_primitive(n,"plane") '
                            'local ok,err=pcall(material.use_openpbr,n,true) return tostring(ok)..":"..tostring(err)')
                    assert r.startswith("false:") and "cannot use OpenPBR" in r, (name, r)
                    time.sleep(3)
                problems = log_problems()
                assert problems and all("[OpenPBR]" in l for l in problems), (name, problems[:5])
    finally:
        shutil.rmtree(GENERATED, ignore_errors=True); shutil.copytree(backup, GENERATED); shutil.rmtree(backup)


def check_openpbr_lua():
    new_scene()
    r = lua(OPENPBR_PLANE +
            'material.set_openpbr(n,"base_color",vec3(0.2,0.4,0.6)) material.set_openpbr(n,"base_metalness",0.25) '
            'material.set_openpbr(n,"geometry_thin_walled",true) '
            'local c=material.get_openpbr(n,"base_color") local m=material.get(n) '
            'local bad={} for _,b in ipairs({{"no_such_input",1},{"base_color",1},{"geometry_thin_walled",1},{"base_weight",vec3(1,1,1)}}) do '
            '  local ok,err=pcall(material.set_openpbr,n,b[1],b[2]) bad[#bad+1]=tostring(ok)..":"..tostring(err) end '
            'return string.format("%.3f,%.3f,%.3f|%.3f|%s|%.3f|%.3f|%.3f|", c.x,c.y,c.z, material.get_openpbr(n,"base_metalness"), '
            'tostring(material.get_openpbr(n,"geometry_thin_walled")), material.get_openpbr(n,"specular_ior"), m.metallic, m.roughness) '
            '..table.concat(bad,"#")')
    head, errors = r.split("|")[:6], r.split("|")[6].split("#")
    assert head == ["0.200,0.400,0.600", "0.250", "true", "1.500", "0.250", "0.300"], head  # mirror: metallic, roughness
    assert errors[0].startswith("false:") and "unknown OpenPBR input" in errors[0], errors[0]
    assert errors[1].startswith("false:") and "expects vec3" in errors[1], errors[1]
    assert errors[2].startswith("false:") and "expects boolean" in errors[2], errors[2]
    assert errors[3].startswith("false:") and "expects number" in errors[3], errors[3]
    base = lua(OPENPBR_PLANE + 'material.set_openpbr(n,"base_color",vec3(0.2,0.4,0.6)) local b=material.get(n).base_color '
               'return string.format("%.4f,%.4f,%.4f", b.x, b.y, b.z)')
    srgb = [round(1.055 * c ** (1 / 2.4) - 0.055, 4) for c in (0.2, 0.4, 0.6)]  # legacy colours are sRGB-authored
    assert [abs(float(v) - s) <= 2e-3 for v, s in zip(base.split(","), srgb)] == [True] * 3, (base, srgb)
```
  Append `check_openpbr_emission` to `CHECKS` too. `main` (Appendix A.1) runs `check_openpbr_module_fallbacks` in its own sessions. Run it. Expected: FAIL (`material.use_openpbr` is nil).
- [ ] **Step 2: Material instance flag.** In `Material.h`, class `MaterialInstance`:
  - public, after `void SetTextureMask(uint32_t mask);`:
```cpp
        // OpenPBR via MaterialX (desktop, setting openpbr): this mesh is shaded by the default OpenPBR module from the
        // params keyed by OpenPBR input name (see OpenPBRMaterial.h). Not persisted until Phase 3.
        void SetOpenPBR(bool enabled);
        bool GetOpenPBR() const { return m_openpbr; }
```
  - private, after `std::optional<uint32_t> m_textureMaskOverride;`: `bool m_openpbr = false;`
  - In `Material.cpp`, after `MaterialInstance::SetTextureMask`:
```cpp
    void MaterialInstance::SetOpenPBR(bool enabled)
    {
        m_openpbr = enabled;
        dirty = true;
    }
```
- [ ] **Step 3: `OpenPBRMaterial.h`:**
```cpp
#pragma once

#include "Scene/Material.h"

namespace pe
{
    struct Mesh;

    // One input of PE's default OpenPBR module (PhasmaCook --mtlx; RuntimeAssets/Shaders/MaterialX/Generated).
    struct OpenPBRInput
    {
        std::string input;           // OpenPBR input name, e.g. base_color
        std::string type;            // float, float3 or bool (the default module's types)
        uint32_t offset = 0;         // byte offset inside the inputs block
        std::vector<float> defaults; // MaterialX default (bool as 0/1)
    };

    struct OpenPBRLayout
    {
        std::vector<OpenPBRInput> inputs;
        uint32_t byteSize = 0;
        bool valid = false;

        const OpenPBRInput *Find(const std::string &input) const;
    };

    // A MaterialX material's bytes in the scene material byte buffer: the legacy MaterialGpuData record first (the
    // raster G-buffer, RT hits and the off path read it), then the module's inputs.
    inline constexpr uint32_t kOpenPBRInputsOffset = sizeof(MaterialGpuData);
    static_assert(kOpenPBRInputsOffset == 80, "PE_OPENPBR_INPUTS_OFFSET in VisibilityCommon.slang must match");

    // Loaded once from the generated .json; invalid (after one PE_WARN) when the .json or the .slang module is missing or
    // the layout is malformed. Invalid means: no material is eligible and the material passes stay disabled.
    const OpenPBRLayout &GetOpenPBRDefaultLayout();
    // The material can be shaded by the default module: the module is ready and the material is standard PBR
    // (no pass, or standard_pbr.pass, whose byte record is MaterialGpuData). Shader-driven materials own their bytes.
    bool OpenPBREligible(Material &material);
    bool MeshUsesOpenPBR(const Mesh &mesh);
    // The value an input resolves to: the instance's param override, then its material's params, then the default.
    void ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input, float out[3]);
    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance);
    // Legacy fields follow the constant inputs, so every legacy consumer (openpbr off, Android, unsupported GPUs, RT
    // hits, the raster G-buffer under the material passes) shows the same base colour, metalness, roughness, IOR and emission.
    void MirrorOpenPBRToLegacy(MaterialInstance &instance);
} // namespace pe
```
- [ ] **Step 4: `OpenPBRMaterial.cpp`:**
```cpp
#include "Scene/OpenPBRMaterial.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/SceneNode.h"
#include "gtc/color_space.hpp"
#include "rapidjson/document.h"

namespace pe
{
    namespace
    {
        struct OpenPBRSchemaEntry
        {
            const char *input;
            const char *type;
            uint32_t offset;
        };

        // The default module's layout as PhasmaCook generates it from MaterialX 1.39.5's open_pbr_surface (prototype
        // output 2026-10-13; Task 8's test pins the same 37 inputs / 220 bytes). The generated loader reads exactly these
        // offsets, so a .json that differs in any entry would make the GPU read past or beside the packed bytes.
        // A MaterialX upgrade regenerates the module: update this table with it.
        constexpr uint32_t kOpenPBRDefaultBytes = 220;
        constexpr OpenPBRSchemaEntry kOpenPBRDefaultSchema[] = {
            {"base_weight", "float", 0},
            {"base_color", "float3", 4},
            {"base_diffuse_roughness", "float", 16},
            {"base_metalness", "float", 20},
            {"specular_weight", "float", 24},
            {"specular_color", "float3", 28},
            {"specular_roughness", "float", 40},
            {"specular_ior", "float", 44},
            {"specular_roughness_anisotropy", "float", 48},
            {"transmission_weight", "float", 52},
            {"transmission_color", "float3", 56},
            {"transmission_depth", "float", 68},
            {"transmission_scatter", "float3", 72},
            {"transmission_scatter_anisotropy", "float", 84},
            {"transmission_dispersion_scale", "float", 88},
            {"transmission_dispersion_abbe_number", "float", 92},
            {"subsurface_weight", "float", 96},
            {"subsurface_color", "float3", 100},
            {"subsurface_radius", "float", 112},
            {"subsurface_radius_scale", "float3", 116},
            {"subsurface_scatter_anisotropy", "float", 128},
            {"fuzz_weight", "float", 132},
            {"fuzz_color", "float3", 136},
            {"fuzz_roughness", "float", 148},
            {"coat_weight", "float", 152},
            {"coat_color", "float3", 156},
            {"coat_roughness", "float", 168},
            {"coat_roughness_anisotropy", "float", 172},
            {"coat_ior", "float", 176},
            {"coat_darkening", "float", 180},
            {"thin_film_weight", "float", 184},
            {"thin_film_thickness", "float", 188},
            {"thin_film_ior", "float", 192},
            {"emission_luminance", "float", 196},
            {"emission_color", "float3", 200},
            {"geometry_opacity", "float", 212},
            {"geometry_thin_walled", "bool", 216},
        };
    } // namespace

    const OpenPBRInput *OpenPBRLayout::Find(const std::string &input) const
    {
        for (const OpenPBRInput &in : inputs)
            if (in.input == input)
                return &in;
        return nullptr;
    }

    const OpenPBRLayout &GetOpenPBRDefaultLayout()
    {
        static const OpenPBRLayout layout = []() -> OpenPBRLayout
        {
            const std::string dir = Path::RuntimeAssetsPath() + "Shaders/MaterialX/Generated/";
            const std::string path = dir + "PeOpenPBRDefault.json";
            if (!std::filesystem::exists(dir + "PeOpenPBRDefault.slang"))
            {
                PE_WARN("[OpenPBR] %sPeOpenPBRDefault.slang is missing (PhasmaCook --mtlx generates it at build time); MaterialX materials keep their legacy shading",
                        dir.c_str());
                return {};
            }
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                PE_WARN("[OpenPBR] %s is missing (PhasmaCook --mtlx generates it at build time); MaterialX materials keep their legacy shading",
                        path.c_str());
                return {};
            }
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            rapidjson::Document doc;
            doc.Parse(text.c_str());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("inputs") || !doc["inputs"].IsArray() ||
                !doc.HasMember("byteSize") || !doc["byteSize"].IsUint() || doc["byteSize"].GetUint() != kOpenPBRDefaultBytes ||
                !doc.HasMember("thin") || !doc["thin"].IsBool() || !doc["thin"].GetBool() ||
                doc["inputs"].GetArray().Size() != std::size(kOpenPBRDefaultSchema))
            {
                PE_WARN("[OpenPBR] %s is not a PhasmaCook material layout", path.c_str());
                return {};
            }
            OpenPBRLayout result;
            result.byteSize = doc["byteSize"].GetUint();
            for (const auto &item : doc["inputs"].GetArray())
            {
                if (!item.IsObject() || !item.HasMember("input") || !item["input"].IsString() || !item.HasMember("type") ||
                    !item["type"].IsString() || !item.HasMember("offset") || !item["offset"].IsUint() || !item.HasMember("default") ||
                    !item["default"].IsArray())
                {
                    PE_WARN("[OpenPBR] %s has a malformed input entry", path.c_str());
                    return {};
                }
                OpenPBRInput in;
                in.input = item["input"].GetString();
                in.type = item["type"].GetString();
                in.offset = item["offset"].GetUint();
                for (const auto &v : item["default"].GetArray())
                {
                    if (!v.IsNumber())
                    {
                        PE_WARN("[OpenPBR] %s: input %s has a non-numeric default", path.c_str(), in.input.c_str());
                        return {};
                    }
                    in.defaults.push_back(v.GetFloat());
                }
                // Entry i must equal the pinned schema's entry i (name, type, offset): that also rules out duplicates,
                // overlaps, gaps and offsets outside the 220 bytes the generated loader reads.
                const OpenPBRSchemaEntry &expected = kOpenPBRDefaultSchema[result.inputs.size()];
                const uint32_t width = in.type == "float3" ? 3u : 1u;
                if (in.input != expected.input || in.type != expected.type || in.offset != expected.offset || in.defaults.size() != width)
                {
                    PE_WARN("[OpenPBR] %s: input %s (%s @ %u) does not match the default module's layout (expected %s %s @ %u)", path.c_str(),
                            in.input.c_str(), in.type.c_str(), in.offset, expected.input, expected.type, expected.offset);
                    return {};
                }
                result.inputs.push_back(in);
            }
            result.valid = true; // every pinned entry matched, in order
            return result;
        }();
        return layout;
    }

    bool OpenPBREligible(Material &material)
    {
        const PassInfoAsset *pass = material.passInfoAsset.get();
        const bool standard = !pass || pass->GetFilePath().filename() == "standard_pbr.pass";
        return standard && GetOpenPBRDefaultLayout().valid;
    }

    bool MeshUsesOpenPBR(const Mesh &mesh)
    {
        return mesh.material && mesh.materialInstance && mesh.materialInstance->GetOpenPBR() && OpenPBREligible(*mesh.material);
    }

    void ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input, float out[3])
    {
        out[0] = input.defaults.size() > 0 ? input.defaults[0] : 0.0f;
        out[1] = input.defaults.size() > 1 ? input.defaults[1] : 0.0f;
        out[2] = input.defaults.size() > 2 ? input.defaults[2] : 0.0f;
        MaterialParamValue value;
        if (instance.HasParamOverride(input.input))
            value = instance.GetParam(input.input);
        else if (auto it = instance.GetParent()->params.find(input.input); it != instance.GetParent()->params.end())
            value = it->second;
        else
            return;
        if (const float *f = std::get_if<float>(&value))
            out[0] = *f;
        else if (const vec3 *v = std::get_if<vec3>(&value))
        {
            out[0] = v->x;
            out[1] = v->y;
            out[2] = v->z;
        }
        else if (const uint32_t *u = std::get_if<uint32_t>(&value))
            out[0] = *u != 0u ? 1.0f : 0.0f;
        else if (const int32_t *i = std::get_if<int32_t>(&value))
            out[0] = *i != 0 ? 1.0f : 0.0f;
    }

    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance)
    {
        const OpenPBRLayout &layout = GetOpenPBRDefaultLayout();
        std::vector<uint8_t> bytes(kOpenPBRInputsOffset + (layout.valid ? layout.byteSize : 0u), 0);
        const MaterialGpuData legacy = instance.BuildGpuData();
        std::memcpy(bytes.data(), &legacy, sizeof(legacy));
        for (const OpenPBRInput &in : layout.inputs)
        {
            float values[3];
            ResolveOpenPBRInput(instance, in, values);
            uint8_t *dst = bytes.data() + kOpenPBRInputsOffset + in.offset;
            if (in.type == "bool")
            {
                const uint32_t b = values[0] != 0.0f ? 1u : 0u;
                std::memcpy(dst, &b, sizeof(b));
            }
            else
                std::memcpy(dst, values, (in.type == "float3" ? 3u : 1u) * sizeof(float));
        }
        return bytes;
    }

    void MirrorOpenPBRToLegacy(MaterialInstance &instance)
    {
        const OpenPBRLayout &layout = GetOpenPBRDefaultLayout();
        auto value = [&](const char *name) -> vec3
        {
            float v[3] = {0.0f, 0.0f, 0.0f};
            if (const OpenPBRInput *in = layout.Find(name))
                ResolveOpenPBRInput(instance, *in, v);
            return vec3(v[0], v[1], v[2]);
        };
        if (!layout.valid)
            return;
        // Legacy colours are sRGB-authored (SceneColor decodes them under linear_color); OpenPBR's are linear.
        const vec3 base = glm::clamp(value("base_color") * value("base_weight").x, vec3(0.0f), vec3(1.0f));
        instance.SetBaseColorFactor(vec4(glm::convertLinearToSRGB(base), instance.GetBaseColorFactor().a));
        instance.SetMetallic(value("base_metalness").x);
        instance.SetRoughness(value("specular_roughness").x);
        instance.SetIor(value("specular_ior").x);
        // Legacy emissive is an sRGB-authored colour whose peak above 1 carries brightness (SrgbToLinearHdr, Common.hlsl:294;
        // GBufferPS.hlsl:93, RayTrace.hlsl:935): store the inverse of OpenPBR's linear emission_color * emission_luminance
        // (MaterialX's own emission weight, emission_weight_out in the generated module).
        const vec3 emission = glm::max(value("emission_color") * value("emission_luminance").x, vec3(0.0f));
        const float peak = std::max({emission.r, emission.g, emission.b, 1.0f});
        instance.SetEmissiveFactor(glm::convertLinearToSRGB(emission / peak) * peak);
    }
} // namespace pe
```
  `Mesh` is defined in `Scene/SceneNode.h:48`; `PassInfoAsset::GetFilePath()` is at `Scene/PassInfoAsset.h:24`. Primitives and loaded legacy materials carry `standard_pbr.pass` (`Primitives.cpp:219-221`, `Material.cpp:100-102`), so they are eligible. If `ResourcePtr::get()` (or whatever `passInfoAsset.get()` is) is not callable on that object, use the accessor `SceneBuffers.cpp` uses (`mat->passInfoAsset.get()`). If `GetFilePath()` is empty for the primitive's standard pass, **STOP** and report: eligibility must then compare the asset handle instead. `gtc/color_space.hpp` is the include `SceneRenderTargets.cpp:7` already uses.
- [ ] **Step 5: Byte path and flag** in `SceneBuffers.cpp`. Add `#include "Scene/OpenPBRMaterial.h"`.
  1. In `Scene::CreateMaterialTable`, in the byte-gather loop, replace:
```cpp
                    Mesh &mesh = m_meshes[meshIdx];
                    if (mesh.indexCount == 0 || !mesh.material || !mesh.material->passInfoAsset)
                        continue;
```
     with:
```cpp
                    Mesh &mesh = m_meshes[meshIdx];
                    if (mesh.indexCount == 0 || !mesh.material)
                        continue;
                    if (MeshUsesOpenPBR(mesh)) // legacy record + OpenPBR inputs, per instance (before the standard_pbr pass packing)
                    {
                        MaterialInstance *inst = mesh.materialInstance;
                        if (inst->gpuByteOffset != 0xFFFFFFFF)
                            continue;
                        inst->gpuByteOffset = 0; // gathered; final offsets follow byte packing
                        byteEntries.push_back({&inst->gpuByteOffset, &inst->gpuByteSize, {}, mesh.material, inst, nullptr});
                        continue;
                    }
                    if (!mesh.material->passInfoAsset)
                        continue;
```
  2. Replace the `pack` lambda body's loop with:
```cpp
                for (size_t i = begin; i < end; ++i)
                {
                    auto &entry = byteEntries[i];
                    if (!entry.layout) // MaterialX instance
                    {
                        entry.data = BuildOpenPBRBytes(*entry.instance);
                        continue;
                    }
                    const auto &layout = *entry.layout;
                    entry.data = entry.instance ? entry.instance->BuildByteAddressData(layout.structMembers, layout.textureSlots, layout.totalByteSize)
                                                : entry.material->BuildByteAddressData(layout.structMembers, layout.textureSlots, layout.totalByteSize);
                }
```
  3. In `Scene::UpdateDirtyMaterials`, in the node/instance loop, before the comment `// Standard PBR instances only need the structured material table upload above.`:
```cpp
                if (inst->GetOpenPBR() && OpenPBREligible(*parent)) // same predicate as MeshUsesOpenPBR
                {
                    if (inst->gpuByteOffset == 0xFFFFFFFF)
                    {
                        pendingDirty = true; // the full rebuild that use_openpbr requested packs it
                        continue;
                    }
                    byteDirtyEntries.push_back({inst->gpuByteOffset, &inst->dirty, BuildOpenPBRBytes(*inst)});
                    continue;
                }
```
  4. In `Scene::ComputeMeshConstants`, after `flags |= 16u; // dedicated terrain triplanar pipeline (cull bucket 8)`:
```cpp
        if (MeshUsesOpenPBR(mesh))
            flags |= 32u; // MaterialX material: the material passes shade its visibility pixels
```
  5. In `UpdateMeshSelectionFlags` (the second writer of `editorFlags`, about line 150), after its `flags |= 16u;` line, add the same two lines.
- [ ] **Step 6: Lua.** In `MaterialBindings.cpp`:
  - Add `#include "Scene/OpenPBRMaterial.h"`.
  - After the `mat.set_function("set", sol::overload(…));` statement, inside the same `AddBindings` lambda:
```cpp
                // OpenPBR via MaterialX (desktop, setting openpbr): a per-mesh switch and the default module's constant
                // inputs by OpenPBR input name. Not saved with the scene until Phase 3.
                auto useOpenPBR = [ensureInstance](Scene *s, int meshIdx, bool enabled) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    if (enabled && !OpenPBREligible(*mesh.material)) // before any state change
                        throw sol::error("material.use_openpbr: this mesh's material cannot use OpenPBR (custom shader material, or "
                                         "the generated OpenPBR module is unavailable: see the log)");
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst || inst->GetOpenPBR() == enabled) return;
                    inst->SetOpenPBR(enabled);
                    if (enabled)
                        MirrorOpenPBRToLegacy(*inst);
                    s->SetTexturesDirty(); // byte layout and editorFlags change: full material-table + mesh-constant rebuild
                    s->SetMaterialDirty();
                };
                auto openpbrInput = [](const char *fn, const std::string &name) -> const OpenPBRInput & {
                    const OpenPBRLayout &layout = GetOpenPBRDefaultLayout();
                    if (!layout.valid)
                        throw sol::error(std::string("material.") + fn + ": the OpenPBR layout is unavailable (see the log)");
                    const OpenPBRInput *in = layout.Find(name);
                    if (!in)
                        throw sol::error(std::string("material.") + fn + ": unknown OpenPBR input '" + name + "'");
                    return *in;
                };
                auto setOpenPBR = [ensureInstance, openpbrInput](Scene *s, int meshIdx, const std::string &name, const sol::object &value) {
                    const OpenPBRInput &in = openpbrInput("set_openpbr", name);
                    MaterialParamValue v;
                    if (in.type == "float" && value.get_type() == sol::type::number)
                        v = value.as<float>();
                    else if (in.type == "float3" && value.is<vec3>())
                        v = value.as<vec3>();
                    else if (in.type == "bool" && value.get_type() == sol::type::boolean)
                        v = value.as<bool>() ? 1u : 0u;
                    else
                        throw sol::error("material.set_openpbr: '" + name + "' expects " +
                                         (in.type == "float3" ? "vec3" : in.type == "bool" ? "boolean" : "number"));
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    inst->SetParam(name, v);
                    MirrorOpenPBRToLegacy(*inst);
                    s->SetMaterialDirty();
                };
                auto getOpenPBR = [openpbrInput](Scene *s, int meshIdx, const std::string &name, sol::state_view lua) -> sol::object {
                    const OpenPBRInput &in = openpbrInput("get_openpbr", name);
                    if (meshIdx < 0) return sol::make_object(lua, sol::nil);
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.materialInstance) return sol::make_object(lua, sol::nil);
                    float v[3];
                    ResolveOpenPBRInput(*mesh.materialInstance, in, v);
                    if (in.type == "float3") return sol::make_object(lua, vec3(v[0], v[1], v[2]));
                    if (in.type == "bool") return sol::make_object(lua, v[0] != 0.0f);
                    return sol::make_object(lua, v[0]);
                };
                mat.set_function("use_openpbr", sol::overload(
                    [useOpenPBR](SceneNodeHandle &h, bool enabled) { Scene *s = GetScene(); useOpenPBR(s, ResolveMeshIdx(s, h), enabled); },
                    [useOpenPBR](SceneNodeHandle &h, int slot, bool enabled) { Scene *s = GetScene(); useOpenPBR(s, ResolveMeshIdx(s, h, slot), enabled); }));
                mat.set_function("set_openpbr", sol::overload(
                    [setOpenPBR](SceneNodeHandle &h, const std::string &name, sol::object value) { Scene *s = GetScene(); setOpenPBR(s, ResolveMeshIdx(s, h), name, value); },
                    [setOpenPBR](SceneNodeHandle &h, int slot, const std::string &name, sol::object value) { Scene *s = GetScene(); setOpenPBR(s, ResolveMeshIdx(s, h, slot), name, value); }));
                mat.set_function("get_openpbr", sol::overload(
                    [getOpenPBR](SceneNodeHandle &h, const std::string &name, sol::this_state ts) -> sol::object { Scene *s = GetScene(); return getOpenPBR(s, ResolveMeshIdx(s, h), name, sol::state_view(ts)); },
                    [getOpenPBR](SceneNodeHandle &h, int slot, const std::string &name, sol::this_state ts) -> sol::object { Scene *s = GetScene(); return getOpenPBR(s, ResolveMeshIdx(s, h, slot), name, sol::state_view(ts)); }));
```
  - If `ensureInstance` was captured by value into the existing lambdas only, it is still in scope here (it is a local of the same `AddBindings` lambda). If `sol::overload` reports an ambiguity between the `(h, name, value)` and `(h, slot, name, value)` forms, the arities differ: report the compiler message, do not rename the API.
- [ ] **Step 7:** Build, run `openpbr_vis.py` on both APIs (expected: pass), read the log, run `clang-format`.
  Amended 2026-10-14 after Task 9's first run:
  - **Readback.** `rt_pixel()` failed on Vulkan with `image is not transfer-src; cannot stage readback on Vulkan`: `CreateSceneRenderTarget` (`Render/SceneRenderTargets.cpp`) gave colour targets no `TRANSFER_SRC`. The orchestrator added `PE_IMAGE_USAGE_TRANSFER_SRC` to its base usage, so every scene colour target (this task's `emissive`, Task 10's `albedo`, `srm`, `normal`) reads back on both APIs. Task 7's deferred perf run (off vs baseline) covers the flag's cost.
  - **Lua errors.** The bindings raise errors with `luaL_error`, not `throw sol::error`: under LuaJIT the thrown `sol::error` reached Lua as the bare text `C++ exception`, and Step 1's checks match the message text.

### Task 10: Material pass 1 (thin G-buffer)

**Files:**
- Create `Shaders/MaterialX/MaterialThin.slang`, `Phasma/Runtime/Code/RenderPasses/MaterialThinPass.{h,cpp}`.
- Modify `Phasma/Runtime/Code/Render/SceneRenderGraph.{h,cpp}`.
- Test: `openpbr_vis.py`.

**Interfaces:**
- Consumes: `VisibilityCommon.slang` (Task 6), the generated `Generated/PeOpenPBRDefault.slang` (Task 8), `Mesh_Constants.editorFlags` bit 32 and the byte layout (Task 9).
- Produces: pass ID `SceneRenderGraphPassId::MaterialThin`, order 307, enabled when `SceneUsesOpenPBR()` and raster.

- [ ] **Step 1: Failing test.** Add to `openpbr_vis.py` and append to `CHECKS`:
```python
def check_material_pass1():
    # A MaterialX plane filling the centre. The legacy fields are deliberately desynchronised from the OpenPBR inputs
    # after set_openpbr's mirror, so the raster G-buffer and pass 1 disagree: the targets must show pass 1's values.
    new_scene()
    lua('settings.set("openpbr",true) settings.set("linear_color",true) settings.set("taa",false) settings.set("render_scale",1) ' +
        OPENPBR_PLANE + 'material.set_openpbr(n,"base_color",vec3(0.2,0.4,0.6)) material.set_openpbr(n,"base_weight",0.5) '
        'material.set_openpbr(n,"specular_roughness",0.7) material.set_openpbr(n,"base_metalness",0.3) '
        'material.set(n,"base_color",vec4(1,1,1,1)) material.set(n,"roughness",0.95) material.set(n,"metallic",0.9) return "ok"')
    tool("set_camera", {"position": [0, 3, 6], "target": [0, 0, 0], "projection": "perspective"}); time.sleep(5)
    srgb = [round((1.055 * (c * 0.5) ** (1 / 2.4) - 0.055) * 255) for c in (0.2, 0.4, 0.6)]
    albedo = rt_pixel("albedo")
    assert np.abs(albedo[:, :3] - srgb).max() <= 1, (albedo[:3], srgb)
    srm = rt_pixel("srm")
    assert np.abs(srm[:, :3] - [255, round(0.7 * 255), round(0.3 * 255)]).max() <= 1, srm[:3]
    normal = rt_pixel("normal")  # stored n * 0.5 + 0.5; "linear" copies it: plane normal +Y -> (128, 255, 128)
    assert np.abs(normal[:, :3] - [128, 255, 128]).max() <= 1, normal[:3]
    emissive = rt_pixel("emissive")
    assert emissive[:, :3].max() == 0, emissive[:3]
```
  `visualize="linear"` copies the stored value (`ConvertImageRowsToRGBA`, `EditorToolRuntime.cpp:368`); the `"normal"` view would apply `x * 0.5 + 0.5` a second time. Run the test. Expected: FAIL (the raster G-buffer holds the desynchronised legacy values: white albedo, roughness 0.95, metallic 0.9).
- [ ] **Step 2: `Shaders/MaterialX/MaterialThin.slang`** (new; prototype-compiled against the generated default module):
```slang
// Material pass 1: the thin G-buffer for visibility pixels of MaterialX materials, so SSAO, GI, screen probes,
// reflections, SSR and TAA read MaterialX's normal, albedo, roughness and metalness. Velocity and transparency stay
// the raster G-buffer's (Phase 2 still rasterises these meshes into it); emissive is cleared because pass 2's
// MaterialX colour includes emission.
#include "../Common/Structures.hlsl"
#include "../Common/Common.hlsl"
#include "VisibilityCommon.slang"

struct PushConstants_MaterialThin
{
    uint verticesOffset;
    uint linearColor;
};

[[vk::push_constant]] PushConstants_MaterialThin pc;
[[vk::binding(0, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(1, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(2, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(3, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(4, 0)]] ByteAddressBuffer pe_materialBytes;
#ifdef PE_NORMAL_RGBA32F
[[vk::image_format("rgba32f")]][[vk::binding(5, 0)]] RWTexture2D<float4> pe_normalOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(5, 0)]] RWTexture2D<float4> pe_normalOut;
#endif
[[vk::image_format("rgba8")]][[vk::binding(6, 0)]] RWTexture2D<float4> pe_albedoOut;
[[vk::image_format("rgba8")]][[vk::binding(7, 0)]] RWTexture2D<float4> pe_srmOut;
#ifdef PE_EMISSIVE_RGBA8
[[vk::image_format("rgba8")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_emissiveOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(8, 0)]] RWTexture2D<float4> pe_emissiveOut;
#endif
[[vk::binding(0, 1)]] Texture2D pe_textures[];
[[vk::binding(1, 1)]] SamplerState pe_samplers[];

// Pass 1 reads inputs only; the module's shading hooks are never called here.
#define PE_REFRACTION_TWO_SIDED false
float3 pe_view_position() { return float3(0.0, 0.0, 0.0); }
uint pe_light_count() { return 0u; }
void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{
    L = float3(0.0, 0.0, 1.0);
    radiance = float3(0.0, 0.0, 0.0);
    occlusion = 1.0;
}
float3 pe_env_radiance(float3 N, float3 V, float3 L, float2 alpha) { return float3(0.0, 0.0, 0.0); }
float3 pe_env_irradiance(float3 N) { return float3(0.0, 0.0, 0.0); }

#include "Generated/PeOpenPBRDefault.slang"

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy >> 1;
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane;
    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    if (any(pixel >= size))
        return;
    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u || (pe_constants[vis.x - 1u].editorFlags & PE_EDITOR_FLAG_MATERIALX) == 0u)
        return;
    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_constants, pc.verticesOffset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));
    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    const PeThin thin = pe_material_thin(sp);
    if (!all(lane == (pixel & 1u)))
        return;
    // GBufferPS's encodings: normal * 0.5 + 0.5; albedo sRGB-encoded under linear_color; srm = (AO, roughness,
    // metalness, transmission flag); emissive alpha 1 (not night-emissive).
    pe_normalOut[pixel] = float4(thin.normal * 0.5 + 0.5, 1.0);
    const float3 albedo = saturate(thin.albedo);
    pe_albedoOut[pixel] = float4(pc.linearColor != 0 ? LinearToSrgb(albedo) : albedo, 1.0);
    pe_srmOut[pixel] = float4(1.0, saturate(thin.roughness), saturate(thin.metalness), 0.0);
    pe_emissiveOut[pixel] = float4(0.0, 0.0, 0.0, 1.0);
}
```
  The four storage formats are the targets' own: `normal` is `SceneNormalRTFormat()` (rgba16f or rgba32f); `albedo`/`srm` are `kDisplayColorFormat` (rgba8); `emissive` is the scene colour format (rgba16f with HDR, else rgba8). `pe_textures`/`pe_samplers` are compiled out for the default module (it never samples), so set 1 has no reflected bindings and the pass never touches it.
- [ ] **Step 3: `MaterialThinPass.h`:**
```cpp
#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    // Material pass 1 (desktop OpenPBR): writes MaterialX's normal, albedo, roughness and metalness into the G-buffer
    // for visibility pixels of MaterialX materials, before SSAO/GI/reflections read it.
    class MaterialThinPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override {}
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_visibility = nullptr;
        Image *m_normal = nullptr;
        Image *m_albedo = nullptr;
        Image *m_srm = nullptr;
        Image *m_emissive = nullptr;
        ::PeFormat m_builtNormalFormat = PE_FORMAT_UNDEFINED;
        ::PeFormat m_builtEmissiveFormat = PE_FORMAT_UNDEFINED;
        bool m_bound = false; // this frame's descriptors were written: ExecutePass may dispatch
    };
} // namespace pe
```
  If `PE_FORMAT_UNDEFINED` is not the enum's first value name, use the first value of `enum PeFormat` (`RHITypes.h:47`).
- [ ] **Step 4: `MaterialThinPass.cpp`:**
```cpp
#include "MaterialThinPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct PushConstants_MaterialThin
        {
            uint32_t verticesOffset;
            uint32_t linearColor;
        };
    } // namespace

    void MaterialThinPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_visibility = rs->GetRenderTarget("visibility");
        m_normal = rs->GetRenderTarget("normal");
        m_albedo = rs->GetRenderTarget("albedo");
        m_srm = rs->GetRenderTarget("srm");
        m_emissive = rs->GetRenderTarget("emissive");
    }

    void MaterialThinPass::UpdatePassInfo()
    {
        std::vector<Define> defines;
        m_builtNormalFormat = m_normal ? m_normal->GetFormat() : PE_FORMAT_UNDEFINED;
        m_builtEmissiveFormat = m_emissive ? m_emissive->GetFormat() : PE_FORMAT_UNDEFINED;
        if (m_builtNormalFormat == PE_FORMAT_R32G32B32A32_SFLOAT)
            defines.push_back(Define{"PE_NORMAL_RGBA32F", "1"});
        if (m_builtEmissiveFormat == PE_FORMAT_R8G8B8A8_UNORM)
            defines.push_back(Define{"PE_EMISSIVE_RGBA8", "1"});
        m_passInfo->name = "MaterialThin_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/MaterialThin.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = defines});
        m_passInfo->Update();
    }

    void MaterialThinPass::Update()
    {
        m_bound = false;
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_normal || !m_albedo || !m_srm || !m_emissive || !scene->GetBuffer() ||
            !scene->GetMaterialByteBuffer())
            return;
        const uint32_t frame = RHII.GetFrameIndex();
        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];
        set->SetImageView(0, m_visibility->GetSRV());
        set->SetBuffer(1, scene->GetBuffer());
        set->SetBuffer(2, scene->GetUniforms(frame));
        set->SetBuffer(3, scene->GetMeshConstants());
        set->SetBuffer(4, scene->GetMaterialByteBuffer());
        set->SetImageView(5, m_normal->GetUAV(0));
        set->SetImageView(6, m_albedo->GetUAV(0));
        set->SetImageView(7, m_srm->GetUAV(0));
        set->SetImageView(8, m_emissive->GetUAV(0));
        set->Update();
        m_bound = true;
    }

    void MaterialThinPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;
        builder.ReadCompute(m_visibility);
        // WriteCompute keeps the contents (layout change from the raster attachment): only MaterialX pixels change.
        for (Image *image : {m_normal, m_albedo, m_srm, m_emissive})
            builder.WriteCompute(image);
    }

    void MaterialThinPass::DeclareOutputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;
        for (Image *image : {m_normal, m_albedo, m_srm, m_emissive})
            builder.OutputCustom(image, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void MaterialThinPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_bound)
            return;
        PushConstants_MaterialThin pc{};
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());
        pc.linearColor = SceneUsesLinearColor() ? 1u : 0u;
        cmd->BeginDebugRegion("MaterialThinPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Dispatch((m_visibility->GetWidth() * 2u + 7u) / 8u, (m_visibility->GetHeight() * 2u + 7u) / 8u, 1);
        cmd->EndDebugRegion();
    }

    void MaterialThinPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();
        // normal_format and HDR change the storage formats: rebuild (only graphics passes are rebuilt for us).
        const ::PeFormat normal = m_normal ? m_normal->GetFormat() : PE_FORMAT_UNDEFINED;
        const ::PeFormat emissive = m_emissive ? m_emissive->GetFormat() : PE_FORMAT_UNDEFINED;
        if (m_passInfo->pCompShader && (normal != m_builtNormalFormat || emissive != m_builtEmissiveFormat))
        {
            m_passInfo->DestroyShaders();
            UpdatePassInfo();
        }
    }

    void MaterialThinPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        m_visibility = m_normal = m_albedo = m_srm = m_emissive = nullptr;
    }
} // namespace pe
```
- [ ] **Step 5: Register** in `SceneRenderGraph.{h,cpp}`, the same 8 places as Task 6 Step 4b:
  1. enum: `MaterialThin, // openpbr: material pass 1 (thin G-buffer)` after `VisibilityDebug`;
  2. components: `IRenderPassComponent *materialThin = nullptr;`;
  3. `#include "RenderPasses/MaterialThinPass.h"`, and the row `{SceneRenderGraphPassId::MaterialThin, 307, "MaterialThin", &SceneRenderGraphPassComponents::materialThin},` after the VisibilityDebug row;
  4. `ShouldUpdateSceneRenderGraphPass`: `if (component == components.materialThin) return isPassEnabled(SceneRenderGraphPassId::MaterialThin);`;
  5. `CreateSceneRenderGraphPassComponent<MaterialThinPass>(renderPassComponents);` after the VisibilityDebug one;
  6. `scenePasses.materialThin = GetGlobalComponent<MaterialThinPass>();`;
  7. DX12 branch: `SetPassEnabled(passEnabled, SceneRenderGraphPassId::MaterialThin, SceneUsesOpenPBR() && GetOpenPBRDefaultLayout().valid && dx12RenderRaster);`; Vulkan path: `SetPassEnabled(passEnabled, SceneRenderGraphPassId::MaterialThin, SceneUsesOpenPBR() && GetOpenPBRDefaultLayout().valid && renderRaster);` (add `#include "Scene/OpenPBRMaterial.h"` to `SceneRenderGraph.cpp`; the `&&` order keeps the layout unloaded while `openpbr` is off);
  8. no `SetScene`.

  Order 307 runs after the debug view (305, which must compare against the raster G-buffer) and before VoxelHiZ (310) and SSAO (400).
- [ ] **Step 6:** Build, run `openpbr_vis.py` on both APIs (expected: pass, including the Task 6 checks: Sponza has no MaterialX materials, so pass 1 changes nothing there), read the log, run `clang-format`.
  Amended 2026-10-14 after Task 10's first run: `check_toggle_identity` (Task 4) failed once on Vulkan, 5 s after toggling off: 6 floor pixels differed by 1/255 in green, at mirror-image positions on a smooth gradient. A fixed 5 s wait does not cover the lighting history settling after the toggle's target rebuild. In repeat runs it settled to exact identity within 10 s, and in two later rounds it was exact at every 1 s sample. The orchestrator replaced the fixed wait with `assert_settles_to(before, …)`, which still demands exact identity, polled for up to 20 s.

### Task 11: Material pass 2 (MaterialX shading under PE lighting)

**Files:**
- `Shaders/Common/Structures.hlsl` (`PushConstants_Lighting`)
- Create `Shaders/MaterialX/PePrelude.slang`, `Shaders/MaterialX/MaterialShade.slang`
- `Phasma/Runtime/Code/RenderPasses/LightPass.{h,cpp}`
- `Phasma/Runtime/Code/Render/SceneRenderGraph.{h,cpp}`
- Test: `openpbr_vis.py`

**Interfaces:**
- Consumes: LightOpaquePass's uniform (`m_uniforms[frame]`, `LightPassUBO` + GI fallback tail), its shadow fallback resources, the anonymous-namespace helpers in `LightPass.cpp` (`ShadowsActive`, `GetLiveShadowViews`, `BindProbeLighting`, `ActiveScreenIrradiance`, `ActiveReflections`).
- Produces: `class MaterialShadePass` (declared in `LightPass.h`, defined in `LightPass.cpp`), pass ID `SceneRenderGraphPassId::MaterialShade`, order 510 (after LightOpaque 500, before GBufferTransparent 600).

- [ ] **Step 1: Failing test.** Add to `openpbr_vis.py` and append to `CHECKS`:
```python
def viewport_window():
    return rt_pixel("viewport")[:, :3].astype(np.float64)


def check_material_pass2():
    # Pass 2 shades a MaterialX plane with MaterialX's code: OpenPBR fuzz (not in the legacy model) attenuates the base
    # layer uniformly and adds red sheen, so green must drop. Without pass 2 the legacy mirror shows no change.
    new_scene()
    lua('settings.set("openpbr",true) settings.set("linear_color",true) settings.set("taa",false) settings.set("render_scale",1) '
        'settings.set("IBL",false) settings.set("global_illumination",false) settings.set("ssao",false) '
        'if lights.get_counts().directional==0 then scene.add_directional_light() end ' + OPENPBR_PLANE +
        '_G.pe_pass2_plane=n return "ok"')
    tool("set_camera", {"position": [0, 3, 6], "target": [0, 0, 0], "projection": "perspective"}); time.sleep(5)
    plain = viewport_window()
    lua('material.set_openpbr(_G.pe_pass2_plane,"fuzz_weight",1.0) material.set_openpbr(_G.pe_pass2_plane,"fuzz_color",vec3(1,0,0)) '
        'return "ok"'); time.sleep(4)
    fuzz = viewport_window()
    # The capture turns NaN/Inf into 0 or 255 (ToByte clamps): every channel of every pixel must stay strictly inside.
    for label, window in (("plain", plain), ("fuzz", fuzz)):
        assert ((window > 0) & (window < 255)).all(), f"{label}: non-finite or saturated shading {window[:3]}"
    assert fuzz[:, 1].mean() < plain[:, 1].mean() - 2.0, (plain.mean(0), fuzz.mean(0))
    lua('material.set_openpbr(_G.pe_pass2_plane,"fuzz_weight",0.0) return "ok"'); time.sleep(4)
    assert np.abs(viewport_window() - plain).max() == 0, "fuzz_weight back to 0 did not restore the original shading"
```
  Lua globals persist across `execute_lua` calls (the existing `cluster_instances.py` relies on it). Run the test. Expected: FAIL (no pass 2, so fuzz changes nothing on screen).
- [ ] **Step 2: Push constant.** In `Structures.hlsl`, `struct PushConstants_Lighting`, after `uint shadow_debug_mode;`:
```hlsl
#ifdef PE_OPENPBR_SHADE
    uint vertices_offset; // material pass 2 only: Scene::GetVerticesOffset()
#endif
```
  `LightingPS` is compiled without the define, so its layout is unchanged.
- [ ] **Step 3: `Shaders/MaterialX/PePrelude.slang`** (new; prototype-compiled):
```slang
// PE prelude for MaterialX modules in material pass 2: the pe_* hooks the generated code calls, implemented on
// LightingPS's own data (Lighting.hlsl: light buffer, CSM, probe GI, screen probes, reflections, IBL) so a MaterialX
// pixel sees exactly the lights, shadows and environment a legacy pixel does. Include after Lighting.hlsl and
// VisibilityCommon.slang, before the generated module. The resolve sets the pe_* statics per lane.
#ifndef PE_PRELUDE
#define PE_PRELUDE

#define PE_REFRACTION_TWO_SIDED false

static float3 pe_P;      // world position of this lane's sample
static float3 pe_N;      // interpolated geometric normal (shadow bias, as LightingPS's G-buffer normal)
static uint2 pe_pixel;   // output pixel (GI screen textures)
static float pe_ssao;    // SSAO, 1 when off

float3 pe_view_position()
{
    return cb_camPos.xyz;
}

// Directional, point, spot, then area (LightSystemUBO counts: enabled lights only). MaterialX has no area light:
// an area light is its centre point with the point-light window (the spec's representative-point limit).
uint pe_light_count()
{
    return cb_numDirectionalLights + cb_numPointLights + cb_numSpotLights + cb_numAreaLights;
}

float PeWindow(float dist, float range)
{
    float attenuation = max(0.0, 1.0 - (dist / range) * (dist / range));
    return attenuation * attenuation;
}

// L points to the light; radiance excludes N.L (MaterialX's BSDF responses include it). Mirrors LightingPS's
// DirectLight / ComputePointLight / ComputeSpotLight attenuation, and its CSM for the first directional light.
void pe_light_sample(uint i, float3 P, out float3 L, out float3 radiance, out float occlusion)
{
    occlusion = 1.0;
    if (i < cb_numDirectionalLights)
    {
        const DirectionalLight light = LoadDirectionalLight(i);
        L = -RotateVectorByQuat(float3(0, 0, -1), light.rotation);
        radiance = light.color.rgb * light.color.a;
        if (i == 0 && cb_shadows != 0)
        {
            const float viewDepth = max(0.0f, dot(P - cb_camPos.xyz, cb_camForward.xyz));
            occlusion = CalculateShadows(P, viewDepth, dot(pe_N, L), pe_N);
        }
        return;
    }
    i -= cb_numDirectionalLights;
    if (i < cb_numPointLights)
    {
        const PointLight light = LoadPointLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float range = light.position.w;
        const float dist = max(0.1, length(toSurface));
        float attenuation = PeWindow(dist, range);
        if (cb_physicalPointFalloff > 0.5)
            attenuation /= max(dist * dist, 1e-4);
        L = normalize(-toSurface);
        radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * attenuation * light.color.w;
        return;
    }
    i -= cb_numPointLights;
    if (i < cb_numSpotLights)
    {
        const SpotLight light = LoadSpotLight(i);
        const float3 toSurface = P - light.position.xyz;
        const float range = light.position.w;
        const float dist = max(0.1, length(toSurface));
        L = normalize(-toSurface);
        const float3 spotDir = normalize(RotateVectorByQuat(float3(0, 0, -1), light.rotation));
        const float theta = dot(-L, spotDir);
        const float cutoffCos = cos(radians(light.params.x));
        const float outerCutoffCos = cos(radians(light.params.x + light.params.y));
        const float cone = theta < outerCutoffCos ? 0.0 : smoothstep(outerCutoffCos, cutoffCos, theta);
        radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * PeWindow(dist, range) * cone * light.color.w;
        return;
    }
    i -= cb_numSpotLights;
    const AreaLight light = LoadAreaLight(i);
    const float3 toSurface = P - light.position.xyz;
    const float range = light.position.w;
    const float dist = max(0.1, length(toSurface));
    L = normalize(-toSurface);
    radiance = dist > range ? float3(0.0, 0.0, 0.0) : light.color.xyz * PeWindow(dist, range) * light.color.w;
}

// MaterialX's irradiance is radiance-equivalent (its diffuse multiplies it by the albedo, no 1/pi): LightingPS's
// probe term E * gi_intensity * weight / pi, with IBL (cube at mip levels-3, ComputeIBL_Common) filling 1 - weight.
float3 pe_env_irradiance(float3 N)
{
    float4 indirect = ProbeLighting(pe_P, N);
    if (cb_screenProbes != 0)
    {
        uint width, height;
        gi_screenIrradiance.GetDimensions(width, height);
        const float4 screen = gi_screenIrradiance.Load(int3(int2((float2(pe_pixel) + 0.5) * float2(width, height) / pc.framebufferSize), 0));
        const float weight = lerp(indirect.a, 1.0, screen.a);
        indirect.rgb = weight > 0.0 ? lerp(indirect.rgb * indirect.a, screen.rgb, screen.a) / weight : 0.0.xxx;
        indirect.a = weight;
    }
    const float indirectWeight = gi_intensity > 0.0 ? indirect.a : 0.0;
    float3 result = indirect.rgb * (gi_intensity * indirectWeight / PI) * (cb_screenProbes != 0 ? 1.0 : pe_ssao);
    if (cb_IBL != 0)
    {
        float w, h, levels;
        Cube.GetDimensions(0, w, h, levels);
        result += Cube.SampleLevel(sampler_Cube, N, max(0.0, levels - 3.0)).rgb * cb_IBL_intensity * (1.0 - indirectWeight) * pe_ssao;
    }
    return result;
}

// Prefiltered radiance along L: ReflectionPass's traced radiance where it has data (as LightingPS, for every lobe:
// it is traced at the G-buffer roughness pass 1 wrote), otherwise the cube at ComputeIBL_Common's mip for alpha.
float3 pe_env_radiance(float3 N, float3 V, float3 L, float2 alpha)
{
    if (cb_reflections != 0)
    {
        uint width, height;
        gi_reflections.GetDimensions(width, height);
        const float4 reflection = gi_reflections.Load(int3(int2((float2(pe_pixel) + 0.5) * float2(width, height) / pc.framebufferSize), 0));
        if (reflection.a > 0.0)
            return reflection.rgb * pe_ssao;
    }
    if (cb_IBL == 0)
        return float3(0.0, 0.0, 0.0);
    float w, h, levels;
    Cube.GetDimensions(0, w, h, levels);
    const float perceptualRoughness = sqrt(sqrt(alpha.x * alpha.y)); // alpha = roughness^2 per axis
    return Cube.SampleLevel(sampler_Cube, L, perceptualRoughness * max(0.0, levels - 1.0)).rgb * cb_IBL_intensity * pe_ssao;
}

#endif
```
  Never use the identifier `material` after the generated module is included: the module does `#define material surfaceshader`. `Lighting.hlsl` is included first, so its own `Material material` parameters are untouched.
- [ ] **Step 4: `Shaders/MaterialX/MaterialShade.slang`** (new; prototype-compiled with both viewport variants):
```slang
// Material pass 2: MaterialX's generated shading for visibility pixels of MaterialX materials, under PE's lights,
// shadows and environment (PePrelude.slang over LightingPS's bindings), written over LightOpaque's result.
// Quad-exact: each output pixel is evaluated by a 2x2 quad on its own triangle (see VisibilityDebug.slang).
#include "../Gbuffer/Lighting.hlsl"
#include "VisibilityCommon.slang"

[[vk::binding(14, 0)]] Texture2D<uint4> pe_visibility;
[[vk::binding(15, 0)]] ByteAddressBuffer pe_geometry;
[[vk::binding(16, 0)]] ByteAddressBuffer pe_frameData;
[[vk::binding(17, 0)]] StructuredBuffer<Mesh_Constants> pe_constants;
[[vk::binding(18, 0)]] ByteAddressBuffer pe_materialBytes;
#ifdef PE_VIEWPORT_RGBA8
[[vk::image_format("rgba8")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#else
[[vk::image_format("rgba16f")]][[vk::binding(19, 0)]] RWTexture2D<float4> pe_viewportOut;
#endif
// Textured graphs (Phase 3) sample these; the default module never does, so they are compiled out.
[[vk::binding(1, 2)]] Texture2D pe_textures[];
[[vk::binding(2, 2)]] SamplerState pe_samplers[];

#include "PePrelude.slang"
#include "Generated/PeOpenPBRDefault.slang"

[numthreads(8, 8, 1)]
[DerivativeGroupQuad]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    const uint2 pixel = tid.xy >> 1;
    const uint2 lane = tid.xy & 1u;
    const uint2 lanePixel = (pixel & ~1u) + lane;
    uint2 size;
    pe_visibility.GetDimensions(size.x, size.y);
    if (any(pixel >= size) || pc.shadow_debug_mode != 0u) // quad-uniform; shadow debug views stay LightingPS's
        return;
    const uint4 vis = pe_visibility[pixel];
    if (vis.x == 0u || (pe_constants[vis.x - 1u].editorFlags & PE_EDITOR_FLAG_MATERIALX) == 0u)
        return;
    const PeTriangle tri = PeLoadTriangle(pe_geometry, pe_frameData, pe_constants, pc.vertices_offset, vis);
    const PeSurfacePoint sp = PeSurfaceAt(tri, float2(lanePixel) + 0.5, float2(size));
    pe_P = sp.P;
    pe_N = sp.N;
    pe_pixel = pixel;
    pe_ssao = cb_ssao != 0 ? Ssao.SampleLevel(sampler_Ssao, (float2(pixel) + 0.5) / float2(size), 0).x : 1.0;
    pe_load_material(pe_materialBytes, pe_constants[vis.x - 1u].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET);
    pe_fill_vertex_data(sp);
    float3 color = pe_material_shade().rgb;
    if (cb_fogDensity > 0.0f) // LightingPS's distance haze, toward the blurred sky behind the pixel
    {
        const float3 V = normalize(cb_camPos.xyz - sp.P);
        const float fogAmount = 1.0f - exp(-max(length(sp.P - cb_camPos.xyz) - cb_fogStart, 0.0f) * cb_fogDensity);
        color = lerp(color, Cube.SampleLevel(sampler_Cube, -V, 2.0f).xyz, fogAmount);
    }
    if (all(lane == (pixel & 1u)))
        pe_viewportOut[pixel] = float4(color, 1.0);
}
```
  DX12 allows 4 descriptor spaces, so pass 2's own resources use set 0 bindings 14-19, after Lighting.hlsl's 0-13. Bindings the compiler strips (unused LightingPS inputs) are skipped by the `HasBinding` guards in Step 5; `Descriptor::Set*` would otherwise log `[Descriptor] Binding N not found` (`Descriptor.cpp:67`).
- [ ] **Step 5: C++ (`LightPass.h`, `LightPass.cpp`).**
  1. `LightPass.h`:
     - add `struct Define;` to the forward declarations;
     - in `class LightOpaquePass`, private section: `friend class MaterialShadePass; // reads m_uniforms and the shadow fallbacks`;
     - after `class LightTransparentPass`, add:
```cpp
    // Material pass 2 (desktop OpenPBR): MaterialX's generated shading for visibility pixels of MaterialX materials,
    // over LightOpaque's result. Shares LightOpaquePass's bindings (Lighting.hlsl sets 0-3), uniform and push
    // constants; lives in LightPass.cpp for its helpers.
    class MaterialShadePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override;
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        std::vector<Define> Defines() const;
        void BindFrame(uint32_t frame);

        Image *m_viewportRT = nullptr;
        Image *m_visibility = nullptr;
        Image *m_albedoRT = nullptr; // bound to Ssao when the SSAO target is absent, as LightOpaquePass does
        std::string m_builtDefines;
        std::vector<bool> m_boundFrames;
    };
```
  2. `LightPass.cpp`, inside the anonymous namespace, before its closing `} // namespace` (about line 309), add the shared helpers:
```cpp
        // LightingPS's compile-time configuration, shared by the opaque lighting pass and material pass 2.
        std::vector<Define> LightingDefines()
        {
            return {Define{"SHADOWMAP_CASCADES", std::to_string(Settings::Get<SceneSettings>().num_cascades)},
                    Define{"SHADOWMAP_SIZE", std::to_string((float)Settings::Get<SceneSettings>().shadow_map_size)},
                    Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)Settings::Get<SceneSettings>().shadow_map_size)},
                    Define{"FORWARD_PLUS_TILE_SIZE", std::to_string(ForwardPlusLightCullingPass::TileSize)},
                    Define{"FORWARD_PLUS_MAX_LIGHTS_PER_TILE", std::to_string(ForwardPlusLightCullingPass::MaxLightsPerTile)}};
        }

        // PushConstants_Lighting by dword index (Structures.hlsl), shared by the opaque lighting pass and pass 2.
        void SetLightingPushConstants(CommandBuffer *cmd, Image *viewport, uint32_t passType)
        {
            const auto &gSettings = Settings::Get<SceneSettings>();
            const uint32_t shadowmapCascades = gSettings.num_cascades;
            ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
            const bool shadowsAvailable = ShadowsActive(shadows);
            Scene &scene = *GetActiveScene();
            cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size()); // num point lights
            cmd->SetConstantAt(1, (uint32_t)scene.GetSpotLights().size());  // num spot lights
            cmd->SetConstantAt(2, (uint32_t)scene.GetAreaLights().size());  // num area lights
            cmd->SetConstantAt(3, 0u);                                      // padding
            cmd->SetConstantAt(4, viewport->GetWidth_f());                  // framebuffer width
            cmd->SetConstantAt(5, viewport->GetHeight_f());                 // framebuffer height
            cmd->SetConstantAt(6, passType);                                // is transparent pass
            for (uint32_t i = 0; i < shadowmapCascades; i++)
                cmd->SetConstantAt(i + 8, shadowsAvailable ? shadows.GetCascadeViewZ(i) : 0.0f); // shadowmap cascade distances
            for (uint32_t i = 0; i < shadowmapCascades; i++)
                cmd->SetConstantAt(i + 12, shadowsAvailable ? shadows.GetCascadeTexelSizeWorld(i) : 0.0f); // cascade world-space texel sizes
            cmd->SetConstantAt(16, gSettings.shadow_distance);
            cmd->SetConstantAt(17, gSettings.shadow_distance * std::clamp(gSettings.shadow_fade_fraction, 0.0f, 1.0f));
            cmd->SetConstantAt(18, gSettings.shadow_normal_bias);
            cmd->SetConstantAt(19, gSettings.shadow_filter_radius);
            cmd->SetConstantAt(20, static_cast<uint32_t>(std::clamp(gSettings.shadow_debug_mode, 0, 2)));
        }

        std::string DefinesKey(const std::vector<Define> &defines)
        {
            std::string key;
            for (const Define &define : defines)
                key += define.name + "=" + define.value + ";";
            return key;
        }
```
     Then, in `LightOpaquePass::UpdatePassInfo`, replace the `const std::vector<Define> definesFrag{…};` initializer with `const std::vector<Define> definesFrag = LightingDefines();`. In `LightOpaquePass::ExecutePass`, replace the block from `cmd->SetConstantAt(0, (uint32_t)scene.GetPointLights().size());` through `cmd->SetConstantAt(20, …);` with `SetLightingPushConstants(cmd, m_viewportRT, 0u);`. Delete only the locals that become unused (`shadowmapCascades`, `gSettings`, `shadows`, `shadowsAvailable`, `scene`), keeping the ones `AddForwardPlusReadBarriers(cmd, frame)` still uses. The opaque pass's commands must stay identical: Task 13's off-identity check proves it.
  3. `LightPass.cpp`, after `LightTransparentPass::Destroy()` (before the final `} // namespace pe`):
```cpp
    void MaterialShadePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_visibility = rs->GetRenderTarget("visibility");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_boundFrames.assign(RHII.GetSwapchainImageCount(), false);
    }

    std::vector<Define> MaterialShadePass::Defines() const
    {
        std::vector<Define> defines = LightingDefines();
        defines.push_back(Define{"PE_OPENPBR_SHADE", "1"});
        if (m_viewportRT && m_viewportRT->GetFormat() == PE_FORMAT_R8G8B8A8_UNORM)
            defines.push_back(Define{"PE_VIEWPORT_RGBA8", "1"});
        return defines;
    }

    void MaterialShadePass::UpdatePassInfo()
    {
        const std::vector<Define> defines = Defines();
        m_builtDefines = DefinesKey(defines);
        m_passInfo->name = "MaterialShade_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/MaterialShade.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = defines});
        m_passInfo->Update();
    }

    void MaterialShadePass::BindFrame(uint32_t frame)
    {
        if (frame >= m_boundFrames.size())
            return;
        m_boundFrames[frame] = false;
        auto *opaque = GetGlobalComponent<LightOpaquePass>();
        Scene *scene = GetActiveScene();
        if (!opaque || !scene || frame >= opaque->m_uniforms.size() || !opaque->m_uniforms[frame] || !m_visibility || !m_viewportRT ||
            frame >= opaque->m_shadowFallbackUniforms.size() || !scene->GetBuffer() || !scene->GetMaterialByteBuffer())
            return;

        SceneRendererHost &renderer = RequireActiveSceneRendererHost();
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        const bool useShadowResources = ShadowsActive(shadows);
        const std::vector<ImageView *> shadowViews = useShadowResources ? GetLiveShadowViews(shadows) : opaque->m_shadowFallbackViews;
        Sampler *shadowSampler = useShadowResources ? shadows.GetSampler() : opaque->m_shadowFallbackSampler;
        Image *ssao = renderer.GetRenderTarget("ssao");
        auto image = [](Descriptor *set, uint32_t binding, Image *img)
        {
            if (img && set->HasBinding(binding))
                set->SetImageView(binding, img->GetSRV(), img->GetSampler());
        };
        auto buffer = [](Descriptor *set, uint32_t binding, Buffer *b)
        {
            if (b && set->HasBinding(binding))
                set->SetBuffer(binding, b);
        };

        const auto &sets = m_passInfo->GetDescriptors(frame);
        Descriptor *set0 = sets[0];
        buffer(set0, 4, scene->GetLightUniform(frame));
        image(set0, 5, ssao ? ssao : m_albedoRT);
        buffer(set0, 7, opaque->m_uniforms[frame]); // PassUBO: the opaque pass's per-frame LightPassUBO
        image(set0, 9, renderer.GetIBL_LUT());
        buffer(set0, 10, scene->GetLightStorage(frame));
        if (set0->HasBinding(14))
            set0->SetImageView(14, m_visibility->GetSRV());
        buffer(set0, 15, scene->GetBuffer());
        buffer(set0, 16, scene->GetUniforms(frame));
        buffer(set0, 17, scene->GetMeshConstants());
        buffer(set0, 18, scene->GetMaterialByteBuffer());
        if (set0->HasBinding(19))
            set0->SetImageView(19, m_viewportRT->GetUAV(0));
        set0->Update();

        if (sets.size() > 1)
        {
            Descriptor *set1 = sets[1];
            buffer(set1, 0, useShadowResources ? shadows.GetUniform(frame) : opaque->m_shadowFallbackUniforms[frame]);
            if (set1->HasBinding(1))
                set1->SetImageViews(1, shadowViews, {});
            if (set1->HasBinding(2))
                set1->SetSampler(2, shadowSampler);
            set1->Update();
        }
        if (sets.size() > 2)
        {
            Image *cube = renderer.GetSkyBox().GetCubeMap();
            image(sets[2], 0, cube);
            sets[2]->Update();
        }
        BindProbeLighting(*m_passInfo, opaque->m_uniforms[frame], frame, true, ActiveScreenIrradiance(frame), ActiveReflections(frame));
        m_boundFrames[frame] = true;
    }

    void MaterialShadePass::UpdateDescriptorSets()
    {
        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); ++i)
            BindFrame(i);
    }

    void MaterialShadePass::Update()
    {
        // Every frame, as the light storage, shadow and GI resources it shares with LightOpaque can be replaced.
        BindFrame(RHII.GetFrameIndex());
    }

    void MaterialShadePass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility || !m_viewportRT)
            return;
        builder.ReadCompute(m_visibility);
        Image *ssao = RequireActiveSceneRendererHost().GetRenderTarget("ssao");
        if (ssao && ActivePostProcessProfile().ssao)
            builder.ReadCompute(ssao);
        ShadowPass &shadows = *GetGlobalComponent<ShadowPass>();
        if (ShadowsActive(shadows))
            for (auto *tex : shadows.GetTextures())
                builder.ReadCompute(tex);
        auto *gi = GetGlobalComponent<GlobalIlluminationPass>();
        Scene *scene = GetActiveScene();
        if (Settings::Get<SceneSettings>().global_illumination && gi && scene && scene->GetTLAS() && gi->GetIrradianceHistory())
        {
            builder.ReadCompute(gi->GetIrradianceHistory());
            builder.ReadCompute(gi->GetDistanceHistory());
        }
        if (Image *screen = ActiveScreenIrradiance(RHII.GetFrameIndex()))
            builder.ReadCompute(screen);
        if (Image *reflections = ActiveReflections(RHII.GetFrameIndex()))
            builder.ReadCompute(reflections);
        builder.WriteCompute(m_viewportRT); // keeps LightOpaque's result: only MaterialX pixels are written
    }

    void MaterialShadePass::DeclareOutputs(RGBuilder &builder)
    {
        if (m_viewportRT && m_visibility)
            builder.OutputCustom(m_viewportRT, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void MaterialShadePass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();
        if (!scene || !m_visibility || !m_viewportRT || frame >= m_boundFrames.size() || !m_boundFrames[frame])
            return;
        SetLightingPushConstants(cmd, m_viewportRT, 0u);
        cmd->SetConstantAt(21, static_cast<uint32_t>(scene->GetVerticesOffset())); // vertices_offset (PE_OPENPBR_SHADE)
        cmd->BeginDebugRegion("MaterialShadePass");
        cmd->BindPipeline(*m_passInfo);
        cmd->PushConstants();
        cmd->Dispatch((m_visibility->GetWidth() * 2u + 7u) / 8u, (m_visibility->GetHeight() * 2u + 7u) / 8u, 1);
        cmd->EndDebugRegion();
    }

    void MaterialShadePass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();
        // HDR (viewport format) and the shadow settings change the shader: rebuild (only graphics passes are rebuilt for us).
        if (m_passInfo->pCompShader && DefinesKey(Defines()) != m_builtDefines)
        {
            m_passInfo->DestroyShaders();
            UpdatePassInfo();
        }
        UpdateDescriptorSets();
    }

    void MaterialShadePass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        m_viewportRT = m_visibility = m_albedoRT = nullptr;
        m_boundFrames.clear();
    }
```
     `LightPass.cpp` already includes `GlobalIlluminationPass.h`, `ReflectionPass.h`, `ScreenProbePass.h`, `ShadowPass.h`, `Skybox/Skybox.h`, `Render/SceneRenderTargets.h` and `ForwardPlusLightCullingPass.h`.
- [ ] **Step 6: Register** `MaterialShade` (order 510) in `SceneRenderGraph.{h,cpp}`, the 8 places as in Task 10 Step 5:
  - the enum value `MaterialShade, // openpbr: material pass 2 (MaterialX shading)`;
  - the component `materialShade`;
  - the row `{SceneRenderGraphPassId::MaterialShade, 510, "MaterialShade", &SceneRenderGraphPassComponents::materialShade},` (no new include: `LightPass.h` is included);
  - the `ShouldUpdate` branch;
  - `CreateSceneRenderGraphPassComponent<MaterialShadePass>(renderPassComponents);`;
  - `scenePasses.materialShade = GetGlobalComponent<MaterialShadePass>();`;
  - `SetPassEnabled(…, SceneRenderGraphPassId::MaterialShade, SceneUsesOpenPBR() && GetOpenPBRDefaultLayout().valid && dx12RenderRaster)` in the DX12 branch and `SceneUsesOpenPBR() && GetOpenPBRDefaultLayout().valid && renderRaster` in the Vulkan path.
- [ ] **Step 7:** Build, run `openpbr_vis.py` on both APIs (expected: pass), read the log (no `Binding N not found`, no validation messages), run `clang-format`.
  Amended 2026-10-14 after Task 11: on a cold shader cache, `MaterialShade` (the whole MaterialX module) compiles on the main thread longer than the editor's 10 s Lua wait (`EditorToolRuntime.cpp`), so the first Lua call after enabling `openpbr` timed out. The pool reruns this test after shader edits, exactly when the cache is cold. The orchestrator added a warm-up session to `openpbr_vis.py`, called in `main` right before the first `editor_scene` (`warm_openpbr_shaders(a.api)`). Measured from a cold MaterialShade cache on Vulkan: 39 s for the whole session, one filtered timeout.
```python
def warm_openpbr_shaders(api):
    # A cold shader cache (after any shader edit) compiles MaterialShade, the whole MaterialX module, on the main thread
    # for longer than the editor's 10 s Lua wait. Fill the cache in a session of its own, so the checked sessions never
    # time out. Its log may hold only this wait's own execute_lua timeouts.
    with tempfile.TemporaryDirectory(prefix="pe-openpbr-warm-") as d:
        assets = Path(d) / "Assets"; (assets / "Agent").mkdir(parents=True)
        (assets / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
        with editor_scene(api, assets):
            lua('settings.set("openpbr",true) settings.set("openpbr_debug",true) return "ok"')
            deadline = time.time() + 300
            while not (tool("execute_lua", {"code": 'return "ready"'}).get("output") == "ready"):
                assert time.time() < deadline, "OpenPBR shaders still compiling after 300 s"
                time.sleep(2)
    bad = [l for l in log_problems() if "<- execute_lua: error" not in l]
    assert not bad, "\n".join(bad[:20])
```
- [ ] **Step 7b: Prove the finite check can fail.** This is local and never kept.
  1. Temporarily add `radiance = asfloat(0x7fc00000u).xxx;` (NaN) at the end of `pe_light_sample`'s directional branch, before its `return`.
  2. Run `openpbr_vis.py --only material_pass2`. Expected: FAIL on "non-finite or saturated shading".
  3. Revert, run it again (expected: pass), and record both runs.

### Task 12: Direct-light match against MaterialX's own renderer

**Files:**
- Create `.testpool/tests/mtlx_reference.py` (it runs in `.testpool/venv-mtlx`).
- Add `check_direct_light_match` to `openpbr_vis.py`.

**What it proves.** With one directional light, no environment and no shadows, PE's pass-2 output equals the output of MaterialX 1.39.5's own GLSL renderer (`GlslRenderer`, FLOAT target) **pixel for pixel** for the same geometry, camera and light, within 2/255 (linear, each pixel clamped and rounded the same way).
- **Fixtures:**
  - PE's sphere primitive (radius 1, 32 × 32, reproduced vertex for vertex as an OBJ) for every case except anisotropy. The curvature exercises MaterialX's `fwidth`-based subsurface term and the quad-exact derivatives.
  - The plane primitive for `default` and `aniso`. The plane exercises the UV-tangent fallback, where PE's per-triangle dP/du equals MaterialX's generated tangent exactly. A curved surface would compare per-vertex averaged tangents against per-triangle ones.
- **Coverage:** the generator, the overrides, the light loop, ABI values (non-default inputs of every type), vertex data, the tangent fallback, derivatives and the light units.

- Amended 2026-10-14 after Task 12's first run: with `SPECULAR_ENVIRONMENT_NONE`, the reference failed in `createProgram` with `error C1503: undefined variable "u_refractionTwoSided"`. MaterialX 1.39.5's `HwShaderGenerator.cpp:199-208` declares that uniform only for environment methods other than NONE, but `mx_transmission_refract.glsl` reads it unconditionally. The reference now uses `SPECULAR_ENVIRONMENT_FIS` with `setEnvSampleCount(0)` and `setIndirectLighting(False)`, which binds MaterialX's zero image for both environment maps: every environment term stays exactly 0 and the shader compiles. The orchestrator checked it renders the default OpenPBR document. Step 0's venv was created offline by the orchestrator from the approved local wheel.
- [ ] **Step 0: Environment.** Confirm the approval record allows the `pip install`; otherwise **STOP**. Then:
```bash
py -3.12 -m venv .testpool/venv-mtlx
.testpool/venv-mtlx/Scripts/python.exe -m pip install MaterialX==1.39.5
.testpool/venv-mtlx/Scripts/python.exe -c "import MaterialX, MaterialX.PyMaterialXRenderGlsl as r; print(MaterialX.getVersionString())"
```
  Expected: `1.39.5`. `.testpool/` is gitignored.
- [ ] **Step 1: Reference script** `.testpool/tests/mtlx_reference.py`:
```python
"""MaterialX 1.39.5's own GLSL renderer as the OpenPBR reference: renders each case's open_pbr_surface on a copy of PE's
sphere or plane primitive under one directional light (no environment, no shadows) from PE's camera, and prints, per
fixture and case, the linear colour of every pixel in a centre window, in PE's top-origin row order, as JSON.
Run with .testpool/venv-mtlx (Python 3.12, MaterialX==1.39.5). Usage: mtlx_reference.py <spec.json>"""
import json, math, sys, tempfile
from pathlib import Path
import MaterialX as mx
import MaterialX.PyMaterialXGenShader as mx_gen_shader
import MaterialX.PyMaterialXGenGlsl as mx_gen_glsl
import MaterialX.PyMaterialXRender as mx_render
import MaterialX.PyMaterialXRenderGlsl as mx_render_glsl

TYPE = {"float": "float", "float3": "color3", "bool": "boolean"}


def plane_obj():
    # Primitives::CreatePlane: 10 x 10 on XZ at y = 0, normal +Y, u along +X, v along +Z, CCW seen from +Y.
    return "v -5 0 -5\nv -5 0 5\nv 5 0 5\nv 5 0 -5\nvt 0 0\nvt 0 1\nvt 1 1\nvt 1 0\nvn 0 1 0\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"


def sphere_obj(radius=1.0, slices=32, stacks=32):
    # Primitives::CreateUvSphere (Primitives.cpp:442-499): same vertices, normals, UVs (u mirrored), triangles p0 p1 p2 / p1 p3 p2,
    # oriented outward; degenerate pole triangles are dropped (zero area, no pixels).
    v, n, t, faces = [], [], [], []
    for i in range(stacks + 1):
        phi = i / stacks * math.pi
        for j in range(slices + 1):
            theta = j / slices * 2.0 * math.pi
            x, y, z = math.cos(theta) * math.sin(phi), math.cos(phi), math.sin(theta) * math.sin(phi)
            v.append((x * radius, y * radius, z * radius)); n.append((x, y, z)); t.append((1.0 - j / slices, i / stacks))
    def tri(a, b, c):
        pa, pb, pc = v[a], v[b], v[c]
        e1 = [pb[k] - pa[k] for k in range(3)]; e2 = [pc[k] - pa[k] for k in range(3)]
        cr = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(c_ * c_ for c_ in cr) < 1e-14:
            return
        centre = [(pa[k] + pb[k] + pc[k]) / 3.0 for k in range(3)]
        faces.append((a, b, c) if sum(cr[k] * centre[k] for k in range(3)) > 0 else (a, c, b))
    for i in range(stacks):
        for j in range(slices):
            p0 = i * (slices + 1) + j; p1 = p0 + 1; p2 = (i + 1) * (slices + 1) + j; p3 = p2 + 1
            tri(p0, p1, p2); tri(p1, p3, p2)
    lines = [f"v {a:.9g} {b:.9g} {c:.9g}" for a, b, c in v] + [f"vt {a:.9g} {b:.9g}" for a, b in t] + [f"vn {a:.9g} {b:.9g} {c:.9g}" for a, b, c in n]
    lines += [f"f {a + 1}/{a + 1}/{a + 1} {b + 1}/{b + 1}/{b + 1} {c + 1}/{c + 1}/{c + 1}" for a, b, c in faces]
    return "\n".join(lines) + "\n"


def value_string(kind, value):
    if kind == "bool":
        return "true" if value else "false"
    return ", ".join(f"{v:.9g}" for v in (value if isinstance(value, list) else [value]))


def material_xml(inputs):
    lines = "".join(f'    <input name="{name}" type="{TYPE[kind]}" value="{value_string(kind, value)}"/>\n'
                    for name, (kind, value) in inputs.items())
    return ('<?xml version="1.0"?>\n<materialx version="1.39">\n  <open_pbr_surface name="ref_surface" type="surfaceshader">\n'
            + lines + '  </open_pbr_surface>\n  <surfacematerial name="RefMaterial" type="material">'
            '<input name="surfaceshader" type="surfaceshader" nodename="ref_surface"/></surfacematerial>\n</materialx>\n')


def main():
    spec = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    w, h, k = spec["width"], spec["height"], spec["window"]
    search = mx.getDefaultDataSearchPath()
    lib = mx.createDocument()
    mx.loadLibraries(mx.getDefaultDataLibraryFolders(), search, lib)

    renderer = mx_render_glsl.GlslRenderer.create(w, h, mx_render.BaseType.FLOAT)
    renderer.initialize()
    renderer.setImageHandler(mx_render_glsl.GLTextureHandler.create(mx_render.StbImageLoader.create()))
    geometry = renderer.getGeometryHandler()
    geometry.addLoader(mx_render.TinyObjLoader.create())
    near, far = 0.05, 1000.0
    right = near * math.tan(math.radians(spec["fovx"]) / 2)
    top = right * h / w

    lights = mx.createDocument()
    lights.importLibrary(lib)
    sun = lights.addNode("directional_light", "pe_sun", "lightshader")
    sun.setInputValue("direction", mx.Vector3(*spec["direction"]))
    sun.setInputValue("color", mx.Color3(1.0, 1.0, 1.0))
    sun.setInputValue("intensity", float(spec["intensity"]))

    results = {}
    with tempfile.TemporaryDirectory() as d:
        for fixture_name, fixture in spec["fixtures"].items():
            obj = Path(d) / f"pe_{fixture_name}.obj"
            obj.write_text(sphere_obj() if fixture_name == "sphere" else plane_obj(), encoding="utf-8")
            geometry.clearGeometry()
            assert geometry.loadGeometry(mx.FilePath(str(obj)), False), f"{fixture_name}: OBJ did not load"
            camera = renderer.getCamera()
            camera.setViewMatrix(mx_render.Camera.createViewMatrix(mx.Vector3(*fixture["eye"]), mx.Vector3(*fixture["target"]), mx.Vector3(0, 1, 0)))
            camera.setProjectionMatrix(mx_render.Camera.createPerspectiveMatrix(-right, right, -top, top, near, far))
            results[fixture_name] = {}
            for name, case in fixture["cases"].items():
                doc = mx.createDocument()
                mx.readFromXmlString(doc, material_xml(case["inputs"]))
                doc.setDataLibrary(lib)
                valid, message = doc.validate()
                assert valid, f"{name}: {message}"
                gen = mx_gen_glsl.GlslShaderGenerator.create()
                ctx = mx_gen_shader.GenContext(gen)
                ctx.registerSourceCodeSearchPath(search)
                options = ctx.getOptions()
                options.shaderInterfaceType = mx_gen_shader.ShaderInterfaceType.SHADER_INTERFACE_COMPLETE
                # Not SPECULAR_ENVIRONMENT_NONE: MaterialX 1.39.5 then omits u_refractionTwoSided, which
                # mx_transmission_refract.glsl still reads (GLSL C1503). FIS with zero samples and the zero environment
                # images (setIndirectLighting(False)) makes every environment term exactly 0.
                options.hwSpecularEnvironmentMethod = mx_gen_shader.HwSpecularEnvironmentMethod.SPECULAR_ENVIRONMENT_FIS
                options.targetColorSpaceOverride = "lin_rec709"
                cms = mx_gen_shader.DefaultColorManagementSystem.create(gen.getTarget())
                cms.loadLibrary(lib)
                gen.setColorManagementSystem(cms)
                handler = mx_render.LightHandler.create()
                handler.setIndirectLighting(False)
                handler.setEnvSampleCount(0)
                handler.registerLights(lights, [sun], ctx)
                handler.setLightSources([sun])
                shader = gen.generate("pe_reference", mx_gen_shader.findRenderableElements(doc)[0], ctx)
                renderer.setLightHandler(handler)
                renderer.createProgram(shader)
                renderer.validateInputs()
                renderer.render()
                image = renderer.captureImage(None)
                rows = []
                for y in range(h // 2 - k, h // 2 + k + 1):  # PE rows (top origin) -> GL rows (bottom origin, GLFramebuffer.cpp:168)
                    for x in range(w // 2 - k, w // 2 + k + 1):
                        c = image.getTexelColor(x, h - 1 - y)
                        rows.append([c[0], c[1], c[2]])
                results[fixture_name][name] = rows
    print(json.dumps(results))


if __name__ == "__main__":
    main()
```
  **Binding names are from MaterialX 1.39.5's `source/PyMaterialX`** (read 2026-10-13). The script has not been run, because the wheel was not installed when this plan was written.
  - If a call fails, read the binding in `.fetchcontent-ninja/materialx-src/source/PyMaterialX/` and fix the call, keeping the geometry, camera, light, options, window and row mapping. Record every change. Examples: `captureImage` takes no argument; `validate()` returns a bool and a message differently; `findRenderableElements` lives in another module.
  - If `renderer.initialize()` cannot create an OpenGL context, **STOP**.
- [ ] **Step 2: The check.** Add to `openpbr_vis.py` and append to `CHECKS`:
```python
VENV_PY = ROOT / ".testpool/venv-mtlx/Scripts/python.exe"
SPHERE_CASES = {  # non-default inputs of every type in the default module (float, float3, bool)
    "default": {},
    "base": {"base_color": [0.7, 0.3, 0.2], "base_weight": 0.9, "specular_roughness": 0.45, "base_diffuse_roughness": 0.4},
    "specular": {"specular_weight": 0.6, "specular_ior": 1.8, "specular_color": [1.0, 0.9, 0.8]},
    "metal": {"base_metalness": 1.0, "base_color": [0.9, 0.6, 0.4], "specular_color": [0.95, 0.9, 0.85], "specular_roughness": 0.35},
    "coat": {"coat_weight": 1.0, "coat_color": [0.9, 0.8, 0.6], "coat_roughness": 0.15, "coat_ior": 1.6, "coat_darkening": 1.0},
    "fuzz": {"fuzz_weight": 0.8, "fuzz_color": [0.6, 0.7, 0.9], "fuzz_roughness": 0.4},
    "thinfilm": {"thin_film_weight": 1.0, "thin_film_thickness": 0.45, "thin_film_ior": 1.4},
    "subsurface": {"subsurface_weight": 1.0, "subsurface_color": [0.8, 0.4, 0.3], "subsurface_radius": 0.5},
    "thinwalled": {"subsurface_weight": 1.0, "subsurface_color": [0.8, 0.4, 0.3], "geometry_thin_walled": True},
}
PLANE_CASES = {"default": {}, "aniso": {"specular_roughness": 0.4, "specular_roughness_anisotropy": 0.7}}
FIXTURES = {"sphere": ("sphere", [0, 1, 3], SPHERE_CASES), "plane": ("plane", [0, 3, 6], PLANE_CASES)}


def encode(linear):  # EditorToolRuntime.cpp ToByte: clamp to [0, 1], then floor(v * 255 + 0.5)
    return np.floor(np.clip(np.asarray(linear, np.float64), 0.0, 1.0) * 255.0 + 0.5)


def pixel_mismatch(pe, ref):
    """Largest per-pixel, per-channel difference of identically encoded pixels (no averaging)."""
    return float(np.abs(np.asarray(pe, np.float64) - encode(ref)).max())


def lua_value(kind, value):
    if kind == "bool":
        return "true" if value else "false"
    return f"vec3({value[0]},{value[1]},{value[2]})" if kind == "float3" else repr(float(value))


def check_direct_light_match():
    # Negative control first: zero-mean +-10/255 errors must fail the per-pixel gate (an averaged gate would pass them).
    probe = np.full((121, 3), 0.5)
    checker = np.where((np.arange(121) % 2 == 0)[:, None], 10.0, -10.0) * np.ones((1, 3))
    checker[-1] = 0.0  # 60 at +10, 60 at -10, one unchanged: mean exactly zero
    assert abs((encode(probe) + checker - encode(probe)).mean()) < 1e-9 and pixel_mismatch(encode(probe) + checker, probe) == 10.0

    assert VENV_PY.exists(), "run Task 12 Step 0 (MaterialX venv) first"
    layout = {i["input"]: i for i in json.loads((GENERATED / "PeOpenPBRDefault.json").read_text(encoding="utf-8"))["inputs"]}
    defaults = {name: (i["type"], i["default"] if i["type"] == "float3" else (bool(i["default"][0]) if i["type"] == "bool" else i["default"][0]))
                for name, i in layout.items()}
    new_scene()
    lua('settings.set("openpbr",true) settings.set("linear_color",true) settings.set("taa",false) settings.set("render_scale",1) '
        'settings.set("IBL",false) settings.set("global_illumination",false) settings.set("ssao",false) settings.set("shadows",false) '
        'settings.set("fog",false) settings.set("bloom",false) settings.set("ssr",false) settings.set("draw_grid",false) '
        'local c=lights.get_counts() '
        'for i=c.point,1,-1 do lights.remove_point(i-1) end for i=c.spot,1,-1 do lights.remove_spot(i-1) end '
        'for i=c.area,1,-1 do lights.remove_area(i-1) end for i=c.directional,2,-1 do lights.remove_directional(i-1) end '
        'if c.directional==0 then scene.add_directional_light() end '
        'lights.set_directional_light(0, vec3(0,10,0), vec3(1,1,1), 2.0) return "ok"')
    sun = lua('return lights.get_directional_lights()[1].name')
    tool("set_node_transform", {"node": sun, "rotation": [-55, 25, 0]}); time.sleep(2)
    q = [float(v) for v in lua('local r=lights.get_directional_lights()[1].rotation return string.format("%.9g,%.9g,%.9g,%.9g", r.x, r.y, r.z, r.w)').split(",")]
    qv, qw = np.array(q[:3]), q[3]
    t = 2.0 * np.cross(qv, [0.0, 0.0, -1.0])
    direction = (np.array([0.0, 0.0, -1.0]) + qw * t + np.cross(qv, t)).tolist()  # RotateVectorByQuat((0,0,-1), q)
    fovx, window, pe, spec_fixtures = 60.0, 5, {}, {}

    for fixture, (primitive, eye, cases) in FIXTURES.items():
        lua('if _G.pe_match_node then scene.delete_node(_G.pe_match_node) end '
            f'local n=scene.add_empty_node("Match") scene.attach_primitive(n,"{primitive}") material.use_openpbr(n,true) _G.pe_match_node=n return "ok"')
        tool("set_camera", {"position": eye, "target": [0, 0, 0], "projection": "perspective", "fov_x_deg": fovx}); time.sleep(4)
        current, pe[fixture] = dict(defaults), {}
        for name, inputs in cases.items():
            wanted = dict(defaults)
            wanted.update({k: (layout[k]["type"], v) for k, v in inputs.items()})
            sets = " ".join(f'material.set_openpbr(_G.pe_match_node,"{k}",{lua_value(kind, v)})'
                            for k, (kind, v) in wanted.items() if current.get(k) != (kind, v))
            lua(sets + ' return "ok"'); time.sleep(3)
            current = wanted
            r = tool("capture_image_resource", {"id": "rt:viewport", "max_width": 16384, "visualize": "linear"}); assert not r.get("error"), r
            img = np.asarray(Image.open(r["path"]).convert("RGB"), np.float64)
            h, w = img.shape[:2]
            pe[fixture][name] = img[h // 2 - window:h // 2 + window + 1, w // 2 - window:w // 2 + window + 1].reshape(-1, 3)
        spec_fixtures[fixture] = {"eye": eye, "target": [0, 0, 0],
                                  "cases": {n: {"inputs": {k: [layout[k]["type"], v] for k, v in i.items()}} for n, i in cases.items()}}

    spec = {"width": w, "height": h, "window": window, "fovx": fovx, "direction": direction, "intensity": 2.0, "fixtures": spec_fixtures}
    with tempfile.TemporaryDirectory(prefix="pe-mtlx-ref-") as d:
        path = Path(d) / "spec.json"; path.write_text(json.dumps(spec), encoding="utf-8")
        out = subprocess.run([str(VENV_PY), str(ROOT / ".testpool/tests/mtlx_reference.py"), str(path)], capture_output=True, text=True, timeout=900)
        assert out.returncode == 0, out.stdout[-3000:] + out.stderr[-3000:]
        ref = json.loads(out.stdout.strip().splitlines()[-1])
    failures = []
    for fixture, cases in pe.items():
        for name, pixels in cases.items():
            r = np.array(ref[fixture][name])
            assert np.isfinite(r).all() and (r >= 0).all() and (r < 0.95).all(), f"{fixture}/{name}: reference {r.max(0)} near the clamp; change the light, not the tolerance"
            worst = pixel_mismatch(pixels, r)
            print(f"  {fixture}/{name}: worst pixel diff {worst:.0f}/255, mean pe {pixels.mean(0).round(1)} ref {encode(r).mean(0).round(1)}")
            if worst > 2.0:
                failures.append(f"{fixture}/{name}: {worst:.0f}/255")
    assert not failures, "MaterialX direct-light mismatch: " + ", ".join(failures)
```
  - **Same pixels on both sides:** same resolution, same horizontal FOV, aspect `h/w`, a symmetric frustum (TAA off, so no jitter), the same eye/target with world-up `+Y`, and the GL row flip in the reference.
  - **Same encoding on both sides:** clamp, then `floor(v × 255 + 0.5)` per pixel. PE's PNG holds exactly that for the RGBA16F viewport (`ConvertImageRowsToRGBA`, `visualize="linear"`); the reference encodes its floats identically.
- [ ] **Step 3: Run on both APIs.** Expected: every pixel of every case ≤ 2/255. On failure, diagnose in this order; if still unresolved, **STOP** and report the per-case table:
  1. `default` fails on both fixtures by a constant factor: light units (`pe_light_sample` radiance), or `linear_color` colour decoding of the light.
  2. `default` passes and only non-default cases fail: the material ABI (loader offsets vs `BuildOpenPBRBytes`). Compare against the `.json` offsets.
  3. Only `plane/aniso` fails: the tangent frame (`PeSurfaceAt`'s UV-tangent fallback vs MaterialX's generated tangent).
  4. Only `sphere/subsurface` fails: derivatives (`fwidth(N)/fwidth(P)`): check the quad lane mapping.
  5. Errors grow away from the window centre: the camera (fovx, view matrix, row flip, a horizontal mirror). Compare the centre pixel alone to separate a geometry error from a shading one.

  Record the per-case table in the report, for both APIs.

### Task 13: Phase 2 close — identity, perf, docs

- [ ] **Step 1: On without MaterialX is invisible.** Add to `openpbr_vis.py`, append to `SPONZA_CHECKS`, and run:
```python
def check_on_without_materialx_identity():
    # Sponza has no MaterialX material: with openpbr on, every new pass must leave the frame byte-identical.
    lua('settings.set("global_illumination",false) settings.set("taa",false) settings.set("render_scale",1) '
        'settings.set("openpbr",false) settings.set("openpbr_debug",false) return "ok"'); time.sleep(6)
    from openpbr_baseline import full_shot
    off = np.asarray(full_shot(), np.int16)
    setting("openpbr", True, 6)
    assert_settles_to(off, "openpbr on (no MaterialX materials) changed the frame")  # Task 10 amendment: settle, then exact
    setting("openpbr", False, 3)
```
  Amended 2026-10-14 before Task 13 ran: turning `openpbr` on rebuilds the targets just as the toggle does, so this check uses Task 10's `assert_settles_to` (exact identity, polled for up to 20 s) instead of one capture after a fixed wait.
  Expected: identical. A difference means the prepass, the G-buffer attachment, LightOpaque's helper refactor (Task 11 Step 5) or a material pass is visible: **STOP** and bisect.
- [ ] **Step 2: Off-identity against the Task 0 baseline** (`check_off_identity`, both APIs) and the full `openpbr_vis.py`, `mtlx_codegen.py`, `cooked-clusters-*`, `linear-color-*`, `depth-equal-positions`, `renderer-features-shaders` suites. Expected: all pass.
- [ ] **Step 3: Perf** (`perf/openpbr/phase2-{off,on}/`). Use the Task 0 Step 3 procedure: the editor check, `tools/perf_cycle.sh Release C:/Users/Christos/repos/PhasmaEngine/build-ninja-physics`, and PID-matched cleanup.
  - Sponza with `openpbr` off vs the baseline must be within the thresholds.
  - Sponza with `openpbr` on: report it.
  - A 100-plane MaterialX scene with `openpbr` on: report it. Cost is reported, not gated (spec §Performance).
- [ ] **Step 4: Android rebake** (`powershell -File tools/bake_android_shaders.ps1 -BuildDir build-ninja-physics`). `LightingPS` and `Structures.hlsl` changed only under defines Android never sets, so the bake must be unchanged apart from the hash of files that include `Structures.hlsl`.
- [ ] **Step 5: Docs.**
  - Update the spec's §Material passes to Phase 2's pinned decisions:
    - the pass-1 approach and targets (velocity and transparency stay the raster's, emissive 0);
    - the environment-radiance rule;
    - the area-light centre point;
    - the override layout `pe_overrides/libraries/…`.
  - Update "OpenPBR Materials" in `docs/wiki/architecture/rendering.md`:
    - what Phase 2 shades and how (byte layout +80, editorFlags bit 32, Lua API);
    - the match-test results table;
    - the costs;
    - the stated limits (no persistence, untextured only, RT hits use legacy mirrors).
  - Run `bash docs/wiki/tools/lint.sh`.
- [ ] **Step 6: Final log review** on both APIs. Leave everything unstaged, and report the full file list (`git status --short`).

### Task 14: Review fixes (added 2026-10-14 after the whole-change review)

A read-only review (Astra, `gpt-6-astra` xhigh) of the complete Phase 0-2 change found six defects. The orchestrator checked each one against the source. Fix exactly these, run every check, and do not touch anything else. Fix 7 (exported games and `slangc`) waits for the user's choice and is not part of this task.

**Files:**
- `Phasma/Runtime/Code/RenderPasses/LightPass.{h,cpp}` (Fix 1)
- `Phasma/Runtime/RuntimeAssets/Shaders/MaterialX/VisibilityCommon.slang`, `MaterialThin.slang`, `MaterialShade.slang` (Fix 2)
- `cmake/PhasmaRuntimeLayout.cmake` (Fix 3)
- `Phasma/Runtime/Code/Scene/OpenPBRMaterial.cpp` (Fix 4)
- root `CMakeLists.txt` (Fix 5)
- `Phasma/Cook/Code/MaterialX/MaterialXGen.cpp` (Fix 6)
- Tests: `.testpool/tests/openpbr_vis.py`, `.testpool/tests/mtlx_codegen.py`

- [ ] **Fix 1: MaterialShade must not read LightOpaque's shadow fallbacks during the parallel update.**
  `UpdateSceneRenderGraphPassComponents` (`SceneRenderGraph.cpp:608-620`) runs every pass's `Update` on `ThreadPool::Update` at once. `LightOpaquePass::Update` can call `UpdateDescriptorSets`, which runs `EnsureLightShadowFallbackResources`. That function always does `views.assign(...)` and can resize the uniforms. Meanwhile `MaterialShadePass::BindFrame` copies `opaque->m_shadowFallbackViews` and indexes `opaque->m_shadowFallbackUniforms`, which is a data race. (`opaque->m_uniforms` is written only in `LightOpaquePass::Init`, never during `Update`, so reading it stays.)
  - `LightPass.h`, in `MaterialShadePass`'s private members, add:
```cpp
        // Its own shadow fallbacks: LightOpaquePass rewrites its copies in its Update, which runs in parallel.
        std::vector<Buffer *> m_shadowFallbackUniforms;
        Image *m_shadowFallbackTexture = nullptr;
        Sampler *m_shadowFallbackSampler = nullptr;
        std::vector<ImageView *> m_shadowFallbackViews;
```
  - Change the friend comment in `LightOpaquePass` to `// reads m_uniforms (written only in Init)`.
  - In `MaterialShadePass::BindFrame`, after `m_boundFrames[frame] = false;`, call `EnsureLightShadowFallbackResources(m_shadowFallbackUniforms, m_shadowFallbackTexture, m_shadowFallbackSampler, m_shadowFallbackViews, "MaterialShade");`.
  - In `BindFrame`, replace each `opaque->m_shadowFallbackUniforms`, `opaque->m_shadowFallbackViews` and `opaque->m_shadowFallbackSampler` with the pass's own member. That includes the early-return guard `frame >= opaque->m_shadowFallbackUniforms.size()`.
  - In `MaterialShadePass::Destroy`, add `DestroyLightShadowFallbackResources(m_shadowFallbackUniforms, m_shadowFallbackTexture, m_shadowFallbackSampler, m_shadowFallbackViews);`.
  - Afterwards, `grep -n "opaque->m_shadowFallback" LightPass.cpp` must return nothing.

- [ ] **Fix 2: reconstruct without dividing by a vertex's w, and never write a non-finite result.**
  `PeScreenBarycentrics` divides each clip position by its own w. A vertex exactly on the camera plane gives inf/NaN, and a vertex near it loses precision. Large triangles near the camera (floors) hit this. Negative w is fine today and must stay fine.
  - In `VisibilityCommon.slang`, replace `PeCross2`, `PeScreenBarycentrics`, `PePerspective` and `PeNdcDepth` (no other file uses them) with:
```hlsl
// Perspective-correct barycentrics straight from clip space (homogeneous rasterisation, Olano & Greer 1997): the
// weights b, summing to 1, whose clip point sum(b_i * c_i) projects to ndc. No per-vertex 1/w, so vertices on or
// behind the camera plane stay finite; only an edge-on triangle (never rasterised) is singular.
float3 PeClipBarycentrics(float4 c0, float4 c1, float4 c2, float2 ndc)
{
    const float3 u = float3(c0.x - ndc.x * c0.w, c1.x - ndc.x * c1.w, c2.x - ndc.x * c2.w);
    const float3 v = float3(c0.y - ndc.y * c0.w, c1.y - ndc.y * c1.w, c2.y - ndc.y * c2.w);
    const float3 n = cross(u, v);
    return n / (n.x + n.y + n.z);
}
```
  - In `PeSurfaceAt`, replace the two lines that compute `l` and `b` with `const float3 b = PeClipBarycentrics(t.c0, t.c1, t.c2, PeNdcFromPixel(pixelCentre, size));`. Replace the last two lines before `return sp;` with:
```hlsl
    const float clipW = dot(b, float3(t.c0.w, t.c1.w, t.c2.w));
    sp.svPosition = float4(pixelCentre, dot(b, float3(t.c0.z, t.c1.z, t.c2.z)) / clipW, 1.0 / clipW);
```
  - In `MaterialThin.slang`, right after `if (!all(lane == (pixel & 1u))) return;` (the derivative work is done by then), add:
```hlsl
    // Never overwrite the raster G-buffer with a non-finite result: keep its value for this texel.
    if (any(isnan(thin.normal)) || any(isinf(thin.normal)) || any(isnan(thin.albedo)) || any(isinf(thin.albedo)) ||
        isnan(thin.roughness) || isinf(thin.roughness) || isnan(thin.metalness) || isinf(thin.metalness))
        return;
```
  - In `MaterialShade.slang`, make the final write `if (all(lane == (pixel & 1u)) && !any(isnan(color)) && !any(isinf(color)))` (a non-finite result keeps LightOpaque's value).
  - **Test** (add to `openpbr_vis.py`, append to `CHECKS`):
```python
def check_camera_plane_reconstruction():
    # Fix 2: a plane whose near edge lies exactly on the camera plane (w = 0 for those vertices) must reconstruct
    # finite, exact-depth pixels; the old per-vertex 1/w made them inf/NaN.
    new_scene()
    lua('settings.set("taa",false) local p=scene.add_empty_node("Floor") scene.attach_primitive(p,"plane") '
        'p:set_position(vec3(0,0,-10)) p:set_scale(vec3(20,1,20)) scene.add_directional_light() '
        'settings.set("openpbr",true) settings.set("openpbr_debug",true) return "ok"')
    tool("set_camera", {"position": [0, 1, 0], "target": [0, 1, -5], "projection": "perspective"}); time.sleep(8)
    assert_reconstruction("plane with vertices on the camera plane")
    lua('settings.set("openpbr",false) settings.set("openpbr_debug",false) return "ok"')
```
    Check `Scene/Primitives.cpp` for the plane primitive's extent and axis. Pick the position and scale so that the edge nearest the camera has vertices at z = 0 exactly (the camera's z), lying flat at y = 0 under a camera looking along -Z. Report the vertices you used. **Prove it can fail:** run the check once against the old reconstruction (keep a byte copy of `VisibilityCommon.slang` before Fix 2, swap it in, build, run, restore the bytes). It must fail, either non-finite or with a depth code below 100. Record both runs.

- [ ] **Fix 3: install the generated module and the cook's MaterialX data.** In `cmake/PhasmaRuntimeLayout.cmake`, inside `pe_install_runtime_layout()`, right after the `if(TARGET PhasmaCook) install(TARGETS PhasmaCook ...) endif()` block, add:
```cmake
    # OpenPBR: the build-time default module goes beside the installed runtime assets (installed under Assets/), and
    # the installed PhasmaCook --mtlx reads MaterialX's library data and PE's overrides from <exe>/MaterialX.
    if(TARGET PeOpenPBRDefaultModule)
        install(FILES "${PHASMA_CONFIG_OUTPUT_DIR}/RuntimeAssets/Shaders/MaterialX/Generated/PeOpenPBRDefault.slang"
                      "${PHASMA_CONFIG_OUTPUT_DIR}/RuntimeAssets/Shaders/MaterialX/Generated/PeOpenPBRDefault.json"
            DESTINATION "Assets/Shaders/MaterialX/Generated"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}")
    endif()
    if(TARGET PhasmaCook AND TARGET MaterialXGenSlang)
        install(DIRECTORY "${PE_MATERIALX_LIBRARIES_DIR}/" DESTINATION "MaterialX/libraries"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}")
        install(DIRECTORY "${PE_RUNTIME_ASSETS_SOURCE_DIR}/MaterialX/pe_overrides/" DESTINATION "MaterialX/pe_overrides"
            COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}")
    endif()
```
  **Test:** run `cmake --install build-ninja-physics --config Release --component Runtime --prefix <a new folder under %TEMP%>`. Confirm that the installed runtime finds runtime assets under `Assets/`: read `Path::Init` and report what you found. Confirm that `Assets/Shaders/MaterialX/Generated/PeOpenPBRDefault.{slang,json}`, `MaterialX/libraries/pbrlib` and `MaterialX/pe_overrides/libraries` exist. Confirm that the installed `PhasmaCook.exe --mtlx <the default .mtlx> <temp out>` exits 0 and writes both files. Delete the temp install afterwards. If `Path` does not resolve runtime assets from `Assets/` in an installed layout, STOP and report.

- [ ] **Fix 4: read the layout through the pack-aware file API.** In `GetOpenPBRDefaultLayout()` (`OpenPBRMaterial.cpp`):
  - replace `std::filesystem::exists(dir + "PeOpenPBRDefault.slang")` with `AssetFileExists(dir + "PeOpenPBRDefault.slang")`;
  - replace the `std::ifstream` read with `FileSystem file(path, std::ios::in | std::ios::ate);`, checking `AssetFileExists(path) && file.IsOpen()`, then `const std::string text = file.ReadAll();`. That is how `ShaderCache.cpp:31-37` reads shader sources, so exported game packs work.
  - Add the includes only if the PCH does not already provide `AssetFileExists` / `FileSystem`.
  - Keep every warning message as it is.
  - The fallback tests in `openpbr_vis.py` (missing JSON, missing Slang, malformed layouts) must still pass unchanged.

- [ ] **Fix 5: do not break configures on CMake 3.22-3.25.** MaterialX 1.39.5 requires CMake 3.26 (its `CMakeLists.txt:8`) while the engine declares 3.22. In the root `CMakeLists.txt`, change `if(PE_MATERIALX AND NOT ANDROID)` around the MaterialX FetchContent block to:
```cmake
# MaterialX 1.39.5 needs CMake 3.26; older CMake keeps building the engine, without OpenPBR generation.
if(PE_MATERIALX AND NOT ANDROID AND CMAKE_VERSION VERSION_LESS 3.26)
    message(WARNING "PE_MATERIALX needs CMake 3.26 or newer (MaterialX 1.39.5): building without OpenPBR generation")
elseif(PE_MATERIALX AND NOT ANDROID)
```
  (the existing block body and `endif()` stay). Everything downstream already checks `if(TARGET MaterialXGenSlang)`; confirm that by grep and report it.

- [ ] **Fix 6: a failed write must fail the generation and keep the previous outputs.** In `MaterialXGen.cpp`, replace the two `std::ofstream(...) << ...;` lines. Write both files to `<name>.slang.tmp` / `<name>.json.tmp`, check each stream after `close()`, and only when both succeeded rename them over the outputs with `std::filesystem::rename(tmp, out, ec)`. On any failure, remove the temps, set `error` to name the file, and return `false` (`main.cpp` turns that into a non-zero exit).
  **Test** (add to `mtlx_codegen.py`, running in its temporary directory):
  1. Generate once.
  2. Mark the generated `.json` read-only (`os.chmod(path, stat.S_IREAD)`) and generate again. The cook must exit non-zero and both previous outputs must be byte-identical.
  3. Restore write permission.

- [ ] **Step 7: Build, format, verify.**
  - `clang-format -i` the changed C++ files. Keep every file LF.
  - Build with the batch file.
  - Run the full `openpbr_vis.py` on Vulkan (the harness enables `PE_VULKAN_VALIDATION=1`, which includes `validate_sync`), then on DX12 (`PE_DX12_DEBUG=1`, as the test sets).
  - Run it once more on DX12 with `PE_DX12_GBV=1` also set in the environment (GPU-based validation).
  - Run `mtlx_codegen.py`.
  - Read every session's log. A sync hazard, a GPU-based-validation message or a debug-layer error is a failure.
  - Expected: everything passes, including Task 6's reconstruction gates and Task 12's direct-light match (≤2/255), with the new barycentrics.
  Amended 2026-10-14 after Task 14's run:
  - **Cold variants.** Three first attempts timed out on cold Slang compiles. Task 11's warm-up covers only the default MaterialShade variant; `linear_color` (viewport format) and Sponza's shadow settings compile others. The orchestrator made `openpbr_vis.py` wait while any `slangc.exe` runs before every MCP request (its `lua` / `tool` wrappers, also patched into `openpbr_baseline`), so the pool run after a shader edit no longer depends on cache state.
  - **Camera-plane test.** The plane's near corner sits at (0, 0, 0), so w = 0 is exact despite camera yaw. Result: the old reconstruction gives non-finite pixels and depth code 0; the fix gives finite pixels and code 161.


### Task 15: Ship slangc with builds, installs and exported games (added 2026-10-14, the user's choice)

The OpenPBR passes are `.slang` sources, which always compile with `slangc` (`Shader::UsesSlang`). Today the engine runs only the build-time absolute `PE_SLANGC_PATH` (`SlangCompiler.cpp:483`). An installed build or an exported game on another machine therefore cannot compile them. The user chose to ship the minimal Slang set with every build, install and export: `slangc` plus `slang.dll`, `slang-compiler.dll`, `slang-glslang.dll`, `slang-glsl-module.dll` and `slang-rt.dll`, 33 MB. Not shipped: `slang-llvm` (84 MB, CPU targets only) and the `slang-standard-module-*` folder. The orchestrator checked on 2026-10-14 that this set alone compiles a compute shader to SPIR-V and to DXIL.

**Files:** `Phasma/Core/CMakeLists.txt`, `Phasma/Core/Code/API/SlangCompiler.cpp`, `Phasma/Export/main.cpp`. Tests: a new `.testpool/tests/openpbr_export.py`, registered in `.testpool/catalog.json`.

- [ ] **Step 1: Copy and install the Slang set.** In `Phasma/Core/CMakeLists.txt`, inside `if(PE_ENABLE_RUNTIME_SHADER_COMPILER)` after `PE_SLANGC_PATH` is defined:
  - Take the directory of `PE_SLANGC_PATH` (`get_filename_component(... DIRECTORY)`). A bare `slangc` has no directory: in that case print one `message(STATUS ...)` saying installs and exports then need `slangc` on PATH, and ship nothing.
  - Otherwise collect whichever of these exist:
    - Windows, from that directory: `slangc.exe`, `slang.dll`, `slang-compiler.dll`, `slang-glslang.dll`, `slang-glsl-module.dll`, `slang-rt.dll`;
    - Linux: `slangc` from that directory, and `libslang.so`, `libslang-compiler.so`, `libslang-glslang.so`, `libslang-glsl-module.so`, `libslang-rt.so` from that directory or its sibling `../lib`.
  - Add a `POST_BUILD` command on `PhasmaCore` that copies them (`copy_if_different`) to `$<TARGET_FILE_DIR:PhasmaCore>`.
  - Install them to `.` with `COMPONENT "${PHASMA_RUNTIME_INSTALL_COMPONENT}"`, using `install(PROGRAMS ...)` for `slangc` / `slangc.exe` and `install(FILES ...)` for the libraries. Confirm the component variable is defined before this point; it is set in `cmake/PhasmaRuntimeLayout.cmake:11`, so check that this file is included first.
  - Comment the block in one line: why it ships (the `.slang` passes), and that only a `slangc` given by full path can be shipped.
- [ ] **Step 2: Prefer the shipped slangc.** In `SlangCompiler.cpp`, add a function that returns, once (function-local static), `<Path::ExecutablePath()>/slangc.exe` (`slangc` on POSIX) if that file exists, else `PathFromUtf8(PE_SLANGC_PATH)`. It logs one line the first time: `PE_INFO("[Shader] slangc: %s", ...)`. Use it in the `RunProcess` call and in the not-started error message (keep the message's hint about `PE_SLANGC_PATH`). The existing `LoadedDxcDirectory()` handling of `dxcompiler.dll` stays as it is.
- [ ] **Step 3: Export it.** In `Phasma/Export/main.cpp`, add the Step 1 file names to `IsRuntimeBinary`'s exact set. Use `starts_with` for the versioned Linux `.so` names, as the existing entries do.
- [ ] **Step 4: Test end to end** (`.testpool/tests/openpbr_export.py`, registered like the other OpenPBR tests, Windows only).
  - Build. Confirm the six files sit beside `PhasmaEditor.exe` in `build-ninja-physics/Release`, byte-identical to the originals.
  - Create a temporary project with one scene. The scene needs a camera, a directional light and a plane. A node script must, on start, run `settings.set("openpbr", true)` and `material.use_openpbr(<plane>, true)`. Phase 2 does not save `use_openpbr` with the scene, so the script is what turns it on.
  - Export it with `PhasmaExport`; read its command line in `Phasma/Export/main.cpp`. Confirm the export folder has the six Slang files.
  - Launch the exported `PhasmaPlayer.exe` on display 1 for long enough to compile and render (about 30 s on a cold cache). Give it an environment whose PATH has no Slang directory. Then force-kill it (PID-matched). Read its `PhasmaEngine.log`:
    - required: `[Shader] slangc: <export folder>/slangc.exe`, and `MaterialShade` / `MaterialThin` compiled;
    - forbidden: `[OpenPBR] ... missing` (Fix 4's pack-aware loader), `could not start slangc`, any `[ERROR]`, any validation message.
  - If the exported player needs something else to start (a project manifest or settings), follow how the exporter documents it, and report what you did.
  - Delete the temporary project and export afterwards.
  Amended 2026-10-14 after Task 15's first run: the exported player logs one pre-existing warning, `[WARN] Failed to load the Phasma window icon '<export>/RuntimeAssets/Icons/phasma_icon.bmp': Parameter 'src' is invalid`. `Phasma/Core/Code/Base/WindowIcon.cpp` (unchanged since HEAD `d90d4422`) loads the icon with `SDL_LoadBMP` from disk, but PhasmaExport packs it into `game.pepak`. It is not part of this task: `openpbr_export.py` allows exactly this line, matched by its fixed prefix `Failed to load the Phasma window icon`, and nothing else.
- [ ] **Step 5:** `clang-format -i` the changed C++ files and keep everything LF. Run the full `openpbr_vis.py` on Vulkan and DX12, plus `mtlx_codegen.py`, and read every log. Expected: all pass.


## Phases 3–7 (one line each; see the spec)

3. Textures, `.mtlx` import, glTF → MaterialX conversion, persistence, Lua/editor (all inputs), per-graph pipelines, binning, the bindless cap, `pe_material_thin` for connected inputs.
4. Compute pre-skinning (current + previous) and sprites; alpha-cut `Mask()` in `VisibilityPS`.
5. RT per-material hit groups.
6. Transparent/transmission forward pass with the generated code.
7. Performance: empty-bin compaction, VRS, skipping legacy G-buffer and lighting work on visibility pixels, Forward+ tiles in pass 2.

---

## Appendix A — shared code referenced by the tasks

### A.1 Pool-test harness: `.testpool/tests/openpbr_vis.py`
```python
"""OpenPBR / visibility buffer pool test: switch, caps, target lifecycle, off-identity, reconstruction (grows per task)."""
import argparse, json, os, subprocess, tempfile, time
from pathlib import Path
import numpy as np
from PIL import Image
from pe_editor import ROOT, editor_scene, lua, tool
from openpbr_baseline import OUT as BASELINE, LOG, capture as capture_scenes, check_log, log_problems  # full-resolution, never resampled
ASSETS = None  # the temp project's Assets folder, set by main()
API = "vulkan"  # set by main()
SPONZA = Path(os.environ.get("TEMP", ".")) / "pe-rendering-research-2026-10-07/ssr-sponza/Assets"


def new_scene():
    tool("invoke_editor_action", {"action": "file.new_scene", "discard_unsaved": True}); time.sleep(2)


def setting(name, value, wait=3):
    lua(f'settings.set("{name}",{str(value).lower() if isinstance(value, bool) else value}) return "ok"'); time.sleep(wait)


def check_setting_roundtrip():
    new_scene()
    lua('local n=scene.add_empty_node("RS") n:set_scene_settings(true) settings.set("openpbr",true) scene.save("pbr.pescene") return "ok"')
    time.sleep(2)
    lua('settings.set("openpbr",false) scene.load("pbr.pescene") return "ok"'); time.sleep(5)
    assert lua('return tostring(settings.get("openpbr"))') == "true", "openpbr did not survive save/load"
    setting("openpbr", False)


def check_off_identity(api):
    for name, im in capture_scenes(api, after_session=check_log).items():  # checks the log after each of its two sessions
        base = BASELINE / f"{api}-{name}.png"
        assert base.exists(), f"missing baseline {base}: run openpbr_baseline.py with the pre-change binary"
        diff = np.abs(np.asarray(im, np.int16) - np.asarray(Image.open(base).convert("RGB"), np.int16)).max()
        assert diff == 0, f"{name}: openpbr off differs from the pre-change binary by {diff}/255"


CHECKS = [check_setting_roundtrip]  # each task appends its checks (temp-project session)
SPONZA_CHECKS = []  # checks that need saved Sponza (Task 6 appends check_reconstruction_sponza)


def main():
    global ASSETS, API
    p = argparse.ArgumentParser(); p.add_argument("--api", default="vulkan"); p.add_argument("--only", default="")
    a = p.parse_args(); os.environ["PE_DX12_DEBUG"] = "1"; API = a.api
    with tempfile.TemporaryDirectory(prefix="pe-openpbr-") as d:
        ASSETS = Path(d) / "Assets"; (ASSETS / "Agent").mkdir(parents=True); (ASSETS / "Models").mkdir()
        (ASSETS / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
        with editor_scene(a.api, ASSETS):
            for check in CHECKS:
                if a.only and a.only not in check.__name__:
                    continue
                print("->", check.__name__, flush=True); check()
    check_log()  # each editor launch rewrites the log: check this session before the next launch
    sponza_checks = [c for c in globals().get("SPONZA_CHECKS", []) if not a.only or a.only in c.__name__]
    if sponza_checks and (SPONZA / "Scenes/sponza.pescene").exists():
        with editor_scene(API, SPONZA):
            lua('scene.load("sponza.pescene") return "ok"'); time.sleep(18)
            for check in sponza_checks:
                print("->", check.__name__, flush=True); check()
        check_log()
    elif sponza_checks:
        raise AssertionError(f"required Sponza fixture missing: {SPONZA}")
    if "check_openpbr_module_fallbacks" in globals() and (not a.only or "fallback" in a.only):
        check_openpbr_module_fallbacks()  # own sessions; asserts its expected [OpenPBR] warnings itself
    if not a.only or "off_identity" in a.only:
        check_off_identity(a.api)
    print("openpbr ok", a.api)


if __name__ == "__main__":
    main()
```

### A.2 Settings plumbing (`openpbr`, then `openpbr_debug` the same way, each after the previous)
- `Phasma/Core/Code/Base/Settings.h`, after `bool linear_color = false; …`:
  - `bool openpbr = false;       // OpenPBR via MaterialX on the visibility buffer (desktop)`
  - `bool openpbr_debug = false; // openpbr: write the visibility_debug reconstruction codes`
- `Phasma/Runtime/Code/Script/Bindings/Settings/SettingsBindings.cpp`, after `{"linear_color", &SceneSettings::linear_color},`:
  - `{"openpbr", &SceneSettings::openpbr},`
  - `{"openpbr_debug", &SceneSettings::openpbr_debug},`
- `Phasma/Runtime/Code/Scene/SceneSerializer.cpp`:
  - save, after `settings.AddMember("linear_color", s.linear_color, allocator);`: `settings.AddMember("openpbr", s.openpbr, allocator);` and the same for `openpbr_debug`;
  - load, after the `gSettings.linear_color = …` line: `gSettings.openpbr = settings.HasMember("openpbr") && settings["openpbr"].IsBool() && settings["openpbr"].GetBool();` and the same for `openpbr_debug`.
- `Phasma/Editor/Code/GUI/Widgets/PipelineControls.cpp`, after `changed |= ImGui::Checkbox("Linear Color", &gs.linear_color);`:
  - `changed |= ImGui::Checkbox("OpenPBR", &gs.openpbr);`
  - `changed |= ImGui::Checkbox("OpenPBR debug view", &gs.openpbr_debug);`
- `Phasma/Runtime/Code/RenderPasses/GlobalIlluminationPass.cpp`, in the hashed list that ends with `settings.use_Disney_PBR ? 1.f : 0.f})`: append `, settings.openpbr ? 1.f : 0.f` before the closing `})`. Pass 1 changes what screen probes and reflections read.

### A.3 Rebuild flag (`SceneRendererCore`)
`SceneRendererCore.h`, next to `bool m_colorFormatChanged = false;`:
```cpp
        bool m_openpbr = false; // SceneUsesOpenPBR() the passes were last built for
```
`CreateRenderTargets`, replacing its first two lines:
```cpp
        m_colorFormatChanged = m_colorFormatChanged ||
                               (m_viewportRT && (m_hdr != SceneUsesHDR() || m_openpbr != SceneUsesOpenPBR()));
        m_hdr = SceneUsesHDR();
        m_openpbr = SceneUsesOpenPBR();
```
`SyncOptionalRenderTargets`:
```cpp
    bool SceneRendererCore::SyncOptionalRenderTargets(bool hasRayTracingGeometry)
    {
        if (!SyncOptionalSceneRenderTargets(m_renderTargets, hasRayTracingGeometry))
            return false;
        // The visibility target switches shader variants: Resize must rebuild pipelines, not only descriptors.
        if (m_openpbr != SceneUsesOpenPBR())
        {
            m_openpbr = SceneUsesOpenPBR();
            m_colorFormatChanged = true;
        }
        return true;
    }
```
`SyncOptionalSceneRenderTargets` treats `visibility` exactly like `velocity`:
- `needVisibility = SceneUsesOpenPBR()`;
- changed when presence differs;
- destroy or create it with `PE_FORMAT_R32G32B32A32_UINT`;
- include it in the early-out.

### A.4 G-buffer pass: the visibility attachment
`GbufferPass.cpp`: add `#include "DepthPass.h"` (for `VisibilityBlendState`, Task 5 Step 5), then replace `ApplyGBufferSurface` in the anonymous namespace with:
```cpp
        void ApplyGBufferSurface(PassInfo &pi, const PassVariant &surface, bool writeVelocity, bool visibilityAttachment,
                                 bool writeVisibility)
        {
            pi.Apply(surface);
            const bool visibilityVariant = visibilityAttachment && writeVisibility;
            if ((!writeVelocity || visibilityVariant) && !surface.fragmentShader.empty() && pi.pFragShader)
            {
                ShaderDesc desc{};
                desc.sourcePath = Path::ResolveAsset(surface.fragmentShader);
                desc.entryPoint = "mainPS";
                desc.stage = PE_SHADER_STAGE_FRAGMENT;
                if (!writeVelocity)
                    desc.defines.push_back({"GBUFFER_NO_VELOCITY", "1"});
                if (visibilityVariant)
                    desc.defines.push_back({"GBUFFER_VISIBILITY", "1"});
                Shader *oldFrag = pi.pFragShader;
                pi.pFragShader = Shader::Create(desc);
                Shader::Destroy(oldFrag);
            }

            // .pass lists 6 color blends: n,a,srm,velocity,emissive,transparency → drop velocity.
            if (!writeVelocity && pi.colorBlendAttachments.size() >= 6)
                pi.colorBlendAttachments.erase(pi.colorBlendAttachments.begin() + 3);
            // standard_pbr meshes wrote their payload in the prepass (mask 0); terrain and voxels draw only here,
            // so they clear it where they win depth.
            if (visibilityAttachment)
                pi.colorBlendAttachments.push_back(VisibilityBlendState(writeVisibility));
        }
```
- **Call sites** (anchors in `GbufferPass.cpp`):
  - `GbufferOpaquePass::UpdatePassInfo`, both `ApplyGBufferSurface(*m_passInfo, *surface, writeVelocity)` and `ApplyGBufferSurface(*m_passInfoDS, *surface, writeVelocity)` (standard opaque) become `(…, writeVelocity, m_visibilityRT != nullptr, false)`.
  - `buildSubsystemPipeline`'s `ApplyGBufferSurface(*pi, *surf, writeVelocity)` (voxel and terrain) becomes `(…, writeVelocity, m_visibilityRT != nullptr, true)`.
  - Every `ApplyGBufferSurface` call in `GbufferTransparentPass` (transparent, voxel water) becomes `(…, writeVelocity, false, false)`.
- **Attachments and formats:**
  - `BuildGBufferAttachments` gains a last parameter `Image *visibility` (after `Image *depth`, before `PeLoadOp colorLoadOp`). It pushes it after `transparency` and before depth, always with `PE_LOAD_OP_LOAD` (the prepass wrote it):
```cpp
            if (visibility)
            {
                Attachment a{};
                a.image = visibility;
                a.loadOp = PE_LOAD_OP_LOAD; // the depth prepass wrote the payload
                a.storeOp = PE_STORE_OP_STORE;
                attachments.push_back(a);
            }
```
  - `BuildGBufferColorFormats` gains a last parameter `Image *visibility` and appends `visibility->GetFormat()` when it is non-null.
  - `GbufferOpaquePass`:
    - gains the member `Image *m_visibilityRT = nullptr;` (`GbufferPass.h`, after `Image *m_depthStencilRT;`);
    - `Init` sets `m_visibilityRT = SceneUsesOpenPBR() ? rs->GetRenderTarget("visibility") : nullptr;` before building attachments;
    - every call in the opaque pass (`Init`, `UpdatePassInfo`, `Resize`) passes `m_visibilityRT`.
  - The transparent pass passes `nullptr`.
  - `ClearRenderTargets` must **not** clear `visibility`.
  - `GbufferOpaquePass::Resize`'s format comparison then rebuilds the pipelines on a toggle, as it already does for velocity.

`PS_OUTPUT_Gbuffer` in `Structures.hlsl`:
```hlsl
#ifdef GBUFFER_NO_VELOCITY
    float4 emissive : SV_TARGET3;
    float transparency : SV_TARGET4;
#ifdef GBUFFER_VISIBILITY
    uint4 visibility : SV_TARGET5;
#endif
#else
    float2 velocity : SV_TARGET3;
    float4 emissive : SV_TARGET4;
    float transparency : SV_TARGET5;
#ifdef GBUFFER_VISIBILITY
    uint4 visibility : SV_TARGET6;
#endif
#endif
```
`TerrainGBufferPS.hlsl` and `VoxelGBufferPS.hlsl`, after `output.transparency = …;`:
```hlsl
#ifdef GBUFFER_VISIBILITY
    output.visibility = uint4(0, 0, 0, 0); // drawn after the prepass: clear any payload this surface covers
#endif
```
