# OpenPBR via MaterialX on a visibility buffer (design, revision 2)

Date: 2026-10-13. Revision 2 of this spec.

## Revision history

**Revision 1** (same day): hand-written factor-driven OpenPBR lobes in deferred lighting, plus a material-ID G-buffer target. Its plan passed three Codex `gpt-6-astra` xhigh review rounds.

**Revision 2** is a redesign after the user set new requirements:
1. "Use all parameters MaterialX gives us … make it correct 100%."
2. The reference is MaterialX's own `open_pbr_surface` shader.
3. Per-pixel values come from a visibility buffer with a compute material resolve.
4. Materials come from `.mtlx` import, alongside glTF and editor/Lua editing.

The revision-1 plan is deleted. Its hand-written `OpenPBR.hlsl`, the Python oracle, the material-ID target and the `Lighting.hlsl` edits are gone. **What carries over** is listed at the end.

**Amended 2026-10-15** (parity work, [plan](../plans/2026-10-15-openpbr-parity.md)): the requirement is that OpenPBR looks the same as in other MaterialX-based engines, so (a) the reference is MaterialX 1.39.5's own generated shader rendered by `GlslRenderer`, (b) the environment is MaterialX's filtered importance sampling (FIS), not a prefiltered cube, and (c) with `linear_color` off (the default) pass 2 writes sRGB-encoded colour. Environment details are in the amended bullets below and in the wiki's "OpenPBR Materials".

## Goal

Every opaque PE surface with a MaterialX material is shaded by **MaterialX's own generated code** for `open_pbr_surface` (OpenPBR 1.1.1, all 41 inputs, any input driven by a texture or node graph). It matches MaterialX's reference shader per pixel within a stated float tolerance. PE supplies the lights, shadows, Lumen GI and reflections through MaterialX's supported override seams.

## The reference and what it inherits (stated once, factually)

"100% vs MaterialX's shader" means running *MaterialX 1.39.5's generated code*. That code is MaterialX's real-time reduction of the spec ([open_pbr_surface.mtlx, identical to the OpenPBR v1.1.1 reference graph](https://github.com/AcademySoftwareFoundation/MaterialX/blob/v1.39.5/libraries/bxdf/open_pbr_surface.mtlx)). It inherits these approximations:

| Feature | How MaterialX's real-time shader handles it |
|---|---|
| Refraction | Environment map only; the scene behind is ignored (`mx_transmission_refract.glsl`) |
| Volume | Absorption only, no scattering |
| Dispersion | Inputs not connected in the graph, so ignored |
| Subsurface | Burley profile over a ring, with curvature from `fwidth(N)/fwidth(P)` |
| `geometry_thin_walled` | Only selects the subsurface branch |
| Layering | Albedo scaling with GGX energy compensation (not full multi-layer transport) |
| Thin film | Two Airy terms |

Across GL, Vulkan and DX12 the result matches within a float tolerance, not bit-exactly: mips, filtering and transcendental precision differ per API and driver (Codex research, 2026-10-13).

## Architecture

Frame order with `openpbr` on (desktop, capable GPU):

1. Culling, two-phase Hi-Z: unchanged.
2. **Depth prepass + visibility target.** Every prepass draw also writes an RGBA32_UINT `visibility` target. Visibility meshes write `(drawId + 1, v0, v1, v2)`; other draws write 0.
3. **GbufferOpaque**: unchanged for legacy meshes. Terrain and voxel pipelines also write `visibility = 0` where they win depth.
4. **Material pass 1 — thin G-buffer.** A compute resolve loads each MaterialX pixel's constant inputs and calls the generated `pe_material_thin` helper. It writes normal, albedo, srm and zero emissive; velocity and transparency stay the raster G-buffer's, so SSAO, GI, screen probes, reflections, SSR and TAA keep working unchanged.
5. SSAO, Forward+ culling, GI, screen probes, reflections: unchanged.
6. LightOpaque: unchanged. It also lights visibility pixels from the thin G-buffer; step 7 overwrites them.
7. **Material pass 2 — MaterialX shading.** The same graph is evaluated again. MaterialX's closures run inside PE's light loop and PE's environment hooks. The result is written to scene colour for visibility pixels.
8. Transparent, RT, post-processing: unchanged.

**Thin properties before lighting, surface closures afterwards.** MaterialX's inputs feed closures that run inline with the light loop, so the thin G-buffer cannot re-light the full material. Phase 2 includes the full generated module in pass 1 with stub light/environment hooks and calls only `pe_load_material` and `pe_material_thin`. The generator emits that helper when `base_color`, `base_weight`, `specular_roughness`, `base_metalness` and `geometry_normal` are unconnected; no separate reduced graph is generated. Pass 2 loads the same inputs and evaluates MaterialX's surface under PE lighting.

## MaterialX integration

**Dependency.**
- MaterialX **1.39.5** (2026-05-22, Apache-2.0) is pulled in via `FetchContent` with a generator-only build: Core, Format, GenShader, GenHw, GenSlang.
- Measured: the build takes 18 s, and a test executable linking it is 1.68 MB.
- It links into **PhasmaCook** (and the editor in Phase 3, for live import).
- The 2 MB `libraries/` folder is copied next to the tools.
- The Player links nothing new; it loads cooked shaders and parameters.

**Generation.**
- `.mtlx` → `SlangShaderGenerator` (MaterialX 1.39.5's first Slang target) → `.slang` → compiled by PE's existing Slang path (`SlangCompiler.cpp`, `.slang` sources) to SPIR-V and DXIL.
- Verified 2026-10-13: the generated default OpenPBR shader compiles with `slangc` to SPIR-V (passes `spirv-val`) and to DXIL `sm_6_6`.
- Generator options: `SHADER_INTERFACE_COMPLETE` (every input is a uniform) and `targetColorSpaceOverride = lin_rec709`, plus distance units in metres. The transmission method stays `TRANSMISSION_REFRACTION` (MaterialX's default).
- **Pipelines:**
  - One compute pipeline per unique generated graph.
  - With the complete interface, every untextured OpenPBR material shares one pipeline and differs only in uniforms.
- **Uniforms:** stored per material in PE's material byte buffer (copy B), at offsets from the generator's own `.json` layout. That layout is authoritative: tightly packed, with a line-exact `pe_load_material` loader, defined in the [Phases 0–2 plan](../plans/2026-10-13-openpbr-materialx-phase0-2.md).

**Override seams. PE never edits MaterialX or `third_party/`.**
1. **PE surface node.**
   - `PeSurfaceNodeSlang : HwSurfaceNode` overrides the virtual `emitLightLoop` and is registered with `registerImplementation("IM_surface_genslang", …)`.
   - It emits a loop over PE lights through `pe_light_count()` / `pe_light_sample(i, P, out L, out radiance, out occlusion)`. These are declared in a PE prelude and implemented by the resolve shader on top of PE's light buffer, Forward+ tiles and CSM shadows.
   - It is the same seam Pixar's Storm and Maya use.
2. **Search-path shadowing.**
   - MaterialX resolves library includes through its source search path in registration order, first match wins (`GenContext::resolveSourceFile`, `ShaderGenerator::emitLibraryInclude`).
   - MaterialX's library includes are root-relative (`libraries/stdlib/...`), so the search-path roots are folders that *contain* `libraries/`. PE registers `RuntimeAssets/MaterialX/pe_overrides/` *before* the MaterialX root; its files mirror MaterialX's tree under `pe_overrides/libraries/` and shadow them by relative path (prototype-verified 2026-10-13):
     - `libraries/pbrlib/genglsl/lib/mx_environment_fis.glsl` (amended 2026-10-15; was `mx_environment_prefilter.glsl`): MaterialX's own filtered importance sampling, with only its sample count, mip count, LOD, lookups and irradiance replaced by `pe_env_*` hooks over PE's environment, GI and reflection sources. The generator option is `SPECULAR_ENVIRONMENT_FIS`.
     - `libraries/stdlib/genslang/lib/mx_texture.slang`: `SamplerTexture2D` becomes a bindless texture index plus a sampler index. Sampling still uses implicit derivatives (see below).
   - Prototype result (2026-10-13): both overrides are picked up for MaterialX's OpenPBR examples and a textured graph; the override's own include must be root-relative (`libraries/pbrlib/genglsl/lib/mx_microfacet_specular.glsl`). Phase 0 re-proves it in the engine build.
3. **No derivative overrides.** The resolve's quad evaluation (next section) gives pixel-shader-equivalent derivatives, so MaterialX's `texture()`, `fwidth`, `mx_aastep` and its subsurface curvature run unmodified.
4. **Per-source Slang routing.** Today `.slang` sources compile through `slangc` only under the global `--shader-compiler slang` option (`VulkanShaderImpl.cpp:249`, `Dx12ShaderImpl.cpp:592`, `ShaderCache.cpp:105`). The plan routes them by extension, cache key included, while HLSL stays on DXC.

**Assets.**
- A PE `Material` gains a MaterialX source: `.mtlx` path + material name, plus per-input value overrides keyed by OpenPBR input name.
- **glTF import** writes a MaterialX OpenPBR document per material. glTF textures become MaterialX `image` nodes, so every textured glTF input is supported by construction.

  | glTF | OpenPBR input | Fidelity |
  |---|---|---|
  | base colour × texture | `base_color` | exact |
  | metallic/roughness | `base_metalness` / `specular_roughness` | exact |
  | `ior` | `specular_ior` | exact |
  | clearcoat | `coat_weight` / `coat_roughness`, `coat_ior` = 1.5, `coat_darkening` = 0 | glTF has no coat colour or darkening |
  | sheen | `fuzz_weight` = max(colour), `fuzz_color` = colour/max, linear | lossy (Charlie vs Zeltner) |
  | specular | `specular_weight` / `specular_color` | glTF leaves metals untouched |
  | transmission/volume | `transmission_weight` / `transmission_color` / `transmission_depth` | |
  | emissive × strength | `emission_color` × `emission_luminance` | |
  | iridescence | `thin_film_*` | needs a JSON side-parse; assimp has no key |
  | anisotropy | `specular_roughness_anisotropy` | exact per texel but nonlinear; rebake |
  | normal map | `geometry_normal` via `normalmap` | |

- **Persistence:**
  - The scene stores the `.mtlx` reference plus overrides.
  - `.pemesh` stores the material's MaterialX reference and constant inputs. The version bump is designed in Phase 3.
- **Legacy fallback:** a MaterialX material keeps legacy fields (`baseColorFactor`, `metallic`, `roughness`, `ior`, emissive) mirrored from its constant inputs. Android, unsupported GPUs and the off switch render it through the legacy G-buffer with those values.

## Visibility buffer

**Payload.**
- The `visibility` target is RGBA32_UINT (16 B/pixel): `x = drawId + 1` (0 = not a visibility surface), `yzw` = the triangle's three **global vertex indices**.
- The visibility pixel shader reads them with `GetAttributeAtVertex` on a `nointerpolation` vertex index (`SV_Barycentrics` family: D3D12 `BarycentricsSupported`, Vulkan `VK_KHR_fragment_shader_barycentric`).
- **Consequence:** no change to culling, LOD, clusters or skinned instancing. The command-local primitive ID / `firstIndex` problem never arises, because the resolve fetches the three vertices directly. Vertex order does not matter, since the resolve recomputes barycentrics from the three clip-space positions (DAIS).
- Vulkan `SV_VertexID` includes the base vertex. D3D12's does not, so on DX12 the visibility vertex shader adds `IndirectAll[drawId].vertexOffset`. Phase 0 verifies the DXC semantics on both APIs.

**Empty scenes.** The depth pass clears the visibility target on its no-mesh path too, so the resolve never reads stale IDs.

**Who writes it.**
- Opaque and alpha-cut rigid meshes, depth prepass and late prepass, both Hi-Z phases.
- Skinned meshes and sprites write 0 until Phase 4 pre-skins them.
- Terrain and voxel G-buffer pipelines write 0 where they win depth (an extra attachment).

**Caps gate.** `SceneUsesOpenPBR()` = the setting **and** all of:
- not Android (Arm and DevSH 2026 advise against visibility buffers on Mali and Adreno);
- fragment barycentrics;
- compute derivatives (D3D12 SM 6.6; Vulkan `VK_KHR_compute_shader_derivatives` with quad grouping);
- `maxColorAttachments` ≥ 7.

Otherwise: one warning, and the legacy path renders everything. `geometryShader` is not required: the payload uses no `SV_PrimitiveID`, and `PerVertexKHR` needs only `FragmentBarycentricKHR` (Codex review, 2026-10-13).

## Material passes

**Derivatives equivalent to a pixel shader's (within measured tolerance).**
- Each work item is an aligned 2×2 screen quad paired with one triangle covering at least one of its pixels.
- All four lanes evaluate *that* triangle at their own pixel centres, using extrapolated barycentrics, as hardware helper lanes do. Only lanes whose pixel belongs to the triangle write.
- `ddx`/`ddy`/`fwidth` and implicit-LOD `texture()` therefore see what a pixel shader rasterising that triangle would. This holds within a measured tolerance, not universally bit-exact: software interpolation, and coarse vs fine derivatives being unspecified, rule out a guarantee. Phase 1 measures it against the raster G-buffer.
- Interior quads cost one evaluation per pixel; triangle-edge quads cost up to four. This is the visibility-buffer version of hardware quad overshading.
- Requires compute derivatives (gate above).

**Phase 2 scope and storage** (implemented and source-verified 2026-10-14).
- Rigid opaque meshes opt into one build-time default OpenPBR module, with its 37 constant inputs packed into 220 bytes. Textures and connected inputs remain Phase 3.
- A material's byte record starts with the 80-byte legacy `MaterialGpuData`, followed by the generated inputs at `+80`. `Mesh_Constants.materialByteOffset` addresses the record; `editorFlags` mask `32` marks a MaterialX mesh. Both passes leave every unmarked pixel untouched.
- Phase 2 uses full-screen quad dispatches. `material.use_openpbr`, `material.set_openpbr` and `material.get_openpbr` edit instances through Lua; these values are not persisted yet. Legacy mirrors supply the off path and RT hits, which omit the additional MaterialX lobes.

**Binning** (later graph-pipeline work).
1. Count (quad, triangle) items per pipeline bin.
2. Prefix-sum.
3. Scatter.
4. One indirect dispatch per bin, with the bin's pipeline ID taken from the material record.

Empty-bin compaction is deferred to the performance phase.

**Pass 1** writes, through `pe_material_thin`:
- `normal`: the normalised reconstructed surface normal (`geometry_normal` is unconnected in Phase 2);
- `albedo`: `base_color` × `base_weight`, sRGB-encoded when `linear_color`;
- `srm`: (1, `specular_roughness`, `base_metalness`, 0);
- `emissive` = (0, 0, 0, 1).

`velocity` and `transparency` stay the raster G-buffer's: Phase 2 still rasterises these meshes and those values describe the same surface.

**Pass 2** writes scene colour, as MaterialX's surface output under PE's lighting:
- **Lights:** every enabled directional, point, spot and area light through `pe_light_sample`, with PE's attenuation and CSM shadows; Forward+ tiles are unused. MaterialX has no area light, so PE area lights use their centre point with the point-light window (stated limit).
- **Environment irradiance:** LightingPS's blended probe/screen-probe term, plus IBL weighted by `1 - weight` (amended 2026-10-15: the IBL term is the exact cosine convolution of the radiance pyramid, a 128x64 lat-long map in MaterialX's radiance-equivalent convention, not a cube mip).
- **Environment radiance (amended 2026-10-15):** ReflectionPass's traced radiance wherever it has data, for every specular lobe; that trace uses pass 1's roughness. Otherwise MaterialX's FIS with `openpbr_env_samples` samples (default 32) over a lat-long radiance pyramid of the sky (RGBA32F, mips = plain box averages like `glGenerateMipmap`, built from the HDR file's own equirect when the sky has one), using MaterialX's own `mx_latlong_compute_lod`. SSAO affects the environment terms only.
- **Limb normals (amended 2026-10-15):** on a coarse mesh a smooth normal can face away from the eye on a facet that faces it, and MaterialX's BSDFs flip it toward the eye, which leaves a top-lit limb pixel black (MaterialX's own renderer shows the same dots). Pass 2 instead bends such a normal to N.V = 0.01 (`PeBendLimbNormal`) when the facet faces the eye, re-orthogonalising the tangent frame; back faces of double-sided meshes keep MaterialX's flip. Pass 1's G-buffer normal stays the interpolated one.
- Emission and transmission (environment refraction) are MaterialX's.

## Other paths (later phases, stated now)

- **Ray-traced secondary rays** (reflections, GI hits).
  - Correct answer: per-material hit groups that run the generated graph at the hit (UE's hit-lighting model). That is Phase 5.
  - Until then, hits use the legacy-mirrored constant values (base colour, metalness, roughness, IOR, emission) through the legacy hit shading. That is an approximation even for untextured materials: coat, fuzz, thin film and subsurface are absent at hits. The GI fold is as today.
- **Transparent / alpha-blend / transmission surfaces** (Phase 6): the generated code in a forward pixel shader, where hardware derivatives are native.
- **Skinned meshes and sprites** (Phase 4): compute pre-skinning into current and previous vertex buffers (as Horizon Forbidden West does). These feed the visibility raster and the resolve.
- **Alpha-cut:** a generated opacity-only `Mask()` in the visibility pixel shader.
- **Android:** the legacy path only. This is a limit, not a plan.
- **Bindless cap:** 500 textures (`RHITypes.h:10`). A real ceiling for textured graphs; lifting it is Phase 3 work.

## Switch

- `openpbr` scene setting, default false. Off: output is byte-identical to the pre-change binary, with no new targets or passes.
- The setting goes through Lua, `.pescene` and the Pipeline panel.
- It joins the GI history hash.
- A toggle rebuilds pipelines via `m_colorFormatChanged`. This includes a toggle that lands with a render-scale resize: `SceneRendererCore` tracks the effective state like `m_hdr`, as reviewed in revision 1.

## Verification

Material-ABI values: Phase 0 checks the generated loader line by line against its layout, with negative cases. Loaded values are checked at runtime by Phase 2's match test with non-default inputs, once a runtime material path exists. This split was accepted in the Codex review of 2026-10-13.

1. **Off-identity:** Sponza and the mix scene, byte-for-byte against a capture from the pre-change binary.
2. **Phase 1:** the visibility buffer's reconstructed depth matches the depth buffer, and a compute re-sample of the base-colour texture through the quad-exact path equals the raster G-buffer albedo within 1/255. Checked on Sponza with LOD on, clusters on/off, two-phase Hi-Z on, on both APIs.
3. **MaterialX match, direct light only** (Phase 2): a sphere with OpenPBR materials (default, coat, fuzz, metal with F82 tint, thin film, anisotropy, subsurface) under one directional light, IBL off, no shadows. Compared against the MaterialX 1.39.5 Python `GlslRenderer` (FLOAT target) at the same camera: ≤ 2/255 encoded, linear comparison. Installing the `MaterialX==1.39.5` wheel and fetching test meshes and environment maps from the git tag need the user's permission at test time.
4. **MaterialX match, environment (amended 2026-10-15):** the same HDR in PE's skybox and in `GlslRenderer`'s FIS, compared at equal sample counts (PE's `openpbr_env_samples` against the reference's), on the sphere and on the plane (one exact tangent, so both integrators draw the same samples); a sunny HDR makes any count far from converged, so equal-count is the implementation check and convergence is reported separately. Tolerances and numbers: `openpbr_parity.py` `ENV_TOLERANCE` and the wiki.
5. Clean logs, no validation errors, and the pool-test conventions from revision 1.

## Performance

- **Off:** within the project thresholds on Sponza.
- **On:** measured and reported.
  - The visibility path is expected to **lose** on large-triangle scenes (Hable's low-density case, 3.01 ms vs 2.57 ms deferred) and win on dense or many-material scenes.
  - Two graph evaluations per pixel, plus edge-quad overshading, is the price of exact MaterialX shading.

Measured 2026-10-14 on an RTX 4080 SUPER, Vulkan Release, immediate presentation, display 1 (2560x1369), render 1024x547: ten snapshots one second apart per row, with debug view off. Costs use all samples; enabled cases are reported, not gated.

| Capture / scene | FPS | GPU total (ms) | App VRAM (MB) |
|---|---:|---:|---:|
| Task 0 baseline / Sponza | 432.27 | 0.381 | 885 |
| Task 7 / Sponza off | 1901.65 | 0.282 | 885 |
| Task 7 / Sponza on | 1702.35 | 0.337 | 895 |
| Task 13 / Sponza off | 1816.94 | 0.417 | 885 |
| Task 13 / Sponza on | 1555.18 | 0.352 | 895 |
| Task 13 / 100 MaterialX planes on | 2042.28 | 0.339 | 350 |

Both Sponza off controls pass `tools/compare_snapshots.py` against the fixed Task 0 baseline. Task 7 on adds 0.010 ms to the prepass, 0.055 ms total GPU time and 10 MB app VRAM against its current off control. These deferred measurements use the completed Phase 2 binary and include empty material dispatches on Sponza (thin/shade 0.018/0.026 ms); Task 13 reproduces those per-pass and VRAM costs. On FPS is 10.5%/14.4% lower than the paired current off controls. Whole-frame totals vary between sessions, including larger Forward+/GUI samples in Task 13 off; the baseline CPU/FPS gap and the lower Task 13 on GPU total are not causal speedup claims.

The 100-plane workload uses separate 1 m default OpenPBR tiles in a 10x10 grid, 1.1 m spacing, one directional light and camera (0,9,12) looking at the origin. Its thin/shade costs are 0.019/0.052 ms. Process lists before/after each window and native polling detected no Godot/WoW overlap. One preliminary overlapping-plane window was rejected and repeated; all valid windows were retained without trimming. The fixed baseline and Sponza fixture remained unchanged, and editor settings were restored byte-for-byte. Full timings and raw-capture locations are in [OpenPBR Materials](../../wiki/architecture/rendering.md#openpbr-materials).

## Phases (each gets its own plan when it starts)

0. **Gates (no user-visible change).**
   - MaterialX dependency and `PhasmaCook --mtlx` code generation, compiled through PE's Slang path on both APIs.
   - The PE surface node and search-path overrides compile.
   - Compute derivatives in quad mode and fragment barycentrics enabled on both APIs.
1. **Visibility buffer** in the prepass, terrain/voxel clear, and a quad-exact compute debug view that re-samples base colour (bindless in compute). No shading change.
2. **Material passes 1 and 2** for rigid opaque meshes with the default, untextured OpenPBR material, plus PE lights and environment hooks. Direct-light match test against MaterialX.
3. **Textures, `.mtlx` import, glTF → MaterialX conversion**, persistence, Lua/editor (all 41 inputs), per-graph pipelines, binning at scale, the bindless cap.
4. **Skinned/sprite pre-skinning and alpha-cut** `Mask()`.
5. **RT per-material hit groups** (reflections and GI hits run the graph).
6. **Transparent/transmission** forward pass with the generated code.
7. **Performance:** empty-bin compaction, software VRS, skipping legacy work for visibility pixels; Android stays legacy.

## Carried over from revision 1

- The setting plumbing and caps-gate pattern.
- The `m_openpbr` rebuild flag and the `HasBinding`-guarded descriptor writes.
- Persistence helper shape (`ForEach…` list, JSON object, written only when non-default).
- Lua key naming (snake_case OpenPBR input names).
- The glTF mapping table above, now MaterialX inputs, with linear-to-sRGB handling for imported picker colours.
- Pool-test conventions: off-identity baseline first, `.testpool` only, Vulkan + DX12, log check.
- The legacy issue table below.

## Legacy issues found during the survey (not fixed by this work)

| Issue | Evidence | Policy |
|---|---|---|
| `V_SmithGGXCorrelated` uses a2 = r² (α) while `DGGX` uses α² = r⁴ | [PBR.hlsl:26-32](../../../Phasma/Runtime/RuntimeAssets/Shaders/Gbuffer/PBR.hlsl), read 2026-10-13 | Left in the legacy path; offered as a separate fix |
| `PBR.hlsl` fallback `#define PI 3.1415628` | same file; masked by `Common.hlsl` | Left; offered |
| Transparent G-buffer pass keeps the opaque surface's srm under alpha-blend pixels | blend state `Default` × `srm.a` = 0 | Left; offered |
| Raster point/spot falloff (1−(d/r)²)² vs ray tracing (1−d/r)² | Lighting.hlsl vs RayTrace.hlsl | Left; offered |
| Lua `night_emissive` updates neither `params` nor dirty flags | MaterialBindings.cpp (code read) | Left; offered |
| Two meshes sharing a cached source (two primitive cubes) merge their instance overrides on reload | SceneSerializer.cpp:3080-3088, :3172-3177; Scene.cpp:705-711 (code read; Codex agreed) | Left; offered |

## References

- MaterialX v1.39.5: https://github.com/AcademySoftwareFoundation/MaterialX/tree/v1.39.5. Key files:
  - `libraries/bxdf/open_pbr_surface.mtlx`
  - `source/MaterialXGenSlang/SlangShaderGenerator.cpp`
  - `source/MaterialXGenHw/Nodes/HwSurfaceNode.h` (virtual `emitLightLoop`)
  - `source/MaterialXGenShader/GenContext.h` (`resolveSourceFile`)
- OpenPBR v1.1.1: https://academysoftwarefoundation.github.io/OpenPBR/
- Visibility buffer:
  - Burns & Hunt 2013 (JCGT): https://jcgt.org/published/0002/02/04/
  - Schied & Dachsbacher 2015 (DAIS): https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf
  - Hable 2021: http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
  - Karis et al. 2021 (Nanite): https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
  - Wihlidal 2024 (Nanite GPU-driven materials): https://media.gdcvault.com/gdc2024/Slides/GDC+slide+presentations/Nanite+GPU+Driven+Materials.pdf
  - McLaren 2022 (Horizon Forbidden West): https://guerrilla-games.com/read/adventures-with-deferred-texturing-in-horizon-forbidden-west
- Mobile guidance:
  - Arm 2026: https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-and-unreal-engine-s-nanite-enabling-the-future-of-mobile-graphics
  - DevSH, Vulkanised 2026: https://vulkan.org/user/pages/09.events/vulkanised-2026/1415-Lucas-Silva-DevshGraphicsProgramming.pptx.pdf
- Extensions:
  - `VK_KHR_compute_shader_derivatives`: https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_compute_shader_derivatives.html
  - `VK_KHR_fragment_shader_barycentric`: https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_fragment_shader_barycentric.html
