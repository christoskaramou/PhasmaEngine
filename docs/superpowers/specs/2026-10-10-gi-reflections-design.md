# GI-lit reflections (Lumen roadmap, step 4)

Date: 2026-10-10. Status: roadmap step approved in chat ("GI-lit reflections"); follows Lumen Reflections (UE5 Lumen
Technical Details, SIGGRAPH 2022 "Lumen: Real-time Global Illumination in Unreal Engine 5"). Built 2026-10-10 with the
changes marked "As built" (wiki, Rendering > Reflections, has the measurements).

## Problem

Opaque specular comes only from the prefiltered sky cubemap (`ComputeIBL_Common`), unoccluded: indoors every glossy
surface reflects the sky, and with IBL off there is no indirect specular at all (a polished floor in the Cornell box
is black where it should mirror the walls). SSR adds on-screen reflections on top but misses everything off screen.

## Design (Lumen Reflections with hardware ray tracing, adapted)

New render graph pass `Reflections` (`ReflectionPass`, derived from `RayTracingPass`, order 480: after
`ScreenProbes`, before `LightOpaque`). It runs whenever screen probes do (GI on, hardware ray tracing, raster or
hybrid); no new setting. As in Unreal, where Lumen Reflections replace SSR, the SSR pass is off while it runs.

1. **Tracing** (`RayTrace.hlsl`, `REFLECTIONS` raygen) at half resolution: one ray per 2x2 block, at a pixel that
   cycles every frame. Pixels rougher than 0.4 (Lumen's `MaxRoughnessToTrace`) are not traced. The direction is a
   GGX visible-normal sample (Heitz 2018) of the G-buffer normal and roughness; hits are shaded as probe hits (direct
   light, world probe irradiance for further bounces, emissive); misses take the sky. Rays are long (scene scale), so
   off-screen and distant surfaces reflect.
   As built: one ray per pixel (Lumen's DownsampleFactor 1); half resolution left a 0.3-rough metal floor flickering.
   All probe-type rays (world probes, screen probes, reflections) now light their hits as Lumen's surface cache does:
   diffuse only, the specular colour folded into the albedo (Unreal's EnvBRDFApproxFullyRough, + 0.45 x F0), since a
   mirror floor's sharp highlights reached the probes as fireflies (6.5% flickering pixels).
2. **Reconstruction + temporal** (`ReflectionResolve.hlsl`, full resolution). Each traced pixel reuses the rays of
   nearby pixels (Lumen's screen-space reconstruction: up to 8 neighbours within a radius that grows with
   roughness, each weighted by this pixel's BRDF for that ray's direction over the pdf it was drawn with, plus depth
   and normal agreement), giving the BRDF-weighted mean radiance. Pixels rougher than 0.4 take it from the screen
   probes' SH evaluated in the reflection direction with a GGX-width lobe (Lumen reuses the screen probe gather for
   rough reflections); 0.3-0.4 fades between the two. The result is reprojected with the camera (history kept where
   normal and distance match, as the screen probe resolve), clamped to the range of this frame's neighbouring rays
   and averaged up to 12 frames (Lumen's default).
   As built: reuse radius at most 4 px (8 px biased a 0.3-rough floor 9%); the SH lobe is at most cosine-wide (L1 SH
   cannot hold a sharper one, and a sharper band 1 only added noise); 24 frames for traced and 32 for the screen probe
   path, the clamp only below roughness 0.15 (12 frames and a clamp everywhere left a glossy floor at +0.37 points of
   flicker); reprojection by the unjittered screen motion, as Unreal's velocity (TAA jitter made the history resample
   between pixels every frame and blurred it; this also applies to the screen probe histories).
3. **Lighting.** `LightingPS.hlsl` (opaque pass) multiplies that radiance by the split-sum term
   (F0 x A + B, energy compensated) in place of the cubemap specular; IBL diffuse and the transparent pass are
   unchanged.

Barriers are just-in-time inside `ReflectionPass::ExecutePass`.

## Not in scope

Screen-space traces before the ray-traced ones, hit-distance (virtual point) reprojection for mirror reflections,
specular occlusion, reflections on translucent surfaces, tile compaction of traced pixels.

## Verification

Pool test `gi-reflections-vulkan`/`-dx12` (PrintWindow grabs), Cornell box with a polished metal floor:
1. **Accuracy** against a reference from the same build with reconstruction off, every pixel traced at full
   resolution and a 1000-frame average (captured with `--baseline`): reflected wall and block regions within 6% at
   roughness 0.15, and the rough path (0.6) within 15% of the traced reference. As built: the floor's mean absolute
   difference at roughness 0.02, 0.3 and 0.6, at most 3.5/255 (the 24-frame result's residual noise reads a few
   percent brighter in the tonemapper's toe; at 1000 frames it matches the reference within 1%).
2. **Before/after:** the floor reflects the red and green walls (it is black with the cubemap path and IBL off).
3. **Flicker:** GI on within 0.2 points of GI off at a still camera; orbiting no worse than world probes. As built:
   counted on surfaces (edges excluded: TAA jitter flickers them with or without GI, more where GI brightens the
   surfaces beside them), editor grid hidden.
4. **Regression:** the GI pool set, `render-warnings`, `linear-color`; clean logs.

Performance: Sponza Release, GI on: `Reflections` at most +1.0 ms GPU at 1440p (scaled from the editor viewport).
