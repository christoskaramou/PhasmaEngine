# Noise-free probe radiance cache (Lumen roadmap, step 2)

Date: 2026-10-10. Status: design approved in chat ("approved"); follows Lumen's world-space radiance cache.

## Problem

Probe GI ([GlobalIlluminationPass.cpp](../../../Phasma/Runtime/Code/RenderPasses/GlobalIlluminationPass.cpp)) traces 64
uniformly spread, per-frame-rotated rays per probe and averages irradiance over time. Measured 2026-10-10 (wiki,
GI section): steady at `gi_hysteresis` 0.999 the image is stable, but at 0.85 about 53% of pixels swing more than
4/255, and for about 3 s after any light or geometry change 18-23% do, because a few rays hitting a small bright
emitter or sunlit patch make each frame's estimate jump, and a restart drops every probe to a 49-frame average.

## Goal

No visible GI flicker at any `gi_hysteresis` and after changes, with unchanged converged brightness and a small,
measured cost. The probe cache also keeps directional radiance, which Lumen's screen probes (step 3) sample.

## Design (Lumen's world radiance cache, adapted)

1. **Radiance per probe.** Each probe keeps a 16x16 octahedral radiance map (`GI_Radiance` + history, RGBA32F,
   rgb = radiance, a = sample count). The irradiance atlas used for shading stays as it is.
2. **Importance-sampled rays.** A new compute pass (`ProbeSample.hlsl`, one workgroup per probe) builds a CDF over
   the probe's 256 radiance texels from last frame's radiance: texel weight = 0.75 x (luminance x solid angle,
   normalised) + 0.25 x (solid angle / 4 pi), so every direction keeps a non-zero probability. Rays are stratified
   over the CDF (ray i at (i + per-probe random offset) / N), jittered inside the chosen texel, and written with
   their pdf to `GI_RayDirs` (RGBA32F: direction, pdf). The pdf over the sphere is the texel probability x 64 x
   |p|^3, the octahedral map's Jacobian (p = the unnormalised octahedron point). Without history (first frame,
   reset) the radiance weight is zero and sampling is uniform over the sphere.
3. **More rays.** 64 -> 256 rays per probe (`GI_Rays` 256 x 512). The probe ray-generation shader reads its
   direction from `GI_RayDirs` instead of the rotating Fibonacci set.
4. **Unbiased irradiance.** `ProbeUpdate.hlsl` uses the self-normalised importance-sampling estimator
   E(n) = pi x sum(L cos / pdf) / sum(cos / pdf); with uniform rays it equals today's estimator. Distance moments
   and the backface fraction are weighted by 1 / pdf the same way.
5. **Variance-aware history.** Per irradiance texel the history keeps the mean (irradiance atlas), the sample count
   (distance.z) and the variance of a per-frame change statistic (distance.w; negative when the last frame was outside
   the noise): the largest per-channel difference to the mean, so colour-only changes count. Two frames in a row
   beyond 4 sigma + 2%, or a visibility change (active flag flipped, mean hit distance beyond its spread), are a real
   change: that texel's average is held at n = 1 / (1 - gi_hysteresis), clamped to [4, 49], while the change
   continues; frames outside the noise are left out of its estimate. Otherwise the count grows to 1000, whatever
   `gi_hysteresis` is, so `gi_hysteresis` sets how fast real changes are followed, not the noise level. A known change
   to anything the probe rays see (lights, ray-traced transforms, geometry and materials, the settings that shade ray
   hits; reset bit 1) caps every texel at 200 frames instead of dropping all probes to 49, so changes too small to
   stand out of the noise still settle in a few seconds. History discard on volume moves and scene switches (reset bit
   0) and the scroll remap stay. (Review 2026-10-10: the first version folded a change into its own noise estimate,
   used luminance only and had no global signal; fixed as above.)
6. **Radiance update.** A second entry point in `ProbeUpdate.hlsl` (one workgroup per probe, a thread per radiance
   texel) averages the rays that landed in each texel this frame and blends them into the history (window up to 32
   frames; texels without rays keep history). Radiance history scrolls with the volume like the irradiance.

Barriers stay just-in-time inside `GlobalIlluminationPass::ExecutePass`: ray directions are barriered compute ->
ray tracing right before the trace, rays ray tracing -> compute right before the update (as today), radiance
compute -> transfer right before its history copy.

## Not in scope

Screen probes (step 3), GI reflections (step 4), software tracing, multiple clipmap volumes, sampling emissive
triangles as lights.

## Verification

Pool tests on Vulkan and DX12:
1. **Noise** (new `gi-noise`, PrintWindow grabs of the editor viewport as in the 2026-10-10 measurement, Cornell box
   and small-emitter scenes): with GI on, pixels leaving their local trend (median of 5 frames, 0.1 s apart) by more
   than 4/255 exceed the GI-off figure by at most 0.2 percentage points at `gi_hysteresis` 0.999 and 0.85, and in the
   3 s starting 0.5 s after a light change. (Plain max-min was the first metric; it also counts the smooth fade a
   large light change legitimately causes, so the test prints it for information only.)
2. **Brightness** (same test): converged mean colour of fixed regions (back wall, floor near the emitter, the
   blocks) matches a baseline captured from the pre-change binary within 3% (the estimator is unbiased; only noise
   differs).
3. **Regression:** `gi-range`, `gi-flicker`, `gi-fitted-volume`, `gi-scroll`, `gi-player`, `render-warnings`,
   `linear-color` pass; no `[WARN]`/`[ERROR]` or validation lines.

Performance: Sponza Release, GI on, 10 snapshots in both run orders: GPU frame at most +0.5 ms (RTX 4080 SUPER).
