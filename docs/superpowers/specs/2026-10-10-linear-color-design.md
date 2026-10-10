# Linear colour pipeline (Lumen roadmap, step 1)

Date: 2026-10-10. Status: approved direction ("do it the way Lumen/Unreal does it"), per-scene switch.

## Goal

Lighting, GI and post-processing work on linear radiance, as in Unreal, so Lumen-style GI (steps 2-4: noise-free
radiance cache, screen probes, GI reflections) has correct inputs. Today nothing decodes sRGB colour textures and
nothing encodes the final image: albedo is used as stored sRGB bytes, the sky is linear, and the result is written
to a UNORM swapchain unchanged (survey 2026-10-10).

## Unreal's model and ours

| Unreal | PhasmaEngine with `linear_color` on |
|---|---|
| Colour textures carry an sRGB flag; the GPU decodes them when sampling. Normal, roughness and mask textures do not. | Material slots base colour (0) and emissive (4) are sampled through an sRGB view of the same image. Other slots keep the UNORM view. |
| Scene colour is linear HDR (FloatRGBA). | `linear_color` forces HDR scene colour (RGBA16F). |
| GBuffer base colour is an sRGB render target (8-bit, encoded on write, decoded on read). | `GBufferPS` encodes albedo to sRGB before writing the 8-bit albedo target; every reader decodes it. (Scene targets are storage images, so an `_SRGB` target format is not available; the manual encode is equivalent.) |
| Colour pickers show sRGB and store linear (FLinearColor); vertex colours are sRGB FColor converted to linear. | Pickers and stored values are unchanged (sRGB-authored); material factors, vertex colours, light colours, sprite tints and the fallback sky are decoded where they are used. |
| Tonemapper plus output transform encode to sRGB at the end; FXAA and UI come after. | The Tonemap pass (ACES or pass-through) ends with linear to sRGB into the display target; Sharpen, FXAA, colour grading, grid, particles, runtime UI and ImGui stay after it, on display values. |
| Eye adaptation (auto exposure). | Not in this step (fixed exposure 1); a later step if needed. |

## Switch

- `linear_color` scene setting, default false: `SceneSettings`, `.pescene`, Lua `settings`, and a render-path
  checkbox next to HDR in the Pipeline panel.
- Off: rendering is byte-identical to today (ATH unaffected).
- On: HDR scene colour; the decodes and the encode below.
- Toggling rebuilds the bindless view table (`Scene::UpdateImageViews`), which restarts GI averaging; light colours
  repack, which also restarts GI.

## Inputs (only when on)

1. **Textures.** Colour-capable images (RGBA8 and BC1/2/3/7 UNORM) are created with a mutable format (Vulkan
   `MUTABLE_FORMAT | EXTENDED_USAGE`, DX12 typeless resource) and get a second, sRGB shader-resource view.
   Storage/mip generation keeps the UNORM view. `Scene::UpdateImageViews` puts the sRGB view in the bindless table
   for slots 0 and 4; the material table indexes it. Raster
   G-buffer, transparent, ray-traced and GI probe hits all read the same table, so they all see linear albedo.
2. **G-buffer albedo.** `GBufferPS` writes `LinearToSRGB(albedo)`; lighting (opaque, transparent), GI shading and
   any other reader of the albedo target decode it.
3. **Emissive.** Linear after the sRGB view and the factor decode; written to the emissive target (RGBA16F in HDR).
4. **Colours.** Base colour and emissive factors, vertex colours, light colours (decoded on the CPU when lights are
   packed), sprite tints (they travel as emissive factors), particle colours drawn before the encode (none today:
   particles draw after Tonemap), and the fallback sky colour.
5. **Debug colours drawn before Tonemap** (lines, AABBs, selection outline): decoded so they show as picked.
6. **Already linear, unchanged:** skybox and IBL (stb linearises 8-bit skies, `.hdr` is linear), terrain layers
   (`TerrainGBufferPS` keeps its own decode; its vertex and tint colours get decoded), the voxel atlas (already
   `_SRGB`; its vertex colours get decoded).

## Output

`TonemapPS` applies ACES as today, then `LinearToSRGB` when `linear_color` is on. Everything after Tonemap is
unchanged and receives display-encoded values, as today. The swapchain stays UNORM; screenshots and MCP captures
read the encoded display target and stay correct.

## Not in scope

- Exposure / eye adaptation.
- Gamma-correct mip generation (mips stay averaged in encoded space, as today).
- Re-tuning ATH or converting stored colours.
- Script fullscreen passes (ATH ink) that write display-authored colours into scene colour: a linear scene that
  uses them must decode their constants itself.
- glTF linear factors / COLOR_0 are decoded like picker colours (wrong only for values other than 0 and 1).
- Named textures of custom material passes keep their UNORM view (the `sRGB` annotation hint stays unused).

## Verification

Pool tests on Vulkan and DX12:
1. Off: Sponza and a mixed test scene (lit, emissive, HDR emissive) match the pre-change binary byte for byte. (No ATH
   launch: the editor on the ATH project risks its save data.)
2. On, unlit: an emissive sRGB colour (lights, IBL, tonemapping off) reads back as the same display value, within 1/255.
3. On, lit: a plane with sRGB 0.5 base colour under a known directional light matches the analytic display value
   within 2/255.
4. On, GI: Cornell-box bounce with IBL off is brighter than with the switch off.
5. On, textures: a texture sampled in slot 0 decodes (sRGB 0.5 texel -> linear 0.214 before lighting) while the
   same file in the normal slot does not.
6. No `[WARN]`/`[ERROR]` lines, no validation errors.

Performance: Sponza Release, linear on versus `hdr` on: within the project thresholds (sRGB sampling is free; one
encode per pixel in Tonemap).
