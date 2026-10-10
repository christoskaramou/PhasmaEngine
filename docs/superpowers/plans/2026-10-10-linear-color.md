# Linear Colour Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A per-scene `linear_color` switch that makes PhasmaEngine handle colour the way Unreal does: sRGB colour textures decoded by the GPU, lighting/GI in linear HDR, one sRGB encode in the Tonemap pass.

**Architecture:** Colour-capable textures are created with a mutable format and get a second, sRGB shader-resource view; the bindless table hands that view to the base-colour and emissive slots when the scene is linear. Authored colours (factors, vertex, light, debug, fallback sky) are decoded where they enter the frame; the G-buffer stores albedo sRGB-encoded in 8 bits and its readers decode it; `TonemapPS` encodes once. Off = today's bytes.

**Tech Stack:** C++20, Vulkan 1.2 / DX12 RHI (`Phasma/Core/Code/API`), HLSL (DXC to SPIR-V/DXIL), glm, Lua (sol2) settings, Python pool tests over the editor MCP (`.testpool/tests/pe_editor.py`).

**Spec:** `docs/superpowers/specs/2026-10-10-linear-color-design.md`

## Global Constraints

- `linear_color` default false; with it off, rendered output is identical to the pre-change binary.
- `linear_color` on forces HDR scene colour (`SceneUsesHDR()` true).
- Picker colours stay sRGB-authored and stored unchanged; decoding happens at use.
- Colours above 1 (HDR emissive factors) keep their brightness: decode the colour divided by its largest channel, multiply back.
- Never commit (PhasmaEngine rule): leave every change unstaged; run `clang-format -i` on each touched `.cpp`/`.h`.
- No std headers added to `.cpp`/`.h` (PCH covers them); `third_party/` is off-limits.
- Every task ends with a build of `PhasmaEditor` + `PhasmaEditorModule` in `build-ninja-physics` (Release) via the scratch `build_editor.bat` (`call .testpool\tests\vsenv.cmd` then `cmake --build build-ninja-physics --config Release --target PhasmaEditor PhasmaEditorModule`).
- After every editor run, check `build-ninja-physics/Release/PhasmaEngine.log` for `[WARN]`/`[ERROR]` and explain each.
- Pool tests run on Vulkan and DX12 (`--api vulkan|dx12`), register in `.testpool/catalog.json` (1-space indent).

## Review Focus

1. HDR emissive factors (e.g. lamp 8, 7.5, 7; tint 1.6, 0.8, 0.4): a plain sRGB decode would multiply brightness; expect colour decoded, brightness kept. Pinned in Task 4's test (factor 1.6, 0.8, 0.4 shows G/B 158/80, not 204/102).
2. Live toggling on -> off -> on without reloading: textures, lights and sky follow each time. Pinned in Task 4's test (identity checked after each toggle).
3. A scene saved with `linear_color` true and reloaded must come back linear (views built at load, not only on toggle). Pinned in Task 1 (roundtrip) and Task 4 (identity after reload).
4. One texture file used as base colour by one mesh and as a normal map by another: only the colour slot decodes. Pinned in Task 3's test (normal RT unchanged on/off).
5. DX12 typeless resources: mip generation (UNORM UAV) and the sRGB SRV must both validate. Pinned by running every test with `--api dx12` and `PE_DX12_DEBUG=1`.

---

### Task 0: Off-identity baseline (before any code change)

**Files:**
- Create: `.testpool/tests/linear_color_baseline.py`
- Output: `.testpool/baselines/linear_color/{vulkan,dx12}-{sponza,mix}.png` (gitignored with `.testpool`)

**Interfaces:**
- Produces: `capture_scenes(api) -> dict[str, PIL.Image]` used by Task 5's identity check (same module, imported).

- [ ] **Step 1: Write the capture script.** Two static scenes, TAA and anything time-varying off so frames are deterministic: saved Sponza (`%TEMP%/pe-rendering-research-2026-10-07/ssr-sponza/Assets`, skip 77 if absent) and a "mix" scene covering textured, emissive, sprite-like, line and lit paths.

```python
"""Off-identity baseline: render fixed scenes with the current binary; Task 5 compares linear_color=false against these."""
import argparse, os, sys, tempfile, time
from pathlib import Path
from PIL import Image
from pe_editor import ROOT, editor_scene, lua, tool
from pe_player import skip

OUT = ROOT / ".testpool/baselines/linear_color"
SPONZA = Path(os.environ.get("TEMP", ".")) / "pe-rendering-research-2026-10-07/ssr-sponza/Assets"
STATIC = 'for _,k in ipairs({"taa","bloom","dof","motion_blur","fxaa","cas_sharpening","ssao","ssr"}) do settings.set(k,false) end'
MIX = '''
local function box(name,pos,scale,color,emissive) local n=scene.add_empty_node(name) scene.attach_primitive(n,"cube")
 n:set_position(pos) n:set_scale(scale) material.set(n,"base_color",color) if emissive then material.set(n,"emissive",emissive) end return n end
box("Floor",vec3(0,-0.1,0),vec3(6,0.2,6),vec4(0.5,0.5,0.5,1))
box("Red",vec3(-1.5,0.5,0),vec3(1,1,1),vec4(0.8,0.1,0.1,1))
box("Glow",vec3(0,0.5,0),vec3(1,1,1),vec4(0,0,0,1),vec3(0.5,0.25,0.75))
box("Hot",vec3(1.5,0.5,0),vec3(1,1,1),vec4(0,0,0,1),vec3(1.6,0.8,0.4))
scene.add_directional_light()
return "mix"'''


def shot():
    r = tool("take_scene_screenshot", {"target": "viewport", "max_width": 640})
    assert not r.get("error"), r
    return Image.open(r["path"]).convert("RGB")


def capture_scenes(api):
    shots = {}
    if (SPONZA / "Scenes/sponza.pescene").exists():
        with editor_scene(api, SPONZA):
            lua('scene.load("sponza.pescene") return "ok"'); time.sleep(18)
            lua(STATIC + ' return "ok"'); time.sleep(3)
            shots["sponza"] = shot()
    with tempfile.TemporaryDirectory(prefix="pe-linear-base-") as d:
        assets = Path(d) / "Assets"; (assets / "Agent").mkdir(parents=True)
        (assets / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
        with editor_scene(api, assets):
            lua(MIX); lua(STATIC + ' return "ok"')
            tool("set_camera", {"position": [0, 3, 6], "target": [0, 0.4, 0], "projection": "perspective"})
            time.sleep(4)
            shots["mix"] = shot()
    return shots


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("--api", default="vulkan"); api = parser.parse_args().api
    OUT.mkdir(parents=True, exist_ok=True)
    for name, im in capture_scenes(api).items():
        im.save(OUT / f"{api}-{name}.png")
        print("saved", OUT / f"{api}-{name}.png")
```

- [ ] **Step 2: Check determinism.** Run it twice per API with the current binary and compare: `python -c` diff of the two runs must be 0 max abs difference. If not 0, find the time-varying input (animated sprite, particle, cursor) and switch it off in `STATIC` before continuing.

Run: `cd .testpool/tests && python linear_color_baseline.py --api vulkan` (and `--api dx12`)
Expected: `saved ...vulkan-sponza.png`, `...vulkan-mix.png`; second run identical.

---

### Task 1: `linear_color` setting and HDR forcing

**Files:**
- Modify: `Phasma/Core/Code/Base/Settings.h:159` (after `hdr`)
- Modify: `Phasma/Runtime/Code/Script/Bindings/Settings/SettingsBindings.cpp:33`
- Modify: `Phasma/Runtime/Code/Scene/SceneSerializer.cpp:247` (save) and `:504` (load)
- Modify: `Phasma/Editor/Code/GUI/Widgets/PipelineControls.cpp:159-160`
- Modify: `Phasma/Runtime/Code/Render/SceneRenderTargets.h:24`, `SceneRenderTargets.cpp:44-48`
- Test: `.testpool/tests/linear_color.py` (created here, grows in later tasks)

**Interfaces:**
- Produces: `bool SceneUsesLinearColor();` `vec3 SceneColor(const vec3 &srgb);` `uint32_t SceneColorPacked(uint32_t rgba8);` in `pe` namespace, declared in `SceneRenderTargets.h`.

- [ ] **Step 1: Failing test (settings roundtrip + HDR forced).**

```python
"""linear_color: setting roundtrip, HDR forcing, decode/encode identities (grows per task)."""
import argparse, os, tempfile, time
from pathlib import Path
from PIL import Image
from pe_editor import ROOT, editor_scene, lua, tool
from pe_player import gpu_problems

LOG = ROOT / "build-ninja-physics/Release/PhasmaEngine.log"


def check_setting_roundtrip():
    lua('local n=scene.add_empty_node("RS") n:set_scene_settings(true) settings.set("hdr",false) settings.set("linear_color",true) scene.save("lin.pescene") return "ok"')
    time.sleep(2)
    lua('settings.set("linear_color",false) scene.load("lin.pescene") return "ok"'); time.sleep(5)
    assert lua('return tostring(settings.get("linear_color"))') == "true", "linear_color did not survive save/load"
    viewport = tool("capture_image_resource", {"id": "rt:viewport", "max_width": 64})
    assert viewport.get("format") == "R16G16B16A16_SFLOAT", f"linear_color did not force HDR scene colour: {viewport.get('format')}"


def main():
    p = argparse.ArgumentParser(); p.add_argument("--api", default="vulkan"); api = p.parse_args().api
    os.environ["PE_DX12_DEBUG"] = "1"
    with tempfile.TemporaryDirectory(prefix="pe-linear-") as d:
        assets = Path(d) / "Assets"; (assets / "Agent").mkdir(parents=True); (assets / "Scenes").mkdir()
        (assets / "Agent/agent_config.json").write_text('{"mcp":true}', encoding="utf-8")
        with editor_scene(api, assets):
            check_setting_roundtrip()
    lines = LOG.read_text(errors="replace").splitlines()
    bad = [l for l in lines if "[WARN]" in l or "[ERROR]" in l]
    assert not bad, "\n".join(sorted(set(bad))[:20])
    assert not gpu_problems(lines)
    print(f"PASS: {api} linear_color", flush=True)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run, expect FAIL** (`settings.set: unknown setting 'linear_color'` warning / assertion).

Run: `cd .testpool/tests && python linear_color.py --api vulkan`

- [ ] **Step 3: Implement.**

`Settings.h` after `bool hdr = false;`:
```cpp
        bool linear_color = false; // decode sRGB colour inputs, light in linear, encode once in Tonemap; forces HDR
```
`SettingsBindings.cpp` after `{"hdr", &SceneSettings::hdr},`:
```cpp
        {"linear_color", &SceneSettings::linear_color},
```
`SceneSerializer.cpp` save, after `settings.AddMember("hdr", s.hdr, allocator);`:
```cpp
            settings.AddMember("linear_color", s.linear_color, allocator);
```
load, after the `gSettings.hdr = ...` line:
```cpp
            gSettings.linear_color = settings.HasMember("linear_color") && settings["linear_color"].IsBool() && settings["linear_color"].GetBool();
```
`PipelineControls.cpp` after the HDR tooltip:
```cpp
        changed |= ImGui::Checkbox("Linear Color", &gs.linear_color);
        ui::ItemTooltip("Decode sRGB colour textures and colours before lighting and encode the image once at the end, as Unreal does. Forces HDR scene color and changes how the scene looks.");
```
`SceneRenderTargets.h` next to `bool SceneUsesHDR();`:
```cpp
    bool SceneUsesLinearColor();
    // An sRGB-authored colour as the frame uses it: decoded in a linear-colour scene (values above 1 keep their
    // brightness), unchanged otherwise.
    vec3 SceneColor(const vec3 &srgb);
    uint32_t SceneColorPacked(uint32_t rgba8);
```
`SceneRenderTargets.cpp`: add `#include <glm/gtc/color_space.hpp>` with the other includes, then:
```cpp
    bool SceneUsesHDR()
    {
        const auto &settings = Settings::Get<SceneSettings>();
        return settings.hdr || settings.linear_color || (settings.global_illumination && RHII.GetCaps().rayTracing);
    }

    bool SceneUsesLinearColor()
    {
        return Settings::Get<SceneSettings>().linear_color;
    }

    vec3 SceneColor(const vec3 &srgb)
    {
        if (!SceneUsesLinearColor())
            return srgb;
        const float peak = std::max({srgb.r, srgb.g, srgb.b, 1.0f});
        return glm::convertSRGBToLinear(srgb / peak) * peak;
    }

    uint32_t SceneColorPacked(uint32_t rgba8)
    {
        if (!SceneUsesLinearColor())
            return rgba8;
        const vec3 c = SceneColor(vec3(rgba8 & 0xFF, (rgba8 >> 8) & 0xFF, (rgba8 >> 16) & 0xFF) / 255.0f);
        const auto byte = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (rgba8 & 0xFF000000u);
    }
```
(Check `AabbsVS.hlsl:27` unpack order matches r = low byte before relying on `SceneColorPacked`; adjust shifts to match.)

- [ ] **Step 4: Build, run, expect PASS.** Then `clang-format -i` the five C++ files.

---

### Task 2: sRGB views for colour-capable images (RHI)

**Files:**
- Modify: `Phasma/Core/Code/API/Image.h` (public `GetSrgbSRV`, members `m_srgbSrv`, `m_mutableFormat`, `m_srgbViewWarned`)
- Modify: `Phasma/Core/Code/API/Image.cpp` (`SrgbFormatOf`, `GetSrgbSRV`, `Unload`, `CreateImageAndUpload` ~354-366, `LoadDdsCompressed` desc, constructor stores `desc.mutableFormat`)
- Modify: `Phasma/Core/Code/API/Vulkan/VulkanImageImpl.cpp:605-606` (`eExtendedUsage`)
- Modify: `Phasma/Core/Code/API/Vulkan/VulkanImageViewImpl.cpp:84-100` (usage restriction for a reinterpreted view)
- Modify: `Phasma/Core/Code/API/DX12/Dx12Translate.h` (`ColorToTypeless`), `Phasma/Core/Code/API/DX12/Dx12ImageImpl.cpp:228`

**Interfaces:**
- Produces: `ImageView *Image::GetSrgbSRV();` returns the sRGB view for RGBA8/BGRA8/BC1/2/3/7 UNORM images created with `mutableFormat`; returns `GetSRV()` for formats with no sRGB twin (float, data, already `_SRGB`); warns once and returns `GetSRV()` for a colour format created without `mutableFormat`.

- [ ] **Step 1: Implement `Image` side.**

`Image.h` public, after `GetSRV`:
```cpp
        // The view that decodes sRGB on sampling (colour textures in a linear-colour scene). Same as GetSRV for
        // formats with no sRGB twin.
        ImageView *GetSrgbSRV();
```
private members, after `m_srv`:
```cpp
        ImageView *m_srgbSrv{};
        bool m_mutableFormat = false;
        bool m_srgbViewWarned = false;
```
`Image.cpp` anonymous namespace:
```cpp
        ::PeFormat SrgbFormatOf(::PeFormat format)
        {
            switch (format)
            {
            case PE_FORMAT_R8G8B8A8_UNORM: return PE_FORMAT_R8G8B8A8_SRGB;
            case PE_FORMAT_B8G8R8A8_UNORM: return PE_FORMAT_B8G8R8A8_SRGB;
            case PE_FORMAT_BC1_RGBA_UNORM: return PE_FORMAT_BC1_RGBA_SRGB;
            case PE_FORMAT_BC2_UNORM: return PE_FORMAT_BC2_SRGB;
            case PE_FORMAT_BC3_UNORM: return PE_FORMAT_BC3_SRGB;
            case PE_FORMAT_BC7_UNORM: return PE_FORMAT_BC7_SRGB;
            default: return PE_FORMAT_UNDEFINED;
            }
        }
```
(confirm the exact `PE_FORMAT_*` enum names in `PeFormat` before compiling.)
In `Image::Image(const ImageDesc &desc)` (where `m_format` etc. are assigned): `m_mutableFormat = desc.mutableFormat;`
```cpp
    ImageView *Image::GetSrgbSRV()
    {
        const ::PeFormat srgb = SrgbFormatOf(m_format);
        if (srgb == PE_FORMAT_UNDEFINED)
            return m_srv;
        if (!m_mutableFormat)
        {
            if (!m_srgbViewWarned)
                PE_WARN("[Image] '%s' cannot be sampled as sRGB (created without mutableFormat)", m_name.c_str());
            m_srgbViewWarned = true;
            return m_srv;
        }
        if (!m_srgbSrv)
        {
            ImageViewDesc desc{};
            desc.viewType = PE_IMAGE_VIEW_TYPE_2D;
            desc.format = srgb;
            desc.levelCount = m_mipLevels;
            desc.layerCount = m_arrayLayers;
            m_srgbSrv = ImageView::Create(this, desc, m_name + "_sRGB");
        }
        return m_srgbSrv;
    }
```
`Unload()`: after `ImageView::Destroy(m_srv);` add `ImageView::Destroy(m_srgbSrv);`
`CreateImageAndUpload` after `desc.usage = usage;`:
```cpp
            desc.mutableFormat = SrgbFormatOf(format) != PE_FORMAT_UNDEFINED; // colour formats also get an sRGB view
```
`LoadDdsCompressed`: same line on its `ImageDesc`.

- [ ] **Step 2: Vulkan.** `VulkanImageImpl.cpp`:
```cpp
        if (desc.mutableFormat)
            info.flags |= vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage;
```
`VulkanImageViewImpl.cpp` before `createImageView`: a reinterpreted colour view of a storage image may only be sampled (sRGB formats lack storage support):
```cpp
        vk::ImageViewUsageCreateInfo usageInfo{};
        if (format != owner->m_parent->GetFormat())
        {
            usageInfo.usage = vk::ImageUsageFlagBits::eSampled;
            info.pNext = &usageInfo;
        }
```

- [ ] **Step 3: DX12.** `Dx12Translate.h` next to `DepthToTypeless`:
```cpp
    inline DXGI_FORMAT ColorToTypeless(DXGI_FORMAT f)
    {
        switch (f)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_TYPELESS;
        case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_TYPELESS;
        case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_TYPELESS;
        case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_TYPELESS;
        default: return f;
        }
    }
```
`Dx12ImageImpl.cpp:228`:
```cpp
        m_resourceFormat = IsDepthStencilFormat(m_viewFormat) ? pe_dx12::DepthToTypeless(m_viewFormat)
                           : desc.mutableFormat                ? pe_dx12::ColorToTypeless(m_viewFormat)
                                                               : m_viewFormat;
```
Grep `Dx12ImageImpl.cpp` and `Dx12ImageViewImpl.cpp` for every use of `m_resourceFormat` / resource `Format` when building RTV/UAV/SRV descs or copy footprints; each view must use `m_viewFormat` (or the view desc format), never the typeless resource format.

- [ ] **Step 4: Build; run `render_warnings.py --api vulkan` and `--api dx12`** (existing pool tests) to prove textures still load, mips generate and no validation errors appear. Expected: PASS both. `clang-format -i` touched files.

---

### Task 3: Bindless table picks the sRGB view for colour slots; live toggle

**Files:**
- Modify: `Phasma/Runtime/Code/Scene/SceneBuffers.cpp:694-800` (`UpdateImageViews`), `:2356-2376` (`TryBindCachedTexture`)
- Modify: `Phasma/Runtime/Code/Scene/Scene.h` (member `bool m_linearColorViews = false;`)
- Modify: `Phasma/Runtime/Code/Scene/Scene.cpp:630-641` (per-frame toggle detection)
- Modify: `Phasma/Runtime/Code/Render/SceneSky.cpp:46-49` (fallback sky colour through `SceneColor`)
- Test: `.testpool/tests/linear_color.py` (add `check_texture_slots`)

**Interfaces:**
- Consumes: `Image::GetSrgbSRV()` (Task 2), `SceneUsesLinearColor()`, `SceneColor()` (Task 1).
- Produces: base colour/emissive `meshImageIndex` entries point at sRGB views when linear.

- [ ] **Step 1: Failing test.** A PNG with one flat texel value sRGB 128 (written with PIL into the temp Assets) used as the emissive texture of an unlit cube (base colour black, emissive factor 1) and as the normal map of another cube. Read `rt:emissive` (RGBA16F, PNG readback stores the clamped raw value) at the cube's centre pixel: linear on -> 55 ± 2 (0.216 * 255); off -> 128 ± 1. Read `rt:normal` at the second cube: identical on/off.

```python
def make_texture(assets):
    tex = assets / "Textures/flat128.png"; tex.parent.mkdir(exist_ok=True)
    Image.new("RGBA", (64, 64), (128, 128, 128, 255)).save(tex)
    return "Textures/flat128.png"


def centre_pixel(rt):
    r = tool("capture_image_resource", {"id": rt, "max_width": 256})
    assert not r.get("error"), r
    with Image.open(r["path"]) as im:
        im = im.convert("RGB"); return im.getpixel((im.width // 2, im.height // 2))


def check_texture_slots(assets):
    path = make_texture(assets)
    lua('local n=scene.add_empty_node("Lit") scene.attach_primitive(n,"cube") n:set_scale(vec3(3,3,0.2)) material.set(n,"base_color",vec4(0,0,0,1)) material.set(n,"emissive",vec3(1,1,1)) return "ok"')
    assert not tool("set_node_texture", {"node": "Lit", "slot": "emissive", "path": path}).get("error")
    tool("set_camera", {"position": [0, 0, 3], "target": [0, 0, 0], "projection": "perspective"})
    lua('settings.set("IBL",false) settings.set("shadows",false) settings.set("linear_color",false) return "ok"'); time.sleep(3)
    off = centre_pixel("rt:emissive")
    lua('settings.set("linear_color",true) return "ok"'); time.sleep(3)
    on = centre_pixel("rt:emissive")
    assert abs(off[0] - 128) <= 1, f"off emissive texel {off}"
    assert abs(on[0] - 55) <= 2, f"linear emissive texel {on} (expected sRGB 128 decoded to ~55)"
    assert not tool("set_node_texture", {"node": "Lit", "slot": "normal", "path": path}).get("error")
    lua('settings.set("linear_color",false) return "ok"'); time.sleep(3); n_off = centre_pixel("rt:normal")
    lua('settings.set("linear_color",true) return "ok"'); time.sleep(3); n_on = centre_pixel("rt:normal")
    assert n_off == n_on, f"normal slot changed with linear_color: {n_off} vs {n_on}"
```
Call `check_texture_slots(assets)` in `main()` after the roundtrip. (Confirm `capture_image_resource` ids `rt:emissive` / `rt:normal` with `list_image_resources` once; the emissive readback is pre-encode raw.)

- [ ] **Step 2: Run, expect FAIL** (`linear emissive texel (128, ...)`).

- [ ] **Step 3: Implement.** In `SceneBuffers.cpp` anonymous namespace:
```cpp
        // Base colour (0) and emissive (4) hold colour: a linear-colour scene samples them through the sRGB view.
        ImageView *MaterialSlotView(Image *image, int slot)
        {
            return (slot == 0 || slot == 4) && SceneUsesLinearColor() ? image->GetSrgbSRV() : image->GetSRV();
        }
```
`UpdateImageViews`: key the map by view instead of image, so an image can own two table entries.
```cpp
        OrderedMap<ImageView *, uint32_t> viewsMap{};
        auto indexOf = [&](ImageView *view)
        {
            if (!view)
                view = defaults.white->GetSRV();
            if (viewsMap.insert(view, static_cast<uint32_t>(m_imageViews.size())).first)
                m_imageViews.push_back(view);
            return viewsMap[view];
        };

        for (const ResourceHandle<Image> &image : m_imageStore)
        {
            PE_ERROR_IF(!image->GetSRV(), "UpdateImageViews: image '%s' has no SRV", image->GetName().c_str());
            indexOf(image->GetSRV());
        }
```
slot loop body: `rt.imageViewIndices[k] = image && !isDefault ? indexOf(MaterialSlotView(image, k)) : 0xFFFFFFFF;`
named textures: `mat->namedTextureIndices[name] = indexOf(image->GetSRV());` and the same for instance overrides (keep the existing null-image `0xFFFFFFFF` branches and `PE_ERROR_IF` messages for a null SRV).
`TryBindCachedTexture`: replace `image->GetSRV()` in the `std::find` with `MaterialSlotView(image, textureSlot)`.
Include `Render/SceneRenderTargets.h` in `SceneBuffers.cpp` if not already visible.

`Scene.h` private: `bool m_linearColorViews = false; // linear_color the bindless table was built for`
`Scene.cpp` in the per-frame update, before `UpdateGeometry();`:
```cpp
        if (const bool linear = SceneUsesLinearColor(); linear != m_linearColorViews)
        {
            m_linearColorViews = linear;
            m_texturesDirty = true; // colour slots switch views
            if (SceneRendererHost *host = GetActiveSceneRendererHost())
                host->ReloadSkyFromSettings(); // the fallback sky colour is authored in sRGB
        }
```
`SceneSky.cpp` `FallbackSceneSkyColor()`:
```cpp
            return vec4(SceneColor(vec3(0.48f, 0.58f, 0.68f)), 1.0f);
```

- [ ] **Step 4: Build, run `linear_color.py` on both APIs, expect PASS.** `clang-format -i` touched files.

---

### Task 4: G-buffer, lighting, SSR and ray-traced shading decode; Tonemap encode

**Files:**
- Modify: `Phasma/Runtime/RuntimeAssets/Shaders/Common/Common.hlsl` (helpers); delete unused `SRGBtoLINEAR` from `Tonemap/Tonemap.hlsl:4-9`
- Modify: `Shaders/Common/Structures.hlsl:98-106` and `Phasma/Runtime/Code/RenderPasses/GbufferPass.h:15-23` (`pad1` -> `linearColor`)
- Modify: `Phasma/Runtime/Code/RenderPasses/GbufferPass.cpp:383-387, 745-749` (set flag)
- Modify: `Shaders/Gbuffer/GBufferPS.hlsl:55, 90-96, 100`
- Modify: `Shaders/Terrain/TerrainGBufferPS.hlsl:80-82, 129, 133`, `Shaders/Voxel/VoxelGBufferPS.hlsl:45, 52`
- Modify: `Phasma/Runtime/Code/RenderPasses/LightPass.h:20-22` + `Shaders/Gbuffer/Lighting.hlsl:56-57` (UBO field), `LightPass.cpp` both `Update()`s, `Shaders/Gbuffer/LightingPS.hlsl:45`
- Modify: `Phasma/Runtime/Code/RenderPasses/SSRPass.cpp:125-...` + `Shaders/SSR/SSRPS.hlsl:10-15, 111`
- Modify: `Phasma/Runtime/Code/RenderPasses/RayTracingPass.h:22` + `Shaders/RayTracing/RayTrace.hlsl:94-95, 810-815, 837`, `RayTracingPass.cpp` UBO fill (~157)
- Modify: `Phasma/Runtime/Code/RenderPasses/TonemapPass.cpp:17, 106` + `Shaders/Tonemap/TonemapPS.hlsl`
- Test: `.testpool/tests/linear_color.py` (`check_identities`, `check_lit`)

**Interfaces:**
- Consumes: Task 1 helpers, Task 3 views.
- Produces: HLSL `SrgbToLinear`, `LinearToSrgb`, `SrgbToLinearHdr` in `Common.hlsl`.

- [ ] **Step 1: Failing tests.**

```python
def display_pixel(x, y):
    r = tool("take_scene_screenshot", {"target": "viewport", "max_width": 512})
    assert not r.get("error"), r
    with Image.open(r["path"]) as im:
        im = im.convert("RGB"); return im.getpixel((int(x * im.width), int(y * im.height)))


def check_identities():
    # Unlit emissive colours must show exactly as picked, on and off, across toggles and a reload.
    lua('''local function q(name,x,e) local n=scene.add_empty_node(name) scene.attach_primitive(n,"cube")
 n:set_position(vec3(x,0,0)) n:set_scale(vec3(0.9,0.9,0.1)) material.set(n,"base_color",vec4(0,0,0,1)) material.set(n,"emissive",e) end
q("E1",-1,vec3(0.5,0.25,0.75)) q("E2",0,vec3(0.2,0.6,0.4)) q("Hot",1,vec3(1.6,0.8,0.4))
settings.set("IBL",false) settings.set("shadows",false) settings.set("tonemapping",false)
for _,k in ipairs({"taa","bloom","fxaa","cas_sharpening","ssao","color_grading"}) do settings.set(k,false) end return "ok"''')
    tool("set_camera", {"position": [0, 0, 2.2], "target": [0, 0, 0], "projection": "perspective"})
    want = {(0.30, 0.5): (128, 64, 191), (0.5, 0.5): (51, 153, 102)}
    for state in ("false", "true", "false", "true"):
        lua(f'settings.set("linear_color",{state}) return "ok"'); time.sleep(3)
        for (x, y), rgb in want.items():
            got = display_pixel(x, y)
            assert all(abs(a - b) <= 1 for a, b in zip(got, rgb)), f"linear_color={state}: {got} != {rgb}"
        hot = display_pixel(0.70, 0.5)
        if state == "true":  # colour decoded, brightness kept: G/B show 158/80 (plain decode would give 204/102)
            assert abs(hot[1] - 158) <= 3 and abs(hot[2] - 80) <= 3, f"HDR emissive decode {hot}"
    lua('local n=scene.add_empty_node("RS2") n:set_scene_settings(true) scene.save("ident.pescene") return "ok"'); time.sleep(2)
    lua('scene.load("ident.pescene") return "ok"'); time.sleep(6)
    for (x, y), rgb in want.items():
        got = display_pixel(x, y)
        assert all(abs(a - b) <= 1 for a, b in zip(got, rgb)), f"after reload: {got} != {rgb}"


def encode(v):
    return 12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055


def decode(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def check_lit():
    # One plane lit straight from above, read at the centre. Off mode gives grey = a*d + s and white = d + s with
    # a = 128/255 (the 8-bit albedo of 0.5); linear mode must show encode(decode(a)*d + s) and encode(d + s).
    light = lua('''_G.lit_plane=scene.add_empty_node("Plane") scene.attach_primitive(_G.lit_plane,"cube")
 _G.lit_plane:set_scale(vec3(3,0.05,3)) material.set(_G.lit_plane,"roughness",1) material.set(_G.lit_plane,"metallic",0)
 scene.add_directional_light()
 settings.set("use_Disney_PBR",false) settings.set("IBL",false) settings.set("shadows",false) settings.set("tonemapping",false)
 for _,k in ipairs({"taa","bloom","fxaa","cas_sharpening","ssao","color_grading","ssr"}) do settings.set(k,false) end
 return lights.get_directional_lights()[1].name''')
    tool("set_camera", {"position": [0, 4, 0.01], "target": [0, 0, 0], "projection": "perspective"})

    def read(grey, linear):
        lua(f'material.set(_G.lit_plane,"base_color",vec4({grey},{grey},{grey},1)) settings.set("linear_color",{linear}) return "ok"')
        time.sleep(3)
        return display_pixel(0.5, 0.5)[1] / 255

    for rotation in ([-90, 0, 0], [90, 0, 0]):  # the light shines along its local -Z; one of these points down
        tool("set_node_transform", {"node": light, "rotation": rotation})
        lua('lights.set_directional_light(0, vec3(0,5,0), vec3(1,1,1), 1.5) return "ok"')
        w0 = read(1.0, "false")
        if w0 > 0.05:
            break
    assert 0.2 < w0 < 0.9, f"white plane off-mode value {w0:.3f} outside the unclamped range; adjust the intensity"
    g0 = read(0.5, "false")
    a = 128 / 255
    d = (w0 - g0) / (1 - a)
    s = w0 - d
    w1, g1 = read(1.0, "true"), read(0.5, "true")
    assert abs(w1 - encode(d + s)) <= 2 / 255, f"white: {w1:.4f} vs {encode(d + s):.4f}"
    assert abs(g1 - encode(decode(a) * d + s)) <= 2 / 255, f"grey: {g1:.4f} vs {encode(decode(a) * d + s):.4f}"
```
Call `check_identities()` and `check_lit()` from `main()`, each after `tool("invoke_editor_action", {"action": "file.new_scene", "discard_unsaved": True})`.

- [ ] **Step 2: Run, expect FAIL** (on-mode identity off by the missing encode).

- [ ] **Step 3: HLSL helpers** (`Common.hlsl`, end of file):
```hlsl
float3 SrgbToLinear(float3 c)
{
    return lerp(c / 12.92, pow((max(c, 0.0) + 0.055) / 1.055, 2.4), step(0.04045, c));
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}

// Picker colours above 1 carry brightness: decode the colour, keep the brightness.
float3 SrgbToLinearHdr(float3 c)
{
    float peak = max(max(c.r, c.g), max(c.b, 1.0));
    return SrgbToLinear(c / peak) * peak;
}
```
Delete `SRGBtoLINEAR` from `Tonemap.hlsl` (no callers).

- [ ] **Step 4: G-buffer flag.** `Structures.hlsl` and `GbufferPass.h`: `float pad1;` -> `uint linearColor;` / `uint32_t linearColor;`. `GbufferPass.cpp` at both `PushConstants_GBuffer pushConstants{};` sites add `pushConstants.linearColor = SceneUsesLinearColor() ? 1u : 0u;`.

`GBufferPS.hlsl`:
```hlsl
    float4 authored = input.color * mat.baseColorFactor; // vertex colour and factor are sRGB-authored
    if (pc.linearColor != 0)
        authored.rgb = SrgbToLinear(authored.rgb);
    float4 combinedColor    = sampledBaseColor * authored;
```
```hlsl
    float3 emissive = pc.linearColor != 0 ? SrgbToLinearHdr(mat.emissiveTransmission.xyz) : mat.emissiveTransmission.xyz;
```
```hlsl
    // 8-bit albedo keeps sRGB precision (Unreal's GBuffer base colour is an sRGB target); readers decode it.
    output.albedo = float4(pc.linearColor != 0 ? LinearToSrgb(combinedColor.xyz) : combinedColor.xyz, combinedColor.a);
```
`TerrainGBufferPS.hlsl`: line 82 `albedo = pc.linearColor != 0 ? SrgbToLinear(input.color.rgb) : input.color.rgb;`; line 129 `albedo *= pc.linearColor != 0 ? SrgbToLinear(input.color.rgb) : input.color.rgb;`; line 133 `output.albedo = float4(pc.linearColor != 0 ? LinearToSrgb(albedo) : albedo, 1.0f);`.
`VoxelGBufferPS.hlsl`: line 45 `float4 vertexColor = input.color; if (pc.linearColor != 0) vertexColor.rgb = SrgbToLinear(vertexColor.rgb); float4 combinedColor = albedo * vertexColor;`; line 52 encode as in GBufferPS.

- [ ] **Step 5: Lighting.** `LightPass.h` after `fog_start`: `uint32_t linear_color = 0;`. `Lighting.hlsl` after `cb_fogStart;`: `uint cb_linearColor;`. Both `LightOpaquePass::Update` and `LightTransparentPass::Update`: `m_ubo.linear_color = SceneUsesLinearColor() ? 1u : 0u;`. `LightingPS.hlsl` after the albedo sample:
```hlsl
    if (cb_linearColor != 0)
        albedo.rgb = SrgbToLinear(albedo.rgb);
```
SSR: add `uint32_t linearColor;` to `SSRBlendPC` (C++) and `uint linearColor;` to `PushConstants_SSRBlend` (HLSL), set it from `SceneUsesLinearColor()`, decode `albedo` in `SSRPS.hlsl:111` the same way.

- [ ] **Step 6: Ray tracing.** `RayTracingPass.h`: `uint32_t padding[2];` -> `uint32_t linear_color; uint32_t padding;`. `RayTrace.hlsl`: `uint cb_rtPassPad0;` -> `uint cb_linearColor;`. In the UBO fill in `RayTracingPass.cpp` (next to `ubo.renderMode`): `ubo.linear_color = SceneUsesLinearColor() ? 1u : 0u;`. Closest-hit base colour (810-815):
```hlsl
    float4 combinedColor = color * mat.baseColorFactor;
    if (cb_linearColor != 0)
        combinedColor.rgb = SrgbToLinear(combinedColor.rgb);
```
emissive (837): `float3 emissive = cb_linearColor != 0 ? SrgbToLinearHdr(mat.emissiveTransmission.xyz) : mat.emissiveTransmission.xyz;`. (The any-hit at 750-756 only tests alpha: unchanged.)

- [ ] **Step 7: Tonemap.** `TonemapPS.hlsl` struct gets `uint linearColor;`; after the ACES lerp:
```hlsl
    if (pc.linearColor != 0)
        output.color.rgb = LinearToSrgb(output.color.rgb); // the single display encode; later passes see display values
```
`TonemapPass.cpp` struct (line 17) gets `uint32_t linearColor;`, and at line 106: `pc.linearColor = SceneUsesLinearColor() ? 1u : 0u;`.

- [ ] **Step 8: Build, run `linear_color.py` both APIs, expect PASS.** `clang-format -i` touched C++.

---

### Task 5: Light, debug colours; off-identity and GI checks

**Files:**
- Modify: `Phasma/Runtime/Code/Scene/SceneLights.cpp:139-157` (POD packing)
- Modify: `Phasma/Runtime/Code/RenderPasses/LinesPass.cpp:132-138, 165-170`, `AabbsPass.cpp:114`, `SelectionOutlinePass.cpp:184`
- Test: `.testpool/tests/linear_color.py` (`check_off_identity`, `check_gi`)

- [ ] **Step 1: Failing tests.**

```python
def check_off_identity(api):
    from linear_color_baseline import OUT, capture_scenes
    import numpy as np
    for name, im in capture_scenes(api).items():  # linear_color is false by default
        base = OUT / f"{api}-{name}.png"
        if not base.exists():
            print(f"skip off-identity {name}: no baseline"); continue
        diff = np.abs(np.asarray(im, np.int16) - np.asarray(Image.open(base).convert("RGB"), np.int16)).max()
        assert diff == 0, f"{name}: linear_color off differs from the pre-change binary by {diff}/255"


def check_light_colour():
    # A (1, 0.5, 0.25) point light on a white floor: decoded on-mode G/R must be decode(0.5) = 0.214, not 0.5.
    light = lua('''local n=scene.add_empty_node("Floor") scene.attach_primitive(n,"cube") n:set_scale(vec3(6,0.05,6))
 material.set(n,"roughness",1) scene.add_point_light() settings.set("IBL",false) settings.set("shadows",false)
 settings.set("tonemapping",false) settings.set("use_Disney_PBR",false)
 for _,k in ipairs({"taa","bloom","fxaa","cas_sharpening","ssao","color_grading","ssr"}) do settings.set(k,false) end
 return lights.get_point_lights()[1].name''')
    tool("set_node_transform", {"node": light, "position": [0, 1, 0]})
    lua('lights.set_point_light(0, vec3(0,1,0), vec3(1,0.5,0.25), 1, 10) settings.set("linear_color",true) return "ok"')
    tool("set_camera", {"position": [0, 4, 0.01], "target": [0, 0, 0], "projection": "perspective"})
    time.sleep(3)
    r, g, _ = (c / 255 for c in display_pixel(0.5, 0.5))
    assert 0.1 < r < 0.98, f"floor red {r:.3f} clamped or unlit; adjust the intensity"
    assert abs(decode(g) / decode(r) - decode(0.5)) <= 0.02, f"light colour not decoded: G/R {decode(g) / decode(r):.3f}"


def check_gi():
    # Cornell box (the chunk from render_warnings.py, copied into this module as CORNELL) with IBL off:
    # the GI-lit back wall is brighter once encoded.
    lua(CORNELL)
    tool("set_camera", {"position": [0, 2, 6.9], "target": [0, 1.95, 0], "projection": "perspective"})
    lua('settings.set("IBL",false) settings.set("global_illumination",true) settings.set("linear_color",false) return "ok"'); time.sleep(10)
    off = sum(display_pixel(0.5, 0.35)) / 3
    lua('settings.set("linear_color",true) return "ok"'); time.sleep(10)
    on = sum(display_pixel(0.5, 0.35)) / 3
    assert on > off + 10, f"GI bounce not brighter in linear mode: {on:.1f} vs {off:.1f}"
```
Run `check_off_identity(api)` outside `editor_scene` (it launches its own); run the other two inside, each after `file.new_scene`.

- [ ] **Step 2: Run, expect FAIL** in `check_light_colour` (G/R 0.5: light colours not decoded yet).

- [ ] **Step 3: Implement.** In the POD packing loops of `UpdateLights` (each `push_back(l)`):
```cpp
            if (nodeEnabled(l.nodeId))
            {
                auto &packed = m_directionalLightsPOD.emplace_back(l);
                packed.color = vec4(SceneColor(vec3(l.color)), l.color.w); // colour sRGB-authored, w = intensity
            }
```
(same for point, spot and area lights). Lines: `constants.color = dot(emissive, emissive) > 0.f ? vec4(SceneColor(emissive), baseColor.a) : vec4(SceneColor(vec3(baseColor)), baseColor.a);` and `constants.color = vec4(SceneColor(emissive), baseColor.a);`. AABBs: `constants.color = SceneColorPacked(mesh.aabbColor);`. Outline: `pc.color = vec4(SceneColor(vec3(gs.selection_outline_color_r, gs.selection_outline_color_g, gs.selection_outline_color_b)), gs.selection_outline_color_a);`.

- [ ] **Step 4: Build, run `linear_color.py`, `render_warnings.py`, `gi_range.py`, `gi_flicker.py` on both APIs; expect PASS.** `clang-format -i` touched files.

---

### Task 6: Register tests, performance, wiki

**Files:**
- Modify: `.testpool/catalog.json` (`linear-color-vulkan`, `linear-color-dx12`)
- Modify: `docs/wiki/architecture/rendering.md` (HDR Boundary section: linear colour paragraph with sources and date)

- [ ] **Step 1: Register** both tests (`paths`: `Phasma/Core/Code/API/Image.*`, `Phasma/Core/Code/API/Vulkan/VulkanImage*`, `Phasma/Core/Code/API/DX12/Dx12Image*`, `Phasma/Core/Code/API/DX12/Dx12Translate.h`, `Phasma/Runtime/Code/Scene/SceneBuffers.cpp`, `Phasma/Runtime/Code/Scene/SceneLights.cpp`, `Phasma/Runtime/Code/Render/SceneRenderTargets.*`, `Phasma/Runtime/Code/Render/SceneSky.cpp`, `Phasma/Runtime/Code/RenderPasses/**`, `Phasma/Runtime/RuntimeAssets/Shaders/**`, `Phasma/Core/Code/Base/Settings.h`; cost expensive; requires the editor exe), then `node .../testpool.cjs validate` and `run linear-color-vulkan linear-color-dx12`.

- [ ] **Step 2: Performance.** Release Sponza, immediate present, 10 snapshots 1 s apart: `hdr` on (baseline) vs `linear_color` on (current), `python tools/compare_snapshots.py baseline/ current/`. Expected within thresholds (FPS ≤5 % and ≤1 fps; ms ≤5 % and ≤0.5 ms; VRAM ≤50 MB). Record numbers.

- [ ] **Step 3: Wiki.** Add to `rendering.md` (HDR Boundary): what `linear_color` does (sRGB views for slots 0/4, albedo stored encoded, decode sites, Tonemap encode, forced HDR), the limits from the spec, the pool tests and the perf numbers, "Verified 2026-10-10". Run `bash docs/wiki/tools/lint.sh`.

- [ ] **Step 4: Final checks.** `git status` shows only the intended files; every touched `.cpp`/`.h` clang-formatted; last editor log has no `[WARN]`/`[ERROR]`. Leave everything unstaged.
