# OpenPBR Phase 5 — RT per-material hit groups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Status: implemented and verified, uncommitted, 2026-10-20 (user chose Decision 1 = B and Native execution). Starting point: `63488b84` on `master`. Results and open items are in the wiki's "Phase 5 RT hit groups" section and the ledger `.superpowers/sdd/2026-10-20-openpbr-phase5/progress.md`. The user approved the loosened lifecycle/identity test bounds (2026-10-20). Open: the RT pipeline build stall (482 s for 4 modules on Vulkan), now its own session. No commit is authorized.

**Goal:** When a ray hits a mesh with a MaterialX material, its hit shader runs that material's generated graph. This covers reflection rays, GI rays, screen-probe rays and full-RT primary rays. Hits then show textures, connected inputs and the coat, fuzz, thin-film and subsurface lobes, not the legacy mirror.

**Architecture:**
- Every one of the four RT pipelines gets the same hit-group list: `[legacy, module 0, module 1, …]`.
- The TLAS instance's SBT record offset selects the group. It is 0 for legacy meshes and 1 + the module's slot for OpenPBR meshes. Each `TraceRay` already passes contribution 0 and multiplier 0, so the hit index is exactly the instance offset.
- A module's closest-hit is a Slang wrapper with three parts: a hit head (RT bindings plus `pe_*` hooks implemented with the legacy hit's lights, shadow rays and probes), the generated module, and a hit main.
- The hit main rebuilds the surface with the visibility path's own reconstruction.
- The generator gains derivative seams so the module compiles in hit stages, where there are no derivatives.

**Tech Stack:** C++20 engine, HLSL (DXC) for `RayTrace.hlsl`, Slang for MaterialX modules, Vulkan RT pipelines and DXR 1.0 state objects, MaterialX 1.39.5 / OpenPBR 1.1.1, the `.testpool` Python harness over the editor MCP.

**Spec:** [2026-10-13-openpbr-design.md](../specs/2026-10-13-openpbr-design.md). The relevant sections are "Other paths" (Phase 5 = "per-material hit groups that run the generated graph at the hit (UE's hit-lighting model)"), Switch, Verification, Performance and Phases. Earlier phase plans in this folder describe the gates this one reuses.

## Decision 1 — what GI-probe hits run (user chose B, 2026-10-20; execution: Native)

Today every secondary hit, reflections included, is shaded like Lumen's surface cache. That means diffuse only, with the specular colour folded into the albedo. `RayTrace.hlsl:961-974` does this so that "a sharp highlight on a mirror would otherwise reach the probes as fireflies."

- **B (recommended): UE's split.**
  - Reflection hits and full-RT/hybrid primary hits run the full `pe_material_shade`, so every lobe is present and lit at the hit.
  - GI-probe and screen-probe hits evaluate the graph's inputs (albedo, metalness, roughness, normal, emission). They pass them through the existing surface-cache fold and the legacy diffuse lighting.
  - Graph textures and connected inputs reach GI; specular fireflies stay out of the probes.
- **A: full shade at every hit.**
  - One code path, and lobes reach GI too.
  - The firefly protection is lost for OpenPBR materials, and each probe ray pays MaterialX's FIS environment integral (`openpbr_env_samples`, default 32).
  - Under A, Task 4 shrinks to the GI gates in its Step 1.

The tasks below are written for B.

## User-visible consequence: module ABI 3

Task 1 bumps the generated-module ABI, because hit stages have no derivatives. As a result:
- Every cooked MaterialX module in every project is regenerated on its next editor load.
- Every exported game that uses MaterialX must be re-exported. Until then its Player rejects the old modules with the existing re-export message and renders them with the legacy material.

## Global Constraints

- `openpbr` scene setting, default false. "Off: output is byte-identical to the pre-change binary, with no new targets or passes." With openpbr off, every RT pipeline keeps exactly one hit group and every instance offset is 0.
- Module hits run MaterialX's generated code only, with no hand-written BSDF. PE supplies lights, shadows and environment through the `pe_*` seams, as `PePrelude.slang` does for pass 2.
- "Android: the legacy path only."
- Verification item 5: "Clean logs, no validation errors". Pool-test conventions: "off-identity baseline first, `.testpool` only, Vulkan + DX12, log check".
- Performance: "Off: within the project thresholds on Sponza" (FPS >5% and >1 fps; ms >5% and >0.5 ms; VRAM >50 MB). "On: measured and reported."
- Build and test only in the sandbox `C:\Users\Christos\pe-p4-sandbox`, under the shared GPU lock (`%TEMP%/pe-gpu-lock`, `fair_lock.sh`), on display 1. Never in `build-ninja-physics`.
- No mouse injection. Every visual gate saves a contact sheet that root views.
- No test-only engine code. `third_party/` is off-limits. Run `clang-format -i` on every touched `.cpp`/`.h`. Follow the CLAUDE.md layout rules in shaders too.
- DX12 API conservatism: no new public RHI surface. Backend fixes stay inside `Dx12*Impl`/`Vulkan*Impl`.
- No commits unless the user says so. Each task ends at a checkpoint with a drafted one-line message.

## Review Focus

1. **Module-set churn while rays run.** Assigning or removing MaterialX materials, or hiding nodes, while reflections and GI run must never leave a TLAS offset at or beyond the pipeline's group count. The expected result is no device loss and no validation error. Task 3 Step 6 and Task 6 Step 1.
2. **Render-mode and openpbr toggles.** Raster ↔ Hybrid ↔ Full RT, and openpbr on ↔ off, rebuild all four pipelines and the offsets in the same frame. The expected result is identical images to a fresh load of the final state. Task 6 Step 1.
3. **Shader hot reload of `RayTrace.hlsl`/`RTCommon.hlsl`.** It must keep the module groups (`RenderPassShaderReload.cpp:20-82` restores `hitGroups`). The expected result is that reflections still show the graph after reload. Task 3 Step 7.
4. **Graphs whose nodes use derivatives.** Examples are `fwidth` in OpenPBR subsurface, `textureGrad` and `aastep`. Such graphs must compile for hit stages instead of failing the pipeline. The expected result is that every Phase 3 fixture graph compiles for hits on both targets. Task 2 Step 1.
5. **A pre-Phase-5 exported game.** Its ABI-2 cooked modules must be rejected with the existing re-export guidance and fall back to legacy, with no crash in RT. Task 1 Step 4.

---

### Task 0: Fixed references from the pre-change binary

**Files:**
- Create: `.testpool/tests/openpbr_rt_baseline.py`
- Reuse: `openpbr_baseline.py` (`full_shot`, `load_mix`), `openpbr_final_perf.py`

- [ ] **Step 1: Capture off-identity references.** Use the sandbox build of `63488b84`, on Vulkan and DX12.
  - Cases: Sponza and the mix scene, each in Raster with GI, reflections and screen probes on; in Hybrid; and in Full RT.
  - Capture each case twice, in separate launches. A case whose two captures differ is not deterministic: record its self-difference as that case's tolerance and say so in the evidence.
- [ ] **Step 2: Capture the on-path references.** Use the vis, parity and graphs scenes with openpbr on. Task 1 must leave raster MaterialX output unchanged against these.
- [ ] **Step 3: Capture perf controls.** Saved Sponza, Release, Immediate present, 10 snapshots 1 s apart, per API.
- [ ] **Step 4: Check.** Each reference directory has `meta.json` with build hash `63488b84`, the API and the mode. The contact sheets have been viewed.

### Task 1: Generator derivative seams (module ABI 3)

**Files:**
- Modify: `Phasma/Cook/Code/MaterialX/MaterialXGen.cpp` (transforms 380-485, tail 489-564)
- Modify: `Phasma/Runtime/RuntimeAssets/MaterialX/pe_overrides/libraries/stdlib/genslang/lib/mx_texture.slang`
- Modify: `Phasma/Runtime/Code/Scene/OpenPBRMaterial.h:7` (`kOpenPBRModuleAbi = 3`)
- Test: `.testpool/tests/mtlx_codegen.py`, `.testpool/tests/openpbr_sprite_abi.py`

**Interfaces:**
- Produces, in every generated module:
  - The macros `pe_ddx(x)`, `pe_ddy(x)` and `pe_fwidth(x)`. They are the builtins unless `PE_MATERIAL_HIT` is defined, and `((x) * 0)` (zero, same type) when it is.
  - `float pe_curvature(float3 N, float3 P)`. It is `length(fwidth(N)) / length(fwidth(P))` normally, and `pe_hit_curvature` under `PE_MATERIAL_HIT`. The hit head declares `static float pe_hit_curvature;` before the module.
  - `texture()` uses `SampleLevel(…, 0.0)` under `PE_MATERIAL_HIT` and implicit `Sample` otherwise. Mip 0 matches the legacy hits' `SampleArray`.
- `PE_OPENPBR_MODULE_ABI 3`.

- [ ] **Step 1: Write the failing codegen assertions** in `mtlx_codegen.py`, for the default module and every Phase 3 fixture graph:

```python
src = generate(fixture)
assert "#define PE_OPENPBR_MODULE_ABI 3" in src
outside = strip_seam_block(src)   # text minus the generator's PE_MATERIAL_HIT seam block
for tok in ("ddx(", "ddy(", "fwidth(", "ddx_fine(", "ddy_fine(", "ddx_coarse(", "ddy_coarse("):
    assert not re.search(r"(?<![\w.])" + re.escape(tok), outside), tok
assert "pe_curvature(N, P)" in src and "length(fwidth(N)) / length(fwidth(P))" not in outside
```

- [ ] **Step 2: Run it and confirm it fails.** Run `py -3 .testpool/tests/mtlx_codegen.py`. Expected: FAIL on ABI 3.
- [ ] **Step 3: Implement the seams.**
  - Rewrite whole tokens, and only outside the emitted seam block.
  - The curvature line is matched exactly. If it is absent in a module that defines `mx_subsurface_scattering_approx`, cooking fails with a message, as the other transforms do.
  - Bump both ABI constants.
- [ ] **Step 4: Update `openpbr_sprite_abi.py`.**
  - Seed ABI-2 cooked caches and check that the editor regenerates them.
  - Check that the Player rejects a cooked-only ABI-2 module with the existing re-export guidance and renders legacy.
  - With full-RT and reflections on, the Player must stay alive (Review Focus 5).
- [ ] **Step 5: Run the tests.**
  - `mtlx_codegen.py`, `mtlx_mask_codegen.py` (sample counts unchanged) and `openpbr_sprite_abi.py` on both APIs must PASS.
  - Run the Task 0 Step 2 on-path scenes: they must stay within 0/255 of the references, because raster code is unchanged outside `#ifdef`.
- [ ] **Step 6: Checkpoint.** Draft: "Add hit-stage derivative seams to generated MaterialX modules (ABI 3)".

### Task 2: Hit surface, hit prelude and module hit wrappers

**Files:**
- Create: `Phasma/Runtime/RuntimeAssets/Shaders/RayTracing/RTCommon.hlsl`. It holds everything in `RayTrace.hlsl` above `[shader("raygeneration")]`, moved verbatim. `RayTrace.hlsl` includes it.
- Create: `Phasma/Runtime/RuntimeAssets/Shaders/MaterialX/PeEnvironment.slang`. It holds `PeLatLongUv`, `PeIrradianceUv`, `pe_env_max_mip` and `pe_env_lod`, moved verbatim from `PePrelude.slang`, which includes it.
- Create: `Phasma/Runtime/RuntimeAssets/Shaders/MaterialX/MaterialHitHead.slang`, `MaterialHitMain.slang`, and `MaterialHit.slang` (the module-0 wrapper, like `MaterialShade.slang`).
- Modify: `VisibilityCommon.slang:198-248` and `Phasma/Runtime/Code/Scene/OpenPBRMaterial.{h,cpp}` (`OpenPBRModule`, `RegisterModule` 361-393, module 0 at 329-331).
- Test: create `.testpool/tests/mtlx_hit_codegen.py`.

**Interfaces:**
- Consumes the Task 1 seams.
- Produces:
  - `PeSurfacePoint PeSurfaceAtBarycentrics(PeTriangle t, float3 b)`. It is everything in `PeSurfaceAt` after the barycentrics, with `svPosition = 0`. `PeSurfaceAt` calls it, then sets `svPosition`.
  - `MaterialHitHead.slang`:
    - It includes `../RayTracing/RTCommon.hlsl`, `VisibilityCommon.slang` and `PeEnvironment.slang`.
    - It adds the set-1 bindings below. DX12 maps binding N to register N, and an array to N..N+17, as described in `MaterialShadeHead.slang`.
    - These bindings were checked free on 2026-10-20. RT set 1 holds bindings 0-3 (b0, b1, t2, u3), and `ProbeVolume.hlsl`, `ScreenProbeCommon.hlsl` and `ReflectionCommon.hlsl` declare sets 0, 2 and 3 only.

      | Binding (set 1) | Declaration |
      |---|---|
      | 4 | `ByteAddressBuffer pe_materialBytes` |
      | 5 | `SamplerState pe_envSampler` |
      | 6 | `SamplerState pe_samplers[18]` |
      | 7 | `Texture2D<float4> pe_envRadiance` |
      | 8 | `Texture2D<float4> pe_envIrradiance` |

    - Statics: `pe_P`, `pe_N`, `pe_occlusion`, `pe_hit_curvature`.
    - Hooks:
      - `pe_view_position()` returns `WorldRayOrigin()`, so V = −ray direction.
      - `pe_light_count()` and `pe_light_sample(i, P, out L, out radiance, out occlusion)` mirror `RT_DirectLight`/`RT_Compute{Point,Spot,Area}Light`: their skip conditions, attenuation, cone and `TraceShadowRay` visibility (directional light 0 only, as today), without their BRDF.
      - `pe_env_irradiance(N)` is the legacy hit's `ProbeLighting` term, `indirect.rgb * scale * indirect.a / PI`. The scale is `gi_intensity` under `GI_SHADING` and 1 under `GI_PROBES`. The `pe_envIrradiance` IBL fills `1 - indirect.a`, times `cb_iblIntensity`.
      - `pe_traced_reflection` returns false.
      - `pe_env_samples()` returns `cb_IBL == 0 ? 0 : int(cb_envSamples)`.
      - `pe_env_lookup` uses `pe_envRadiance` times `cb_iblIntensity * pe_occlusion`.
  - `MaterialHitMain.slang`: `[shader("closesthit")] void pe_hit_closest(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)`. In order, it:
    1. builds a rigid `PeTriangle` from `PeLoadVertex(geometry, meshInfos[i].vertexOffset, GetIndices(...))`, with `world` taken from `ObjectToWorld4x3()` (row-vector form, last column 0,0,0,1) so that `sp.P` stays on the traced ray even if the refit lags the frame data;
    2. calls `PeSurfaceAtBarycentrics`, then `PeBendLimbNormal(t, -WorldRayDirection(), sp)`;
    3. sets `pe_hit_curvature` to the largest |Δn|/|Δp| over the three world-space edges, and `pe_occlusion` from the legacy AO texture as the legacy hit does;
    4. calls `pe_load_material(pe_materialBytes, constants[id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET)`, then `pe_fill_vertex_data(sp)` and `pe_material_shade(sp.nextUv, sp.spriteBlend)`;
    5. writes radiance, `t = RayTCurrent()`, `hitKind = HitKind()` and `alpha = 1`.
  - `OpenPBRModule::hitSource`: the wrapper path `ShaderCache/MaterialX/<hash>.hit.slang` for registered modules, and `MaterialHit.slang` for module 0. The wrappers compile with the pass's RT defines plus `PE_MATERIAL_HIT`.
  - `RayTracingPassUBO.cb_rtPassPad1` is renamed `cb_envSamples` (no size change). Task 3 fills it.

- [ ] **Step 1: Write the failing test `mtlx_hit_codegen.py`.**
  - For the default module and every Phase 3 fixture graph, and for each RT define set (`GI_SHADING`; `GI_PROBES`; `GI_PROBES+SCREEN_PROBES`; `GI_PROBES+REFLECTIONS`), compile `pe_hit_closest` with slangc to SPIR-V and to DXIL `lib_6_6`, as `SlangCompiler.cpp` does.

```python
spv = disasm(compile_hit(module, defines, "spirv"))
assert "OpEntryPoint ClosestHitKHR" in spv and "pe_hit_closest" in spv
assert not re.search(r"OpImageSampleImplicitLod|OpDPdx|OpDPdy|OpFwidth", spv)
dxil = dxc_dump(compile_hit(module, defines, "dxil"))
assert "pe_hit_closest" in dxil and not re.search(r"dx\.op\.(sample|derivCoarse|derivFine|calculateLOD)\(", dxil)
```

- [ ] **Step 2: Run it and confirm it fails.** Expected: FAIL, because the wrapper is missing.
- [ ] **Step 3: Implement the files above.** `RTCommon.hlsl`, `PeEnvironment.slang` and the `PeSurfaceAt` split are moves and refactors, with no behaviour change.
- [ ] **Step 4: Run the tests.** `mtlx_hit_codegen.py` must PASS. The Task 0 on-path scenes must be within 0/255. Then rebuild the sandbox and run the off-identity smoke from Task 0 Step 1 (Raster, Vulkan): it must be byte-identical.
- [ ] **Step 5: Checkpoint.** Draft: "Add MaterialX hit wrappers and the hit-stage prelude".

### Task 3: Hit groups in the RT pipelines and SBT offsets

**Files:**
- Modify: `Phasma/Runtime/Code/RenderPasses/RayTracingPass.{h,cpp}`. This covers `UpdatePassInfo` (32-58), the descriptor writes (88-140) and the UBO. The three subclasses inherit the changes.
- Modify: `Phasma/Runtime/Code/Render/SceneRendererCore.cpp:214-223`, `Phasma/Runtime/Code/Scene/Scene.h` and `SceneRayTracing.cpp` (`WriteRTInstances` 155-251).
- Modify, only if Step 1 shows it is needed: `Phasma/Core/Code/API/DX12/Dx12ShaderImpl.cpp` (Slang RT stages → `lib_6_6`) and `RenderPassShaderReload.cpp`.
- Test: create `.testpool/tests/openpbr_rt_hits.py`.

**Interfaces:**
- Consumes `OpenPBRModule::hitSource`, `pe_hit_closest` and `cb_envSamples`.
- Produces:
  - `void Scene::SetRTHitModules(const std::vector<uint32_t> &modules)`. When the list changes it sets `m_tlasMasksDirty`, so the next refit rewrites every instance.
  - `const std::vector<uint32_t> &Scene::GetRTHitModules() const`.
  - `WriteRTInstances` writes `instanceShaderBindingTableRecordOffset` / `InstanceContributionToHitGroupIndex` as 1 + the position of `materialInstance->gpuOpenPBRModule` in that list, for meshes where `MeshUsesOpenPBR`. When the module is not in the list, for example when openpbr is off and the list is empty, it writes 0. Disabled instances also get 0.
  - Offsets change without the list changing (a mesh moves from legacy to an existing module, or two meshes swap modules). So the material byte packing (`SceneBuffers.cpp:1252-1302`, where `gpuOpenPBRModule` is assigned) also sets `m_tlasMasksDirty` whenever it runs with RT instances present.
  - `void RayTracingPass::SyncHitModules(const std::vector<uint32_t> &modules)`. It stores the list in `m_hitModules` and rebuilds when the list changes.
  - Every `UpdatePassInfo` path builds its groups from `m_hitModules`: resize, HDR format change, lazy re-init and hot reload. Groups are `{legacy closesthit, legacy anyhit}`, then per module `{pe_hit_closest, legacy anyhit}`. Task 5 replaces that any-hit.
  - `SceneRendererCore` calls `scene->SetRTHitModules(modules)`, then `SyncHitModules(modules)` on each initialized RT pass (RayTracing, GlobalIllumination, ScreenProbe, Reflection). It uses the same `modules` the thin/shade syncs use, which is empty when openpbr is off.
- **Invariant:** in every submitted frame, the refit that writes new offsets and the pipelines that hold the matching groups come from the same list. Verify where `UpdateTLASTransformations` runs relative to this sync. If the refit runs earlier in the frame, run it from the sync.

- [ ] **Step 1: Risk check.**
  - Build a pipeline with one Slang `pe_hit_closest` group next to the DXC library, and one instance at offset 1, on each API.
  - On DX12, confirm the Slang hit compiles as a library export that `ExportToRename` renames, and that the global root signature reflects its set-1 bindings.
  - On Vulkan, confirm the payload is accepted across DXC and Slang modules.
  - Record the result and fix it inside the backend if needed.
- [ ] **Step 2: Write the failing gate `openpbr_rt_hits.py --api vulkan|dx12`**, part `fullrt`.
  - Setup: Phase 2's seven sphere materials (default, coat, fuzz, metal F82, thin film, anisotropy, subsurface) and a 4×4-checker textured plane viewed at magnification. One directional light; shadows, IBL and GI off; TAA and bloom off.
  - Capture the HDR viewport in Raster with openpbr on, then in Full RT. Compare object pixels only (object-id mask, one-pixel silhouette border excluded).

```python
for m in ("default", "coat", "fuzz", "metal_f82", "thin_film", "anisotropy", "checker"):
    assert max_encoded_diff(raster[m], fullrt[m], mask[m]) <= 2   # spec item 3 tolerance
assert mean_encoded_diff(raster["subsurface"], fullrt["subsurface"], mask) <= 2
assert max_encoded_diff(raster["subsurface"], fullrt["subsurface"], mask) <= 16
assert legacy_fullrt_coat_diff >= 8   # the legacy-mirror hit misses the coat, so the gate can fail
```

  - Subsurface has its own bound because its curvature differs by construction.
    - The raster side uses per-pixel `length(fwidth(N)) / length(fwidth(P))`, an L1 screen-space ratio.
    - The hit uses the per-triangle edge ratio.
    - Both approach 1/R on a sphere, but not to 2/255.
  - The bounds above are this plan's proposal. Report the measured max and mean. Changing a bound needs the user's approval.

- [ ] **Step 3: Write the failing gate**, part `reflections`.
  - Setup: Raster mode, reflections on, a legacy mirror floor (metallic 1, roughness 0), and a sphere whose `base_color` is connected red through a `constant` node. Its legacy mirror therefore stays white.
  - Read `LR_Rays` over the sphere's reflection.

```python
assert mean_r / mean_g >= 4.0 and mean_r / mean_b >= 4.0
```

  - Then swap materials between this sphere and a second sphere connected blue. Both use already-enrolled modules, so the module list is unchanged. The reflections must swap colours, which proves offsets follow the packing and not only the list.

```python
assert ratio_rg(left_after_swap) <= 0.25 and ratio_bg(left_after_swap) >= 4.0
```

- [ ] **Step 4: Run both parts and confirm they fail.** Expected: FAIL, because hits are still legacy.
- [ ] **Step 5: Implement the interfaces above.**
  - Write the set-1 bindings 4-8 only `if (desc->HasBinding(n))`.
  - `pe_materialBytes` is `scene.GetMaterialByteBuffer()`; the samplers are `GetOpenPBRSamplers()`.
  - The environment comes from the skybox's `BuildOpenPBREnvironment` images and `MaterialShadePass`'s environment sampler.
  - `cb_envSamples` comes from `openpbr_env_samples`.
- [ ] **Step 6: Run the gates and the churn check.**
  - Both parts of `openpbr_rt_hits.py` must PASS on Vulkan and DX12.
  - Then run a 20-iteration add/remove/reassign loop of three modules with reflections and GI on, under Vulkan validation and DX12 GBV. Expected: no device loss and clean logs.
- [ ] **Step 7: Hot-reload check.** Append a comment to the sandbox build tree's `RayTrace.hlsl`, the file the passes create their shaders from, and wait for the shader reload. `RenderPassShaderReload.cpp` rebuilds every captured hit group. The `reflections` assertion must still PASS. Restore the file.
- [ ] **Step 8: Off-identity.** All Task 0 Step 1 cases on both APIs must be byte-identical, or within their recorded self-difference.
- [ ] **Step 9: Checkpoint.** Draft: "Run MaterialX graphs at RT hits through per-module hit groups".

### Task 4: GI-probe hits through the surface-cache fold (Decision 1 = B)

**Files:**
- Modify: `RTCommon.hlsl`, `RayTrace.hlsl` (`closesthit` 959-1061) and `MaterialHitMain.slang`.
- Test: `openpbr_rt_hits.py`, new part `gi`.

**Interfaces:**
- Produces, in `RTCommon.hlsl`, two functions extracted from the legacy closest-hit and called by it in its existing order:
  - `void RT_FoldSurfaceCache(inout float3 albedo, inout float metallic, inout float roughness, inout float3 F0)`: the `#ifdef GI_PROBES` block.
  - `float3 RT_LightOpaque(float3 P, float3 N, float3 V, float3 albedo, float metallic, float roughness, float3 F0, float occlusion)`: from the energy compensation to the end of the area-light loop, without emission.
- `pe_hit_closest` under `defined(GI_PROBES) && !defined(REFLECTIONS)` does the following:
  - Evaluates `p = pe_material_inputs(sp.nextUv, sp.spriteBlend)`.
  - Takes albedo = `p.base_color * p.base_weight`, metallic = `p.base_metalness`, roughness = `p.specular_roughness`, N = `normalize(p.geometry_normal)`, and F0 = `lerp(0.04, albedo, metallic)`.
  - Folds, lights with `RT_LightOpaque`, and adds `p.emission_color * p.emission_luminance`.

- [ ] **Step 1: Write the failing gate**, part `gi`.
  - Setup: Raster mode, GI probes on, screen probes off. A white legacy wall stands in shadow from the only light and receives bounce light only. Next to it is a lit OpenPBR plane.
  - (a) The plane's `base_color` is connected red, and its legacy mirror is white.
  - (b) The plane uses constant inputs equal to its mirror.
  - Read wall pixels after a fixed, documented frame count from load.
  - Probe rays rotate with `gi_frame`, so for case (b) capture the off reference twice. Its self-difference, recorded, is that comparison's tolerance.

```python
assert ratio_rg(wall_on_connected) >= 2.0 and abs(ratio_rg(wall_off_connected) - 1.0) <= 0.05
assert max_encoded_diff(wall_on_constant, wall_off_constant) <= max(1, self_diff(wall_off_constant))  # fold parity
```

- [ ] **Step 2: Run it and confirm it fails.** Expected: FAIL on (a), because Task 3's GI variant runs the full shade.
- [ ] **Step 3: Implement the extraction and the GI branch.** If extracting from the legacy closest-hit changes the off-identity bytes, keep the legacy body untouched and give the hit a copy instead. Record why.
- [ ] **Step 4: Run the tests.** `gi`, `fullrt` and `reflections` must PASS on both APIs. Task 0 off-identity must be byte-identical.
- [ ] **Step 5: Checkpoint.** Draft: "Light MaterialX GI-probe hits through the surface-cache fold".

### Task 5: Generated mask at RT any-hit

**Files:**
- Modify: `MaterialHitMain.slang`, `RayTracingPass.cpp` (module groups use `pe_hit_any`).
- Test: `openpbr_rt_hits.py`, new part `mask`.

**Interfaces:**
- Produces `[shader("anyhit")] void pe_hit_any(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)`. It reconstructs as `pe_hit_closest` does and computes `a = pe_material_mask(sp, sp.nextUv, sp.spriteBlend)`, with cutoff `constants[id].alphaCut`.
  - Under the Phase 4 rule, `a <= 0 || a < cutoff` calls `IgnoreHit()`.
  - Otherwise, if `payload.rayType == 1 && a < 1`, it multiplies `payload.radiance` by `1 - a` and calls `IgnoreHit()`, as the legacy any-hit does.
  - Triangles where `PeUsesGeneratedMask` is false keep the legacy alpha test, as in `MaterialMaskMain.slang`.

- [ ] **Step 1: Write the failing gate**, part `mask`.
  - Setup: the Phase 4 checker-opacity quad, with legacy alpha deliberately 1 and the checker kept off the shadow-sample grid as in `openpbr_mask_cases.py`.
  - (a) Full-RT coverage against Raster coverage.
  - (b) Full RT with shadows on: count lit pixels in the quad's shadow on a legacy floor.

```python
assert coverage_mismatch(raster, fullrt) <= cell_edge_pixels(quad)
assert 0.35 <= lit_fraction(fullrt_shadow) <= 0.65 and lit_fraction(off_shadow) <= 0.05
```

- [ ] **Step 2: Run it and confirm it fails.** Expected: FAIL, because the legacy alpha of 1 covers the whole quad.
- [ ] **Step 3: Implement `pe_hit_any`** and switch the module groups to it.
- [ ] **Step 4: Run the tests.** The `mask`, `fullrt`, `reflections` and `gi` parts and `openpbr_mask.py` on both APIs must PASS.
- [ ] **Step 5: Checkpoint.** Draft: "Test generated MaterialX masks at RT any-hit".

### Task 6: Lifecycle, persistence, Player and validation

**Files:**
- Create: `.testpool/tests/openpbr_rt_lifecycle.py`
- Modify: `.testpool/catalog.json`, adding `openpbr-rt-hits(-dx12)` and `openpbr-rt-lifecycle(-dx12)`.

- [ ] **Step 1: Write the lifecycle gate.**
  - Mixed scene: legacy, two modules, cutout, and a hidden node. GI, reflections and screen probes on.
  - Loop 10 times, in this order: switch Raster → Hybrid → Full RT; toggle openpbr off and on; assign and remove a module; hide and show a node.
  - Then compare against a fresh load of the final saved state. Also run save/load, play/stop, and the exported Player with the same RT settings.
  - Comparisons run at two levels:
    - With GI, reflections and screen probes off (deterministic), and with Full RT and direct light only: ≤ 1/255.
    - With them on: every image is captured at a fixed frame count from load. The tolerance is the self-difference of two fresh-load captures of the final state, recorded per API.

```python
assert no_device_lost and clean_log(validation=True)
assert max_encoded_diff(after_churn_det, fresh_det) <= 1 and max_encoded_diff(player_det, editor_det) <= 1
tol = self_diff(fresh_gi_a, fresh_gi_b)
assert max_encoded_diff(after_churn_gi, fresh_gi_a) <= tol and max_encoded_diff(player_gi, editor_gi) <= tol
```

- [ ] **Step 2: Run it on both APIs**, plus the Vulkan-validation and DX12 GBV variants. Expected: PASS. Fix the causes; never loosen tolerances without the user's approval.
- [ ] **Step 3: Rerun the existing gates.**
  - Phase 2 to 4: parity, vis, graphs, textures, persist, masks, sprites, skinning, export-player, combined 4.4.
  - On both APIs; all must PASS.
- [ ] **Step 4: Checkpoint.** No engine diff is expected; record the evidence in `.testpool/runs/claude-p5/`.

### Task 7: Performance

**Files:**
- Create: `.testpool/tests/openpbr_rt_perf.py`, reusing `openpbr_final_perf.py`'s ten-snapshot capture.

- [ ] **Step 1: Off.** Saved Sponza, compared against the Task 0 controls with `tools/compare_snapshots.py`. Expected: PASS on both APIs.
- [ ] **Step 2: On, measured and reported.**
  - Setup: 100 MaterialX planes with reflections and GI on; legacy materials versus 1, 4 and 16 modules.
  - Report GPU ms for `LR_Tracing`, `GI_ProbeTracing`, `SP_ProbeTracing` and `RayTracingPipeline`, and RT pipeline build time, cold and warm.
- [ ] **Step 3: Record the numbers** in the wiki (Task 8). Do not gate on them. Ask the user before optimizing.

### Task 8: Documentation

**Files:**
- Modify: `docs/wiki/architecture/rendering.md`. Update the OpenPBR section: replace line 518's "RT hits use the mirrored legacy material…", the two-material-copies note at 557, and the tile-binning limits.
- Modify: the spec's "Other paths" section. Add a dated amendment recording Phase 5's model and limits:
  - mip 0 at hits;
  - subsurface curvature from the hit triangle;
  - skinned meshes and sprites are still absent from the TLAS (pre-existing, `SceneRayTracing.cpp:331`, :523-535);
  - legacy hits ignore OpenPBR's `cb_linearColor`;
  - GI fold policy per Decision 1.

- [ ] **Step 1: Write the pages** with source references and the verification date.
- [ ] **Step 2: Lint.** Run `bash docs/wiki/tools/lint.sh`. Expected: no findings.
- [ ] **Step 3: Checkpoint.** Draft: "Document OpenPBR Phase 5 hit groups".

## Out of scope

- Ray cones and texture LOD at hits (mip 0, as legacy).
- Skinned meshes and sprites in the TLAS.
- Transparent/transmission MaterialX (Phase 6).
- Hit-group performance work such as dedup and compaction (Phase 7).
- Android RT.
