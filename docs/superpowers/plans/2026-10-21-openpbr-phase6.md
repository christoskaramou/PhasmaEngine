# OpenPBR Phase 6 — Transparent and transmission forward pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Status: implemented 2026-10-21. The user chose Decision 2 = A and Native execution. Starting point: `2af9bf37` (Phase 5) on `master`. No commit is authorized.

Deviations, all ruled in the ledger `.superpowers/sdd/2026-10-21-openpbr-phase6/progress.md`:
- Hybrid: forward meshes take TLAS mask 0x02 while OpenPBR is effective. Hybrid's transparent-only primary trace painted their legacy hit over the pass, which Review Focus 3 had not foreseen.
- No `MaterialForwardVS.hlsl`: the pass compiles `GBufferVS.hlsl` with `PE_OPENPBR_FORWARD`, with its own resource names for DX12's source-binding scan.
- The tangent-sign fallback reads per-vertex UVs instead of vertex ids.
- The legacy skip lives in a `GBUFFER_TRANSPARENT` permutation, so the opaque G-buffer shader is unchanged.
- The pass lives in `LightPass.h/.cpp` as a `MaterialShadePass` subclass.
- Final review: the pass runs at sort 805, after Hybrid's `RayTracing` (800) writes the viewport. At 710 an alpha-cut or legacy transparent surface behind MaterialX glass erased the blend. The byte repack also refreshes TLAS masks for forward meshes.

**Goal:** Meshes with a MaterialX material whose render type is alpha-blend or transmission are shaded by MaterialX's generated code in a forward pixel shader, blended over the lit scene, instead of by the legacy transparent G-buffer with mirrored values.

**Architecture:**
- A new per-module raster pass, `OpenPBRForwardPass`, runs after `LightTransparent` (700) at sort 710.
- Its vertex shader is `GBufferVS.hlsl` (joints, sprite UVs, TAA jitter). Its pixel shader is a wrapper: a forward head with pass 2's lights, shadows and environment hooks, the generated module, and a forward main that calls `pe_material_inputs` and `pe_shade_inputs`.
- It draws a CPU list of forward meshes sorted far to near, with depth test on and depth write off.
- When `openpbr` is effective, the legacy transparent G-buffer skips those meshes, gated at runtime by a push constant.
- Alpha is the evaluated `geometry_opacity` input. No generated text changes, so there is no module ABI bump.

**Tech Stack:** C++20 engine, HLSL (DXC) for `GBufferVS.hlsl`, Slang for MaterialX wrappers, Vulkan and DX12 raster pipelines, MaterialX 1.39.5 / OpenPBR 1.1.1, the `.testpool` Python harness over the editor MCP.

**Spec:** [2026-10-13-openpbr-design.md](../specs/2026-10-13-openpbr-design.md). The relevant sections are "The reference" ("Refraction: Environment map only"), "Other paths" ("Transparent / alpha-blend / transmission surfaces (Phase 6): the generated code in a forward pixel shader, where hardware derivatives are native"), Switch, Verification and Phases. Earlier plans in this folder define the harness this one reuses.

## Settled by the spec: transmission is MaterialX's environment-only refraction

The generator keeps `hwTransmissionRenderMethod = TRANSMISSION_REFRACTION` (`MaterialXGen.cpp:373`). Under it, MaterialX adds the transmission lobe to the surface colour, not to its transparency (`HwSurfaceNode.cpp:246-253`). Transparency comes only from `geometry_opacity`:
- the colour is multiplied by opacity;
- transparency becomes `mix(1, 0, opacity)` (`HwSurfaceNode.cpp:257-260`).

As a result:
- **Alpha.** The output alpha MaterialX writes, `1 - dot(transparency, 0.3333)` (`GlslShaderGenerator.cpp:551`), equals the evaluated `geometry_opacity` to within 1e-4. The forward main reads it from `PeSurfaceInputs.geometry_opacity`. MaterialX discards alpha below 0.001 (`HwShaderGenerator.cpp:179`), so the forward main does too.
- **Transmission meshes.** A transmission mesh with opacity 1 comes out opaque, showing the environment refracted through it but not the scene behind.

**User-visible consequence.** With `openpbr` on, a MaterialX mesh imported as transmission (glTF `KHR_materials_transmission`) stops showing the scene behind it. Today's legacy path partly blends the scene through. This follows the spec's reference.

## Decision 2 — the blend state (user chose A, 2026-10-21)

The generated colour already includes the factor `opacity`.

- **A (recommended): MaterialX's own blend.**
  - Blend `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` over that colour, as `GlslRenderer` (`GlslRenderer.cpp:197-198`) and MaterialX View (`RenderPipelineGL.cpp:426-427`) do.
  - A surface at opacity 0.5 contributes 25% of its lit colour, over 50% of the background.
  - This matches the agreed reference renderer pixel for pixel.
- **B: premultiplied blend.**
  - Blend `ONE, ONE_MINUS_SRC_ALPHA`, so opacity 0.5 contributes 50%, which is what OpenPBR defines `geometry_opacity` as (coverage).
  - It deliberately deviates from MaterialX's renderers. The parity test then compares against the reference's colour, not its blended framebuffer.

**How the encoding follows the choice.** With `linear_color` off the frame is display-referred, and pass 2 encodes its colour with `LinearToSrgb` (`MaterialShadeMain.slang:45-47`).
- Under A, the forward main encodes the generated colour as is. That is MaterialX's `hwSrgbEncodeOutput` order (`GlslShaderGenerator.cpp:545-547`).
- Under B, it encodes the colour divided by alpha, then multiplies by alpha again.

The tasks are written for A. Under B only Task 3 Step 3's blend line and Task 4's expected value change.

## Global Constraints

- **Off.** The `openpbr` scene setting defaults to false. "Off: output is byte-identical to the pre-change binary, with no new targets or passes." This means identical output, not identical shader binaries: the runtime gate in `GBufferVS.hlsl` is allowed.
- **Generated code only.** The forward pass runs MaterialX's generated code, with no hand-written BSDF. PE supplies lights, shadows and environment through the `pe_*` seams in `PePrelude.slang`, as pass 2 does.
- **Android:** "the legacy path only."
- **Logs.** Verification item 5: "Clean logs, no validation errors".
- **Tests: the user's standing directive is crucial tests only** (2026-10-21: "keep only crucial tests"; 2026-10-20: "limit the tests to what you change", "make them as fast as possible", "use full res for tests").
  - One run per API per gate.
  - No repeated captures, toggle matrices or multi-scene baselines.
  - Render scale 1.
- **Sandbox.** Build and test only in `C:\Users\Christos\pe-p4-sandbox`, under the shared GPU lock (`%TEMP%/pe-gpu-lock`, `fair_lock.sh`), on display 1. Never in `build-ninja-physics`. Start only after the RT-stall session has released the lock.
- **Other sessions' files.** Phase 6 does not edit files with another session's unstaged work:
  - `Shader.cpp`, `ShaderCache.cpp/.h` and `RenderPassShaderReload.cpp` (shader hot reload);
  - `RayTracingPass.*`, `SceneRendererCore.*` and any file the RT-stall session reports touching;
  - in `docs/wiki/architecture/rendering.md`, only the new Phase 6 section.
- **Hygiene.** No mouse injection. Every visual gate saves a contact sheet that I view. No test-only engine code. `third_party/` is off-limits. Run `clang-format -i` on every touched `.cpp`/`.h`, and follow the CLAUDE.md layout rules in shaders too. DX12 API conservatism: no new public RHI surface.
- **Commits.** None unless the user says so. Each task ends at a checkpoint with a drafted one-line message.

## Review Focus

1. **The draw list follows culling's visibility rules.** A mesh is skipped when its node is hidden (`set_visible(false)`), the off-screen animation cull has cleared it, its hierarchy is disabled, it has `gpuPending` set, or its `indexCount` is 0. The expected result is that such a mesh is absent, as in the legacy path. Task 4 Step 1 (`order` part) hides one plane.
2. **Render-type and module churn.** Flipping a mesh opaque ↔ alpha-blend, assigning or removing a `.mtlx`, toggling `openpbr`, and releasing then re-initializing the pass (pass-release sweep) must move the mesh between the visibility path and the forward pass in the same frame. The expected result is no frame with a double draw, no device loss and no validation error. Task 4 Step 1 (`parity` part) flips one render type.
3. **Full RT and Hybrid.**
   - The forward pass is enabled exactly where `LightTransparent` is, plus the `openpbr` gate.
   - In Full RT, MaterialX transparents keep their legacy hit groups.
   - The expected result is no crash and Hybrid behaving as legacy transparents do. Checked by review; no test.
4. **Skinned and sprite forward meshes.** The forward PS consumes `GBufferVS`'s joint and sprite outputs (`nextUv`, `spriteBlend`). The expected result is that an animated alpha-blend MaterialX mesh deforms and a sprite's frame blend matches the legacy UVs. Checked by review; no test.
5. **Opacity from a texture** (glTF BLEND base-colour alpha). Alpha is evaluated per pixel with native derivatives. The expected result is correct texture mips at grazing angles. Checked by review; no test.

---

### Task 0: Pre-change reference capture

**Files:**
- Create: `.testpool/tests/openpbr_forward.py` (`--part baseline` only in this task)
- Reuse: `openpbr_rt_baseline.py` (`lua_patient`, `lua_query`, `tool_patient`, `editor_ready`), `openpbr_baseline.py` (`full_shot`), `hdr_io.py`

- [ ] **Step 1: Write the fixture and the capture.** Build the scene in a temp project:
  - a floor plane;
  - a legacy alpha-blend cube (legacy material, `render_type` `alpha_blend`, alpha 0.5);
  - a MaterialX alpha-blend sphere (`geometry_opacity` 0.5);
  - a MaterialX transmission sphere (`transmission_weight` 1);
  - one directional light.

  Use `openpbr` off, TAA, GI and SSR off, and render scale 1. Capture the viewport once at full resolution per API into `.testpool/baselines/openpbr-forward/{vulkan,dx12}/off.png`, with `meta.json` holding the build hash.
- [ ] **Step 2: Run it** on the pre-change sandbox build: `py -3.12 -X utf8 openpbr_forward.py --api vulkan --part baseline`, then `--api dx12`.
  - Expected: two PNGs, both meshes visible, and a clean log. View the contact sheet.

### Task 1: Route forward meshes and skip them in the legacy transparent G-buffer

**Files:**
- Modify: `Phasma/Runtime/Code/Scene/OpenPBRMaterial.h/.cpp` (around line 679)
- Modify: `Phasma/Runtime/Code/Scene/SceneBuffers.cpp` (call sites below), `Scene.h` (around line 591)
- Modify: `Phasma/Runtime/Code/RenderPasses/GbufferPass.cpp` (push constants)
- Modify: `Phasma/Runtime/RuntimeAssets/Shaders/Gbuffer/GBufferVS.hlsl`, `Shaders/Common/Structures.hlsl` (`PushConstants_GBuffer`), `Shaders/MaterialX/VisibilityCommon.slang` (flag constant)

**Interfaces:**
- Produces: `bool MeshUsesOpenPBRForward(const Mesh &mesh)`, which requires `RenderType::AlphaBlend` or `RenderType::Transmission` and `OpenPBREligible`.
- Produces: `const std::vector<uint32_t> &Scene::GetOpenPBRForwardModules() const`.
- Produces: `PE_EDITOR_FLAG_MATERIALX_FORWARD = 128u`. Grep first that no C++ or shader site uses bit 128.
- Produces: `uint openpbrForward` in `PushConstants_GBuffer`.

- [ ] **Step 1: Route each call site.** `MeshUsesOpenPBR` keeps its meaning (visibility path, opaque and alpha-cut). Do not widen it.

  | Site | Purpose | Predicate after |
  |---|---|---|
  | `SceneBuffers.cpp:36` | skinning (flag 64) | unchanged (opaque only) |
  | `:58` | byte-layout dirty check | `MeshUsesOpenPBR \|\| MeshUsesOpenPBRForward` |
  | `:201`, `:2813` | flag 32 | unchanged; add flag 128 for `MeshUsesOpenPBRForward` |
  | `:913`, `:974` | bindless image residency | either predicate |
  | `:1277`, `:1639` | byte packing | either predicate |
  | `:1299` | `m_openpbrModules` (material passes, RT hit groups) | only `MeshUsesOpenPBR`; forward modules go to `m_openpbrForwardModules` |
  | `SceneRayTracing.cpp:30` | RT hit group | unchanged (forward meshes stay legacy at hits) |
- [ ] **Step 2: Add the runtime skip.**
  - `GbufferTransparentPass::ExecutePass` sets `pushConstants.openpbrForward = SceneUsesOpenPBR() ? 1u : 0u`. `GbufferOpaquePass` leaves it 0.
  - In `GBufferVS.hlsl`, when `pc.openpbrForward != 0` and `(constants[id].editorFlags & 128u) != 0`, output `float4(0, 0, -1, 1)`. This is the off-screen rejection `MaterialMaskVS.hlsl` uses.
- [ ] **Step 3: Build** in the sandbox (`sandbox_build_full.cmd`).
  - Expected: no warnings in touched files.
- [ ] **Step 4: Check off-identity.** Write the test's `identity` part now: it loads Task 0's fixture with `openpbr` off and compares against `off.png` byte for byte. Run `openpbr_forward.py --api vulkan --part identity`.
  - Expected: PASS, 0 differing pixels.
- [ ] **Step 5: Checkpoint.** Draft message: `Route MaterialX alpha-blend and transmission meshes out of the legacy transparent G-buffer when OpenPBR is on`.

### Task 2: Forward wrapper, shared raster surface and module registration

**Files:**
- Create: `Phasma/Runtime/RuntimeAssets/Shaders/MaterialX/MaterialForwardHead.slang`, `MaterialForwardMain.slang`, `MaterialForward.slang` (the default-module wrapper, like `MaterialShade.slang`), `MaterialForwardVS.hlsl`
- Modify: `Shaders/MaterialX/VisibilityCommon.slang` (move the raster surface here), `MaterialMaskMain.slang` (call it), `PePrelude.slang` (skip the screen-probe lookup at :100 and the traced-reflection lookup at :128 under `PE_FORWARD`)
- Modify: `Shaders/Gbuffer/GBufferVS.hlsl` (binding macros), `Shaders/Common/Structures.hlsl` (`PushConstants_Lighting` forward fields, `VS_OUTPUT_Gbuffer` vertex id under `PE_OPENPBR_FORWARD`)
- Modify: `Phasma/Runtime/Code/Scene/OpenPBRMaterial.h/.cpp` (`forwardSource`, `RegisterModule`)
- Test: `.testpool/tests/mtlx_hit_codegen.py`, extended with the forward wrapper

**Interfaces:**
- Produces: `PeSurfacePoint PeRasterSurface(float4 svPosition, float3 P, float3 N, float4 tangent, float2 uv, float2 nextUv, float spriteBlend, float4 color, float tangentSign)`.
  - This is `PeMaskSurface`'s body. The mask passes `PeMaskTangentSign(vertices)`; the forward PS passes the same function over its `GetAttributeAtVertex` vertex ids.
- Produces: `std::string OpenPBRModule::forwardSource`. `RegisterModule` writes `<hash>.forward.slang` with `WriteWrapper(module->forwardSource, modulePath, "MaterialForward")`. Module 0 uses `dir + "MaterialForward.slang"`.
- Produces: under `#ifdef PE_OPENPBR_FORWARD`, `PushConstants_Lighting` gains `vertices_offset`, `env_samples`, `jointsCount` and `projJitter`.
  - It stays at or below Vulkan's guaranteed 128 bytes, and dword comments pin the offsets.
  - The forward vertex shader gets `prevProjJitter = 0`, since forward pixels write no velocity.

- [ ] **Step 1: Write the failing test.** Extend `mtlx_hit_codegen.py`: for the default module and every Phase 3 fixture graph, compile `<hash>.forward.slang` (`mainPS`) and `MaterialForwardVS.hlsl` (`mainVS`) to SPIR-V and DXIL with the forward defines.
  - Assert that both compile, and that the pixel shader's reflected bindings match the binding map below.
- [ ] **Step 2: Run it.**
  - Expected: FAIL, "MaterialForward.slang not found".
- [ ] **Step 3: Write the binding map.** It holds within Vulkan's four guaranteed sets, and DX12 maps binding N to register N.

  | Set | Bindings | Resource | Source |
  |---|---|---|---|
  | 0 | 0-13 | `Lighting.hlsl` (G-buffer textures unused, lights, Forward+ lists, `PassUBO`) | as `MaterialShadeHead` |
  | 0 | 15 / 16 / 17 / 18 / 20 | geometry (tangent-sign fallback), frame data, `Mesh_Constants`, material bytes, deformed vertices | shade head's numbers; the vertex shader's `data`/`constants`/`pe_deformed` map here |
  | 0 | 22 | DX12 skin draws | was `GBufferVS` (2, 0) |
  | 1 | 0-2 | shadows | `Lighting.hlsl` |
  | 2 | 0 | scene texture table | as shade head |
  | 3 | 0-5, 6-10 | probes; fog cube, env sampler, graph samplers, env radiance and irradiance | as shade head |

  `GBufferVS.hlsl` takes its four bindings and `pc` from macros whose defaults keep today's values, so the legacy binaries are unchanged. `MaterialForwardVS.hlsl` defines the macros with the numbers above and includes it.
- [ ] **Step 4: Write `MaterialForwardMain.slang`'s `mainPS(VS_OUTPUT_Gbuffer input, bool front : SV_IsFrontFace) : SV_TARGET0`.**
  - Build the surface: `sp = PeRasterSurface(...)`.
  - Load the material: `pe_load_material(pe_materialBytes, constants[input.id].materialByteOffset + PE_OPENPBR_INPUTS_OFFSET)`.
  - Shade: call `pe_fill_vertex_data(sp)`, then `p = pe_material_inputs(sp.nextUv, sp.spriteBlend)`, `color = pe_shade_inputs(p).rgb` and `alpha = saturate(p.geometry_opacity)`. Discard when `alpha < 0.001`.
  - Hooks: `pe_ssao = 1`, `pe_occlusion = 1`. Screen probes and traced reflections are off for forward pixels, as `LightingPS` does for `passType != 0`. The head defines `PE_FORWARD`, which `PePrelude.slang`'s two lookups test.
  - Fog: `color = lerp(color, fogColor * alpha, fogAmount)`, which is the premultiplied form of pass 2's haze.
  - Encoding: under `cb_linearColor == 0`, `color = LinearToSrgb(color)` (Decision 2 = A).
  - Return `float4(color, alpha)`, with NaN and Inf replaced by 0.
- [ ] **Step 5: Run the test.**
  - Expected: PASS on every graph, both targets.
- [ ] **Step 6: Check the mask regression:** `py -3.12 -X utf8 .testpool/tests/mtlx_mask_codegen.py`.
  - Expected: PASS (`PeRasterSurface` refactor).
- [ ] **Step 7: Checkpoint.** Draft message: `Add the MaterialX forward wrapper with PE lights and environment over GBufferVS`.

### Task 3: `OpenPBRForwardPass`

**Files:**
- Create: `Phasma/Runtime/Code/RenderPasses/OpenPBRForwardPass.h/.cpp`
- Modify: `Phasma/Runtime/Code/Render/SceneRenderGraph.h/.cpp` (pass id, sort 710, components, both enable lists at around `:589` and `:643`, `SetPassScene`)
- Modify: `Phasma/Runtime/Code/RenderPasses/LightPass.cpp` only if `MaterialShadePass`'s descriptor writes must be extracted into a shared free function
- Test: `.testpool/tests/openpbr_forward.py` (`--part parity`)

**Interfaces:**
- Consumes: `Scene::GetOpenPBRForwardModules()` and `MeshUsesOpenPBRForward` (Task 1); `OpenPBRModule::forwardSource` (Task 2).
- Produces: `class OpenPBRForwardPass : public IRenderPassComponent`, with one `PassInfo` per forward module, structured like `OpenPBRMask` (`Sync`, `Rebuild`, `Clear`).

- [ ] **Step 1: Write the failing test** (`--part parity`, full resolution, `linear_color` on, IBL off).
  - Cases: opacity 0.5 spheres for the default material and the coat case, as MaterialX alpha-blend meshes.
  - For each case, capture the HDR viewport with the sphere visible (`O`) and hidden (`Bg`).
  - Render the reference with the existing `mtlx_reference_full.py`. Its output `R` is the generated colour, which includes the opacity factor.
  - Assert `O - (1 - a) * Bg` matches `R * a` (Decision 2 = A) within the direct-light parity tolerance (2/255 encoded), over the sphere mask.
- [ ] **Step 2: Run it.**
  - Expected: FAIL. The sphere is absent: Task 1 skips it and nothing draws it yet.
- [ ] **Step 3: Implement the pass.**
  - Raster state: one colour attachment (`viewport`, load) plus depth (load, test on, write off), with the depth test, compare op and cull mode of `standard_pbr.pass` `transparent`. Blend is `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`, and alpha keeps the destination's.
  - Draw list: each frame, walk the scene's meshes under Review Focus 1's rules. Sort far to near by `distance(camPos, world bounds centre)`, `CullingCS`'s key (`CullingCS.hlsl:499`). Issue `DrawIndexed(indexCount, 1, firstIndex, vertexOffset, meshConstantsSlot)` with the draw-id buffer bound, switching pipeline when the module changes.
  - Pipelines: built only for modules in `GetOpenPBRForwardModules()`, through the same pipeline path as `MaterialShadePass`. *Corrected after review:* that path compiles synchronously on the main thread, as pass 2 does; the RT-stall session's async fix covers RT hit groups only.
  - Lifetime: `Destroy` clears the pipelines and the module list (the Phase 5 re-init lesson).
  - Enable: the pass runs where `LightTransparent` does and `SceneUsesOpenPBR()` holds, and only when the forward module list is non-empty.
- [ ] **Step 4: Run the test.**
  - Expected: PASS on Vulkan. View the contact sheet (O, Bg, R, difference).
- [ ] **Step 5: Checkpoint.** Draft message: `Shade MaterialX alpha-blend and transmission meshes in a sorted forward pass`.

### Task 4: Verification gate

**Files:**
- Modify: `.testpool/tests/openpbr_forward.py` (remaining parts), `.testpool/catalog.json` (`openpbr-forward`, `openpbr-forward-dx12`)

- [ ] **Step 1: Finish the test's parts.** One script, one editor launch per API.
  - `identity`: Task 0's fixture with `openpbr` off is byte-identical to `off.png`.
  - `parity` (Task 3), plus one render-type flip: the default sphere goes opaque, then back to alpha-blend. Its pixels must equal the visibility path's when opaque and Task 3's when blended, with no frame lost in between.
  - `transmission`: the existing environment parity case `transmission` from `openpbr_parity.py`, rendered as a MaterialX transmission mesh, at its own `ENV_TOLERANCE` row (`--pe-samples 32 --ref-samples 32`).
  - `order`: two overlapping opacity 0.5 planes from different modules (red and blue). Viewed from each side, the composite equals far-then-near blending to within 2/255. Hiding the near plane leaves only the far one.
  - A log check: no `[ERROR]`, no validation messages.
- [ ] **Step 2: Run it once per API:** `openpbr_forward.py --api vulkan --part all`, then `--api dx12`.
  - Expected: PASS on both APIs, with contact sheets viewed.
- [ ] **Step 3: Measure Sponza off-mode perf once, on Vulkan** (`GBufferVS.hlsl` is a shared off-path shader): `openpbr_rt_perf.py --api vulkan --out <new>`, comparing against a capture from the Task 0 build with `tools/compare_snapshots.py`.
  - Expected: within the CLAUDE.md thresholds.
- [ ] **Step 4: Register both catalog entries.** `paths` cover the Task 1-3 files. Run `node "C:/Users/Christos/.claude/skills/test-pool/scripts/testpool.cjs" validate`.
  - Expected: `valid`.

### Task 5: Documentation

**Files:**
- Modify: `docs/wiki/architecture/rendering.md` (new section "Phase 6 forward pass"; Limits line), `docs/superpowers/specs/2026-10-13-openpbr-design.md` (dated amendment under "Other paths")

- [ ] **Step 1: Write the wiki section.** Cover:
  - routing and the call-site table;
  - the wrapper and binding map;
  - alpha = `geometry_opacity` under REFRACTION, with the MaterialX source lines;
  - the blend decision;
  - draw order;
  - limits, each with a verification date and the exact cases tested.
- [ ] **Step 2: Lint:** `bash docs/wiki/tools/lint.sh`.
  - Expected: clean.
- [ ] **Step 3: Checkpoint.** Draft one message covering Tasks 1-5. Commit only when the user says so.

## Out of scope (stated limits)

- **Seeing the scene through glass.** Screen-space or ray-traced refraction is excluded, because the spec's reference refracts the environment only. MaterialX's `TRANSMISSION_OPACITY` method is also excluded: it is a global generator option and would regenerate every module.
- **Transparent layers.** No order-independent transparency. Sorting is per mesh. A MaterialX transparent behind legacy glass is hidden by the legacy pass's depth write, the same single-layer limit legacy transparents already have.
- **Velocity and screen effects.** Forward pixels write no velocity, so TAA can ghost moving transparents. SSR, DOF and motion blur see the opaque surface behind them.
- **Ray tracing.** MaterialX transparents keep their legacy hit groups in Full RT, Hybrid and GI.
- **Shadows.** Transparent shadow casting is unchanged.
- **Android:** the legacy path only.
