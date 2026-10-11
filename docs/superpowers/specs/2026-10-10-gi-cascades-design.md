# World radiance cache cascades (Lumen roadmap, step 5)

Date: 2026-10-10. Status: continues the Lumen roadmap toward parity with Unreal ("we are making it the same"); follows
Lumen's radiance cache clipmaps (SIGGRAPH 2022, "Lumen: Real-time Global Illumination in Unreal Engine 5").

## Problem

World probe GI ([GlobalIlluminationPass.cpp](../../../Phasma/Runtime/Code/RenderPasses/GlobalIlluminationPass.cpp))
has one 8x8x8 volume. A scene too large to fit it (more than 7 cells of 4x `gi_probe_spacing` per axis) gets a volume
that follows the camera and fades into unoccluded sky light past it. Measured 2026-10-10 with screen probes and
reflections: in a 100 m covered corridor the floor is 53 near the camera and climbs to 93 past ~7 m (far/near 1.74;
GI off reads a flat 115), so a covered space turns sky-bright a few metres away.

## Design

1. **Cascades.** A camera-following volume becomes 4 nested 8x8x8 cascades with spacing s, 2s, 4s, 8s
   (`gi_probe_spacing` = s), each snapped to its own spacing around the camera: at s = 2 they reach +-7, 14, 28 and
   56 m. A volume fitted to a small scene stays one cascade. Probes are numbered across cascades (cascade x 512 +
   local), so the atlases (irradiance, distance, radiance, far radiance, rays) simply grow downward, 4x as tall.
2. **Uniforms.** Per cascade: origin, history origin, spacing; plus the cascade count. Ray length (16 spacings),
   normal bias (0.1 spacing) and the far-field start (1.75 spacings) follow each probe's cascade. Each cascade
   scrolls and keeps its history on its own; a spacing or cascade-count change discards all history.
3. **Lookups.** Lighting (`ProbeLighting`, also at ray hits) and the screen probes' far-field hand-off use the finest
   cascade whose probes surround the point, blended into the next cascade over its outer cell; only the last cascade
   fades into sky light. Screen probe rays take their length from the cascade they hand over to.
4. **Cost.** Every cascade is traced and updated every frame (Lumen amortises distant clipmaps; measure first).

## Verification

- ~~The 100 m corridor: far/near floor at most 1.25.~~ As built: wrong expectation. A reference with 1 km screen
  probe rays (no cache hand-off) shows the same rise (52 -> 87): the far floor nears the corridor's open end. The
  cascades match it (52 -> 87).
- A 40x40 m roof 12 m up on pillars, seen from outside cascade 0: the floor under it within 3/255 of the 1 km-ray
  reference (one volume: 102.1 against 92.4; cascades: 92.8), pool test `gi-cascades`.
- The GI pool set on Vulkan and DX12 (fitted scenes keep one cascade and must not change), clean logs.
- Cost: world probe pass on Sponza (fitted, one cascade: unchanged) and on the corridor (four cascades), on a quiet GPU.
