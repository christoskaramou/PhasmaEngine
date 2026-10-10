# Probe Radiance Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Noise-free probe GI: importance-sampled rays from a per-probe radiance cache, 256 rays, unbiased
estimator, variance-aware history.

**Architecture:** `GlobalIlluminationPass` gains a sample dispatch (before the trace) and a radiance dispatch (after
the irradiance update); the probe raygen reads per-ray directions and pdfs; `ProbeUpdate.hlsl` weights by 1/pdf and
keeps per-texel variance.

**Tech Stack:** C++20, HLSL (DXC to SPIR-V/DXIL), Vulkan/DX12 ray tracing, Python pool tests.

**Spec:** `docs/superpowers/specs/2026-10-10-gi-radiance-cache-design.md`

## Global Constraints

- Converged brightness unchanged (within 3% of the pre-change binary per region); estimator unbiased.
- Barriers just-in-time, right before the dispatch or trace that needs the resource.
- Never commit without the user's word; clang-format touched C++; no std headers added (PCH).
- Build: `.testpool\tests\vsenv.cmd` then `cmake --build build-ninja-physics --config Release`.
- Every task ends with its pool tests on Vulkan and DX12 and a clean editor log.

## Review Focus

1. Probe volume scroll (camera-following volume): radiance history must shift with the probes (gi-scroll covers
   irradiance only) — pinned in Task 2 by extending the scroll check to the radiance atlas.
2. Reset frames (first frame, scene switch, spacing change): sampling falls back to uniform, no NaN from empty
   history — pinned in Task 1/3 by a toggle off/on check in gi-noise.
3. A probe inside geometry (most rays backface): weighting the backface fraction by 1/pdf must keep the active flag
   stable — covered by gi-range/gi-flicker on walls.
4. Real light change at default 0.999: response no slower than today's 49-frame restart — pinned in Task 4 by a
   light-off convergence time check.
5. DX12 binding reflection for the new GI_PROBES binding (set 2, binding 4) — pinned by every test on DX12.

---

### Task 0: Baseline and noise test (before code changes)

**Files:** Create `.testpool/tests/gi_noise.py` (adapted from the 2026-10-10 `gi_tremble.py` measurement: PrintWindow
on the editor PID, Cornell and emitter scenes, GI off / on 0.999 / on 0.85 / first 3 s after a light change) with a
`--baseline` mode that saves converged region means to `.testpool/baselines/gi_noise/<api>.json`.

- [ ] Capture the brightness baseline on both APIs with the current binary.
- [ ] Run the noise checks: expected FAIL at 0.85 and in the transient (today's numbers: ~53% and ~20%).

### Task 1: 256 rays with directions and pdfs from a sample pass (uniform)

**Files:** `GlobalIlluminationPass.h/.cpp` (GI_Rays 256x512, GI_RayDirs RGBA32F 256x512, rayCount 256, sample
PassInfo + dispatch, barrier rayDirs compute -> ray tracing before the trace), new
`Shaders/GlobalIllumination/ProbeSample.hlsl` (uniform stratified directions, pdf 1/(4 pi)), `ProbeVolume.hlsl`
(GI_PROBES binding 4: ray directions), `RayTracing/RayTrace.hlsl` (raygen reads the direction), `ProbeUpdate.hlsl`
(reads direction and pdf; self-normalised IS weights; backface fraction weighted).

- [ ] Build; gi-range, gi-flicker, gi-noise brightness on both APIs pass.

### Task 2: Radiance cache

**Files:** `GlobalIlluminationPass.h/.cpp` (GI_Radiance + history 128x1024 RGBA32F, radiance PassInfo, dispatch after
the irradiance update, copy to history), `ProbeUpdate.hlsl` (entry `radiance`: per texel, average this frame's rays
in that texel; blend; scroll remap).

- [ ] Build; capture the radiance atlas once to check it shows the scene (red/green walls, lamp); gi-scroll passes.

### Task 3: Importance sampling

**Files:** `ProbeSample.hlsl` (CDF over radiance texels with the 0.25 uniform mixture; stratified; jitter; pdf with
the octahedral Jacobian).

- [ ] Build; gi-noise brightness within 3%; probe flicker (gi-flicker range) improves.

### Task 4: Variance-aware history

**Files:** `ProbeUpdate.hlsl` (mean / count / variance per texel, 4 sigma + 2% change test, restart at
1/(1-gi_hysteresis) clamped [4, 49], else grow to 1000), `GlobalIlluminationPass.cpp` (restart bit no longer drops
every probe; reset bit unchanged).

- [ ] Build; gi-noise full thresholds pass on both APIs; light-off response within 49 frames.

### Task 5: Register, measure, document

- [ ] Register `gi-noise-vulkan`/`-dx12`; run the GI and rendering pool set.
- [ ] Perf: Sponza GI on, before (pre-change baseline numbers) vs after, both orders: <= +0.5 ms GPU.
- [ ] Wiki: GI section rewritten for the radiance cache (sources, measurements, date); lint.
