# OpenPBR overnight handoff (2026-11-01)

Branch `claude/trusting-heisenberg-zbvhgh`, 11 commits on top of `master` (`8945405c`). This file is a handoff note:
move what you keep into `docs/wiki` and drop this file before merging.

## What was checked here, and what was not

The cloud container has no GPU and no `slangc`.

- **Checked:** every commit builds on Linux, Release. That covers PhasmaCore, PhasmaRuntime, PhasmaEditor,
  PhasmaEditorModule, PhasmaPlayer, PhasmaExport and PhasmaCook. `RayTrace.hlsl` compiles to SPIR-V with the
  repo's own DXC, with warnings as errors, in all four RT variants: `GI_SHADING`, `GI_PROBES`, `+REFLECTIONS` and
  `+SCREEN_PROBES`. The build's PhasmaCook regenerates the default module at ABI 9 with both new hit hooks.
- **Not checked:** no `.slang` file was compiled; the auto-mode permission check blocked running a downloaded
  Slang. Nothing was rendered, on either API.
- **Not checked:** the DX12-only code paths. No Windows-only file was edited.
- **Not checked:** formatting. clang-format 18 here cannot read `.clang-format`, because `AlignFunctionDeclarations`
  needs clang-format 20 or newer. Run `clang-format -i` on the touched C++ files.

## Commits

| Commit | What |
|---|---|
| `2d4074e` | Shape modules **Task 7**: one MaterialX hit group per shape group, plus a per-pass slot table (set 1, binding 9). |
| `a0b90b2` | Hit groups ordered by the shape list, so reordering nodes only rewrites the slot table, with no per-frame set copy. |
| `3e88c01` | Spec "Later": `.slang` sources log `compiler=slang`. |
| `bb6e586` | Spec "Later": a persistent Vulkan pipeline cache at `ShaderCache/_pipelines/vulkan.bin`. |
| `7b3a2e9` | The area-light seam at RT hits uses the graph's shading normal; the BSDF is inlined once per hit shader. |
| `755163e` | The forward pass bends limb normals; lines draw after glass (sort 720 → 806). |
| `558a8aa` | Ray-cone texture LOD at RT hits, legacy and MaterialX. **Module ABI 9.** |
| `8a305c9` | MaterialX alpha-blend and Transmission meshes run their graph at camera-pass RT hits, with traced refraction. |
| `97e11a0` | The continuation payload sets every field (no `(HitPayload)0` cast in Slang). |
| `bb14486` | Review fixes: cache saves after builds land, failures reported once per group, one shared skip-blended rule. |

**ABI 9:** every cooked module regenerates on the next editor load, the shape list moves to `shapes_abi9.txt`, and
exported games must be re-exported.

## Run these first (your machine)

1. **Compile the slangc shaders on both APIs.** Open any scene with MaterialX materials and RT on. Watch for
   `[OpenPBR] ... shader compile failed` and `[RT] ... failed to compile`. The new code is in:
   - `MaterialHitHead.slang` and `MaterialHitMain.slang`: the slot lookup, the continuation rays and the refraction
     seam;
   - `MaterialForwardMain.slang` and `VisibilityCommon.slang`: the limb normal;
   - `mx_texture.slang` and `mx_environment_fis.glsl`, both overrides inside cooked modules.
2. **Task 7's gates:**
   - `openpbr_shape_set.py --part rt`, which you still need to write as Task 7 Step 1 describes. Expect 2 hit groups
     on Vulkan; the finish line counts the legacy group.
   - `openpbr_rt_hits.py` (all parts), `openpbr_rt_lifecycle.py`, `rt_hit_build_cancel.py` and
     `openpbr-rt-async-build`, on both APIs.
3. **Gates whose expected images changed on purpose:**
   - `openpbr_forward.py --part parity`: limb pixels with N.V < 0.01 now differ from GlslRenderer. Mask the rim the
     way pass 2's parity cases do.
   - Tests that capture RT reflections or Full RT with textures: the ray-cone LOD now picks mips instead of mip 0.
     `openpbr_rt_hits` part "Full RT vs raster" should get closer to raster; textured checkers may move.
   - Tests that capture MaterialX glass in Full RT: it now shows the graph, not the legacy mirror.
4. **Perf (AGENTS.md gates):** each active RT pass on the RT probe scene against `master`; mixed-shape waves are the
   spec's known risk. Ray cones should make reflection and GI rays cheaper, since they now hit coarser mips.
5. **Pipeline cache (Task 8 Step 3):** run a warm Vulkan open with RT on twice in a row. The log should show
   `[Vulkan] pipeline cache: N KB loaded` on the second launch, and RT pipeline creation should no longer take 24–25 s.

## Behaviour changes to know about

- **RT hit groups:**
  - The SBT holds the legacy group plus one MaterialX group per shape group in the scene. Vulkan has one group; DX12
    has one per 8 shapes.
  - An instance selects its group only once every RT pass has adopted that group with the same text.
  - A module whose shape the adopted groups already hold makes the passes rewrite only the slot table, logged as
    `[RT] hit groups kept: …`.
- **Forward meshes in RT:**
  - Probe passes (GI, screen probes, reflections) still skip alpha-blend and Transmission surfaces. Both legacy and
    MaterialX any-hits now read `HitPayload::skipBlended`, which the probe raygens set.
  - Camera hits (Full RT, and Hybrid continuations from legacy glass):
    - blend `colour * a + behind * (1 − a)`, as the forward pass blends;
    - refract through one ray along the mean refracted direction, so `transmission_color` tints the scene behind;
    - pass straight through exit faces;
    - fall back to the sky once two bounces are spent.
- **Lines:** they draw after Hybrid's transparent trace and the forward pass. A line in front of any glass stays on
  top; a line behind glass is depth-hidden, as behind legacy glass. A Lua pipeline script that orders a pass against
  Lines' old key, 720, needs updating.
- **Pipeline cache:**
  - Every Vulkan pipeline now uses one cache. The file is saved at most every 5 s after a module or RT build is
    adopted, and again at shutdown.
  - The file is rejected if it comes from another GPU or driver, is torn, or is over 256 MB.

## Not done, with the reason

- **Skinned meshes and sprites in the TLAS.** This needs:
  - an unculled RT skinning dispatch: the OpenPBR pre-skin culls to the camera frustum, so off-screen skins would
    vanish from shadows and reflections;
  - a BLAS per skinned instance, built with `ALLOW_UPDATE` and refit every frame before the TLAS update, with
    acceleration-structure build barriers;
  - BLAS refit support in the Windows-only DX12 acceleration-structure code;
  - deformed vertices in `PeHitTriangle`. The legacy hit already skins attributes through `SkinVertex`.

  I could not compile or test any of it, so I left it alone.
- **Vulkan specialization constants:** the spec defers them to a spec of their own.
- **OIT, and SSR seeing the surface behind glass:** both are design-sized, with no plan yet.
- **The exit-face IOR:** exit faces refract with `specular_ior`, while the entry lobe uses MaterialX's modulated
  eta. Coated glass can therefore refract slightly asymmetrically.

## Plan bookkeeping (`2026-10-28-materialx-shape-modules.md`)

- **Task 6, Steps 3–6:** the code is in `8945405c`, built per module as your "As built" note says. Steps 1, 2, 7 and
  8 are test runs.
- **Task 7, Step 3:** done (`2d4074e`, `a0b90b2`). Steps 1, 2, 4 and 5 are yours.
- **Spec "Later":**
  - the compiler label is done (`3e88c01`);
  - persisting `VkPipelineCache` is done (`bb6e586`, `bb14486`);
  - specialization constants are still open.

## Wiki text to paste (`rendering.md`, OpenPBR Materials)

**Hit shaders (shape groups, 2026-11-01).**
- **Groups:** the RT pipelines hold one MaterialX hit group per shape group: the whole shape list on Vulkan, 8 shapes
  per group on DX12. Each group compiles `ShaderCache/MaterialX/group_<key>.hit.slang` (`MaterialHitHead` + the
  combined module + `MaterialHitMain`).
- **Slots:** each RT pass keeps a slot table, `pe_shapeSlots` at set 1, binding 9, indexed by the record's module
  index (`GetOpenPBRShapeSlot`). The hit shaders pass that slot to the combined module's dispatchers.
- **Hit sets:** `MakeOpenPBRHitSet` orders groups by the shape list. `SceneRendererCore::SyncRTHitModules` builds one
  set for every pass. A pass whose groups already hold every shape takes a new module list without compiling
  (`RayTracingPass::RetargetHitSet`).
- **SBT offsets:** an instance uses 1 + its group (`OpenPBRHitSet::HitGroup`) only where every pass agrees.
- **Failures:** a failed group is marked broken and its shapes build alone. A lone shape's modules are rejected.
- **Texture LOD:** hits sample at a ray-cone LOD (`RT_RayConeLodBase`; MaterialX through `pe_hit_texture_lod`, module
  ABI 9).
- **Forward meshes:** camera-pass hits on forward meshes blend, refract (`pe_traced_refraction`, the FIS override's hit
  seam) and pass through exit faces. Probe rays skip them (`HitPayload::skipBlended`).
- **Area lights:** their representative point follows the graph's shading normal.

**Limits removed:**
- "Texture LOD at hits is mip 0".
- "RT keeps legacy hit groups for forward meshes", including the refraction tint by the mirrored base colour.
- "The area-light seam's specular representative point uses the geometric normal".
- "Lines draw before the pass".
- "Limb normals are not bent".
