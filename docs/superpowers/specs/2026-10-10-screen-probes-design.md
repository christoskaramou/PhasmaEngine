# Screen probes (Lumen roadmap, step 3)

Date: 2026-10-10. Status: design approved in chat; built 2026-10-10 with the changes marked "As built" (wiki,
Rendering > Screen Probes, has the measurements).

## Problem

Diffuse GI ([GlobalIlluminationPass.cpp](../../../Phasma/Runtime/Code/RenderPasses/GlobalIlluminationPass.cpp)) is
read per pixel from an 8x8x8 world probe grid (`ProbeLighting`, spacing about 1 m in the Cornell box). Probes that
far apart miss contact bounce (a red wall colouring a box standing next to it, light pooled in corners) and cannot
resolve detail smaller than their spacing.

## Goal

Per-pixel diffuse GI with contact detail, without light leaking through thin walls and with no visible noise or
flicker, still or with the camera moving, at most +1.5 ms GPU at 1440p (RTX 4080 SUPER).

## Design (Lumen's screen probe gather, adapted)

New render graph pass `ScreenProbes` (`ScreenProbePass`, derived from `RayTracingPass`, order 470: after
`GlobalIllumination` 460, before `LightOpaque` 500). Enabled with GI on hardware ray tracing; no new setting.

1. **Placement** (`ScreenProbePlace.hlsl`, a 64-thread group per 16x16 tile). Each frame the probe takes a jittered
   pixel of its tile; when that pixel is sky the nearest of 64 candidates on a 2-pixel lattice with geometry is used,
   otherwise the tile has no probe. Writes world position + view distance and the G-buffer normal.
   Added 2026-10-10, Lumen's adaptive placement (`ScreenProbeAdaptive.hlsl`, a group per 8x8 sub-tile): where the
   four uniform probes' bilinear x plane x normal weight sums under 0.25 at the sub-tile's jittered pixel (the point
   where the resolve starts handing the pixel to world probes), the sub-tile takes a probe of its own. Up to half as
   many adaptive probes as uniform ones (Lumen's default fraction) sit in extra rows of every probe image (slot k at
   `(k % tilesX, tilesY + k / tilesX)`), so tracing, integration and history need no new paths; a per-sub-tile map
   (slot + 1) lets the resolve and the rough reflection path blend the four nearest sub-tiles' adaptive probes
   (bilinear on the 8 px grid) with the uniform ones. Their 64 rays are cosine-distributed; the spatial filter leaves
   them as traced. One level only (Lumen also refines at 4 px).
2. **Ray guiding** (same pass, a thread per direction bin). Each probe has 8x8 world-space octahedral direction
   bins. Bin probability = 0.25 x its share of the cosine-weighted hemisphere + 0.75 x its share of
   cos x last frame's radiance in that bin, read from the previous frame's probe at the reprojected position (same
   surface: normals within 25 degrees, plane distance under 5% of view distance). 64 rays are stratified over that
   distribution, jittered inside the bin; pdf through the octahedral Jacobian (as `ProbeSample.hlsl`).
3. **Tracing** (`RayTrace.hlsl`, `SCREEN_PROBES` raygen, 8x8 rays per probe in a `tilesX*8 x tilesY*8` target).
   Rays start at the probe (normal offset) and run 2x the world probe spacing. Hits are shaded as world probe hits
   (direct light, world probe irradiance for further bounces). Back-face hits return black (a ray that slipped into a
   closed interior), so light does not leak. A ray that runs out of length takes its light from step 2's world
   radiance cache at its end point (trilinear over the 8 world probes with their visibility weights), so distant
   light is the cache's averaged value, not noise; outside the world volume it takes the sky.
   As built: the end-point lookup was blotchy and an origin lookup leaked through thin walls, so, as in Lumen, the
   world probe rays continue past their interpolation footprint (1.75 spacings) into a far-field radiance cache,
   looked up at the screen probe with `ProbeLighting`'s weights; screen rays run 3.5 spacings so no surface falls
   between their end and the far-field start.
4. **Integration** (`ScreenProbeIntegrate.hlsl`, a group per probe). L1 spherical harmonics per colour from the 64
   rays, each weighted by 1/pdf; per-bin mean radiance for next frame's guiding (bins without rays take the SH value).
   As built: 1/pdf weights made fireflies at grazing angles, so the SH comes from the bins (mean radiance x solid
   angle; a bin without rays keeps last frame's value, or the mean of the filled bins).
   Changed 2026-10-11: L2 SH (9 coefficients, as Lumen) instead of L1, which biased grazing light (wiki pitfall).
5. **Spatial filter** (`ScreenProbeFilter.hlsl`). Each probe's SH averaged with its 3x3 neighbours, weighted by
   normal agreement, plane distance and depth difference.
6. **Resolve + temporal** (`ScreenProbeResolve.hlsl`, full resolution). Each pixel blends the 4 nearest probes
   (bilinear on the tile grid x plane-distance weight x normal weight), evaluating each probe's SH irradiance for the
   pixel's normal. Pixels with no usable probe take world probe lighting. The result is reprojected with the camera
   (previous view-projection) and blended with history: bilinear history taps are kept when their stored normal and
   distance match; the average grows to 32 frames.
7. **Lighting.** `LightingPS.hlsl` (opaque pass) reads the resolved irradiance instead of `ProbeLighting`; the
   transparent pass keeps world probes. Specular is unchanged (step 4).

Barriers are just-in-time inside `ScreenProbePass::ExecutePass`, before each dispatch or trace that needs the data.
History (probe positions, normals, bin radiance, resolved irradiance, pixel geometry) is copied at the end of the pass.

## Not in scope

Screen-space tracing. (Object motion through the velocity target, adaptive probes and GI reflections were added later
on 2026-10-10.)

## Verification

Pool tests on Vulkan and DX12 (PrintWindow grabs of the editor viewport):
1. **Contact bounce** (Cornell box): the white block faces next to the red and green walls pick up their colour more
   than with world probes alone (screen probes off vs on, same frame). As built: world probes turned out to over-bleed
   the green wall onto the block, so the check is accuracy instead: against a reference with 50 m screen rays the
   image is closer than world probes alone (pool test `gi-screen-probes`).
2. **Leak** (thin wall test scene: a bright light on one side of a 5 cm wall, a closed dark room on the other): the
   dark side's wall face stays within 2/255 of GI off.
3. **Flicker** (`gi-noise` metric, pixels leaving a 5-frame median trend by more than 4/255): GI on within 0.2
   points of GI off, still and while the camera orbits slowly.
4. **Regression:** gi-noise, gi-range, gi-flicker, gi-fitted-volume, gi-scroll, gi-player, render-warnings,
   linear-color pass; clean logs.

Performance: Sponza Release, GI on, 10 snapshots, 1440p viewport: `ScreenProbes` at most +1.5 ms GPU.
