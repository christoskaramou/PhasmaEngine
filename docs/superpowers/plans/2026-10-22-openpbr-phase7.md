# OpenPBR Phase 7 — Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Status: approved 2026-10-21 and implemented the same day (uncommitted; ledger in `.superpowers/sdd/2026-10-22-openpbr-phase7/progress.md`). The user chose Decision 1 = A and Native execution. Starting point: `6d998073` (Phase 6) on `master`. No commit is authorized.

**Goal:** Cut material pass 2's cost on MaterialX-heavy frames without changing what it renders, by running MaterialX's BSDF once per pixel instead of once per quad lane.

**Architecture:**
- Today each output pixel of pass 2 is shaded by a full 2x2 quad on its own triangle (exact derivatives), and all four lanes run the whole BSDF; only one writes.
- Phase 7 splits pass 2 into two stages inside one group per 8x8 tile:
  - **Stage 1 (quad-mapped):** it evaluates the graph's inputs with real derivatives, plus curvature, and stores them in groupshared memory.
  - **Stage 2 (pixel-mapped):** after a barrier, it runs `pe_shade_inputs` with one thread per pixel, so every lane does useful BSDF work.
- The BSDF's one derivative (subsurface curvature) moves into stage 1 behind a generator seam, which bumps the module ABI to 4.

**Tech Stack:** Slang compute (MaterialShadeMain, MaterialClassify), the MaterialX generator (`MaterialXGen.cpp`), C++ pass code (`LightPass.cpp`, `MaterialThinPass.cpp`), and the `.testpool` harness.

**Spec:** [2026-10-13-openpbr-design.md](../specs/2026-10-13-openpbr-design.md), Phases item 7: "Performance: empty-bin compaction, software VRS, skipping legacy work for visibility pixels; Android stays legacy."

## Measurements this plan is built on (2026-10-21, RTX 4080 SUPER, Release, Vulkan, 2560x1369, Immediate, 10 snapshots)

Saved Sponza with every one of its 103 mesh slots on the default MaterialX module (untextured), `openpbr` on, GI off. Scripts and snapshots are in `.superpowers/sdd/2026-10-22-openpbr-phase7/` (`measure*.py`, `m1`-`m3`).

| Pass | `openpbr` off | All-MaterialX |
|---|---|---|
| GPU total | 0.67 ms | 4.64-4.66 ms |
| MaterialShade (pass 2) | - | 2.95-3.42 ms |
| MaterialThin (pass 1) | - | 0.53-0.77 ms (run to run) |
| Classify | - | 0.06 ms |
| GbufferOpaque | 0.20 ms | 0.26 ms |
| LightOpaque | 0.16 ms | 0.09 ms |

- **The BSDF is pass 2's cost.** A sandbox-only stub ran the full BSDF for pixel (0,0) alone and gave every other pixel its evaluated base colour. That dropped pass 2 to 0.96 ms and the frame to 2.65 ms: the BSDF is about 2.0-2.5 ms, or 68-72% of pass 2. Quad-redundant shading runs it four times per pixel.
- **The fixture is an upper bound.** The default module has constant inputs. Textured graphs keep their four-lane texture sampling in stage 1, so they save less.
- **Environment sampling played no part in these numbers.** The env-32, env-8, env-1 and IBL-off captures were byte-identical; Sponza is an interior with little environment light.
- **Empty bins are unproven.** Fifteen extra modules on spheres behind the camera added +0.20 ms to pass 1, but pass 1 alone varied by 0.24 ms between identical runs.
- **Legacy work on MaterialX pixels is at most about 0.2 ms.** That is GbufferOpaque's material work plus LightOpaque.

## Decision 1 — what Phase 7 does (user chose A, 2026-10-21)

- **A (recommended): shade once per pixel**, the two-stage pass 2 above.
  - The output is the same as today's, within float reordering.
  - Expected saving: up to about 1.5-1.9 ms of the 4.66 ms all-MaterialX frame, less the occupancy cost of about 17.4 KB of groupshared per group.
  - **User-visible consequence:** module ABI 4. Every cooked module regenerates on its next editor load, and exported games must be re-exported. Until then their Players reject the old modules with the existing message and render legacy materials, as after Phase 5.
- **B: the spec's software VRS.** Shade some tiles at a coarser rate, which is lossy, and the parity tests would need a tolerance. Not recommended before A, which recovers the same redundancy without loss.
- **Small items, recommended out of Phase 7 with their numbers recorded:**
  - Skipping legacy work on MaterialX pixels saves at most about 0.2 ms. It needs a guard so that a frame where pass 1 or 2 skips does not show garbage.
  - Empty-bin compaction is not measurable above run-to-run noise; compacting from a frame-late readback risks a one-frame pop.

The spec names VRS, so replacing it with A is a deviation, and it is your call. The tasks below are written for A.

## Global Constraints

- **Off path unchanged.** `openpbr` off renders byte-identically to the pre-change binary, with no new targets or passes. Pass 2 runs only with OpenPBR.
- **Generated code only.** No hand-written BSDF. The seam returns the curvature stage 1 computed with the same `fwidth(N)/fwidth(P)` expression on the same quad.
- **Android:** the legacy path only.
- **Tests: crucial only** (the user's directive): one run per API per gate, full resolution where the harness allows, no matrices. Perf follows CLAUDE.md: Release, Immediate, 10 snapshots 1 s apart, on a quiet machine, as an A/B against today's tree.
- **Sandbox.** Build and test only in `C:\Users\Christos\pe-p4-sandbox`, under the GPU lock, on display 1. Sync only Phase 7 files. The sandbox shares `.fetchcontent-ninja` with the main tree, so retry once on `LNK1104`.
- **Other sessions' files.** Phase 7 does not edit other sessions' unstaged files (`RayTracingPass.*`, `SceneRendererCore.*`, `Shader.cpp`, `ShaderCache.*`, `RenderPassShaderReload.cpp`). In `rendering.md` it edits only its own section.
- **Hygiene.** No test-only engine code, no `third_party/`, `clang-format -i` on touched C++, and the CLAUDE.md layout rules in shaders too.
- **Commits.** None unless the user says so.

## Review Focus

1. **Tiles with several modules or legacy pixels.** Stage 2 lanes whose pixel is not this module's, or not MaterialX at all, must write nothing. Expected: identical images on mixed scenes. Task 3 Step 1 uses the parity scenes, which mix MaterialX and legacy pixels.
2. **Subsurface curvature.** Stage 1's curvature must equal today's `fwidth` value on that quad. Expected: the parity `subsurface` case within its current tolerance. Task 3 Step 1.
3. **Groupshared versus occupancy.** About 17.4 KB per 64-thread group can lower occupancy enough to eat the gain. Expected: the Task 2 gate measures it, and the plan stops if pass 2 is not at least 30% faster.
4. **The DX12 compute-derivative and groupshared path.** DXIL quad derivatives in stage 1 must match Vulkan. Expected: parity on DX12, once. Task 3 Step 1.
5. **Exported games with ABI-3 modules.** They must get the existing re-export message and legacy fallback, with no crash. Task 1 Step 3.

---

### Task 0: References

- [ ] **Step 1: Capture the A side.** Run `perf_ab.sh`'s A build on today's tree (Phase 7 reverted) for the all-MaterialX Sponza fixture (`measure.py`'s `mtlx` state) and for saved Sponza with `openpbr` off. Keep the build as `build-a` until Task 4.
- [ ] **Step 2: Expected:** two A captures, with each run's log check passing.

### Task 1: Curvature seam (module ABI 4)

**Files:**
- Modify: `Phasma/Cook/Code/MaterialX/MaterialXGen.cpp` (`kDerivativeSeams`)
- Modify: `Phasma/Runtime/Code/Scene/OpenPBRMaterial.h` (`kOpenPBRModuleAbi = 4`)
- Test: `.testpool/tests/mtlx_codegen.py`, `openpbr_hit_abi.py`, `openpbr_export_player.py` (ABI pins 3 -> 4)

**Interfaces:**
- Produces: under `PE_SHADE_ONCE`, `pe_curvature(N, P)` returns `static float pe_shade_curvature`, which the wrapper sets. The derivative macros stay native, since stage 1 needs them. `PE_MATERIAL_HIT` keeps its branch.

- [ ] **Step 1: Write the failing test.** `mtlx_codegen.py` pins ABI 4 and checks that the generated seam block contains `pe_shade_curvature` under `PE_SHADE_ONCE`.
  - Expected: FAIL (ABI 3, no seam).
- [ ] **Step 2: Implement the seam and bump the ABI.**
  - Expected: `mtlx_codegen.py` and `mtlx_hit_codegen.py` PASS (CPU only).
- [ ] **Step 3: Check the ABI-3 fallback.** A cooked ABI-3 module loads with the existing re-export message and the legacy fallback; that is `openpbr_hit_abi.py` with its pin moved to 4.
  - Expected: PASS on Vulkan.

### Task 2: Two-stage pass 2

**Files:**
- Modify: `Phasma/Runtime/RuntimeAssets/Shaders/MaterialX/MaterialShadeMain.slang`, `MaterialShadeHead.slang` (`PE_SHADE_ONCE`), `MaterialClassify.slang` (a second args `uint4` per bin: one group per tile), `VisibilityCommon.slang` (`PeBinTile` for one group per tile)
- Modify: `Phasma/Runtime/Code/RenderPasses/MaterialThinPass.cpp/.h` (bin layout offsets: thin args, shade args, tiles), `LightPass.cpp` (`MaterialShadePass::ExecutePass` dispatches the shade args)

**Interfaces:**
- Consumes: `pe_shade_curvature` (Task 1).
- Produces:
  - Bin layout `uint4 thinArgs, uint4 shadeArgs, tiles...`, with `BinThinArgsOffset(slot)`, `BinShadeArgsOffset(slot)` and `BinTilesOffset(slot)`.
  - `[numthreads(8, 8, 1)] [DerivativeGroupQuad] mainCS` in which one group covers one 8x8-pixel tile:
    - Stage 1 loops over the tile's four 4x4-pixel quadrants. Each iteration maps 64 lanes to 16 pixels times 4 lanes, as today's `PeSurfaceAt` and `pe_material_inputs` path does.
    - Each writer lane stores `PeSurfaceInputs` and the curvature (`length(fwidth(sp.N)) / length(fwidth(sp.P))`) at `gs_inputs[pixelInTile]` and `gs_curvature[...]`, plus a valid flag.
    - Stage 2 runs after `GroupMemoryBarrierWithGroupSync`. Thread i takes pixel i, rebuilds its surface point with `PeLoadTriangle` and `PeSurfaceAt` (analytic, so no derivatives), sets the `pe_*` statics and `pe_shade_curvature`, calls `pe_fill_vertex_data` and `pe_shade_inputs`, then applies fog and encoding and writes.

- [ ] **Step 1: Prototype gate (sandbox, not committed).** Implement the shader and bin changes, build, and capture the all-MaterialX Sponza once.
  - Expected: pass 2 is at least 30% below its Task 0 value. If not, try 32-pixel stage-2 batches (two per tile, half the groupshared) once. If that also misses, stop: record the numbers, revert, and report.
- [ ] **Step 2: Check the image.** Compare the all-MaterialX Sponza capture against Task 0's, byte for byte.
  - Expected: identical, or within 1/255 where float reordering differs.

### Task 3: Verification (crucial only)

- [ ] **Step 1: Parity.** Run `openpbr-parity` (direct light plus the environment pool flags) once on Vulkan and once on DX12.
  - Expected: within its existing tolerances, including `subsurface`. The parity scenes mix MaterialX and legacy pixels.
- [ ] **Step 2: Forward and hits.** Run `mtlx_hit_codegen.py` (CPU) only, since neither path defines `PE_SHADE_ONCE`.
  - Expected: PASS.
- [ ] **Step 3: Logs.** No `[ERROR]` lines and no validation messages in the gate runs.

### Task 4: Performance

- [ ] **Step 1: B side.** Run the B side of the A/B (A, B, A, B) on a quiet machine: the all-MaterialX Sponza fixture and saved Sponza with `openpbr` off.
  - Expected: pass 2 and the frame faster on the fixture, and the off control within the CLAUDE.md thresholds.

### Task 5: Documentation

- [ ] **Step 1: Docs.**
  - Wiki `rendering.md`: a "Phase 7 performance" section with the measurements, the design, the ABI 4 consequence, and the dropped items with their numbers.
  - Spec: an amendment under Phases.
  - Then `bash docs/wiki/tools/lint.sh`.
  - Expected: lint clean.

## Out of scope (stated)

- Software VRS (Decision 1 B).
- Skipping legacy work on MaterialX pixels (at most about 0.2 ms).
- Empty-bin compaction (not measurable above noise).
- Pass 1 (thin) evaluates only inputs, which need the quad, so the restructure doesn't apply to it.
