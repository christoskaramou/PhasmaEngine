# OpenPBR Forward-Pass Gaps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Status: approved and implemented 2026-10-21, uncommitted (ledger `.superpowers/sdd/2026-10-21-openpbr-forward-gaps/progress.md`). The user chose Decision 1 = A (dominant layer) and Native execution, and accepted the identity re-record. The user chose this work ("Forward-pass gaps") after Phase 7 (`d49c1840`, `621ffdd7`). No commit is authorized.

**Goal:** Close two Phase 6 limits for MaterialX alpha-blend and transmission meshes. TAA gets their motion, and ray tracing sees them at their MaterialX opacity.

**Architecture:**
- **Velocity.** `OpenPBRForwardPass` binds the `velocity` render target as a second colour attachment.
  - `MaterialForwardMain.slang` writes the jitter-corrected NDC motion that `GBufferPS.hlsl` computes, from `GBufferVS`'s `positionCS` / `prevPositionCS`, which the forward variant already outputs.
  - How it combines with what is behind is Decision 1.
- **RT opacity.** `MirrorOpenPBRToLegacy` (`OpenPBRMaterial.cpp`) also copies a constant `geometry_opacity` into the legacy `baseColorFactor.a`.
  - Every RT hit already reads that alpha: Full RT, reflections, GI and shadow rays all go through the legacy hit group for forward meshes. That covers any-hit shadow attenuation `1 - a` and the closest hit's continuation blend.
  - This is the same choke point that already mirrors base colour, metalness, roughness, IOR and emission.

**Tech Stack:** Slang/HLSL (forward PS, `Structures.hlsl` push constants), C++ (`LightPass.cpp` forward pass, `OpenPBRMaterial.cpp`), `.testpool` harness.

**Spec:** [2026-10-13-openpbr-design.md](../specs/2026-10-13-openpbr-design.md), Phase 6 item and the "Legacy fields follow the constant inputs" rule in `OpenPBRMaterial.h`. Limits being closed: wiki `rendering.md`, "Phase 6 forward pass", **Limits** (velocity; RT at legacy alpha).

## Findings this plan is built on (code read 2026-10-21)

- The legacy transparent G-buffer pass writes velocity through `standard_pbr.pass` "transparent" with the `Default` blend (SRC_ALPHA / ONE_MINUS_SRC_ALPHA).
  - `GBufferPS` outputs a `float2`, so the source alpha that blend uses is undefined.
  - MaterialX forward meshes are moved off-screen in that pass (`openpbrForward`), so today nothing writes velocity for them.
- `PushConstants_Lighting` under `PE_OPENPBR_FORWARD` ends at dword 22 (`jointsCount`) and has no jitter. `PushConstants_GBuffer` carries `projJitter` / `prevProjJitter`.
- RT any-hit (`RayTrace.hlsl:219-260`) takes the transparency of alpha-blend meshes from `baseColorFactor.a × vertex colour × texture`. `MirrorOpenPBRToLegacy` keeps that alpha untouched today.
- `capture_image_resource {"id": "rt:velocity", "visualize": "velocity"}` reads the velocity target signed (`v * 0.5 + 0.5`), with blue = |v|. `linear` clamps negative motion to 0. `openpbr_skinning.py` already drives motion with an `update_editor` node script.
- TAA (`TAA/TAA.hlsl`) reprojects with the velocity at the pixel itself: no closest-depth dilation, and `in_depth` is bound but unused. Writing velocity is enough for it.
- `MirrorOpenPBRToLegacy` runs on instance creation, assignment, every input edit and scene load (`SceneSerializer.cpp:1683`). Saved scenes and Player loads therefore get the mirrored opacity without a re-export.
- In Full RT, `OpenPBRForwardPass` needs raster (`SceneRenderGraph.cpp`), so it does not draw. Forward meshes keep TLAS mask 0x02, which Full RT's 0xFF rays and its transparency continuation still hit through the legacy hit group.

## Decision 1: how the forward pass writes velocity (yours)

- **A (recommended): the dominant layer.** Where the surface is at least half opaque (`geometry_opacity >= 0.5`), its motion replaces what is behind; below that, the velocity behind stays.
  - Implementation: the velocity attachment uses the `Default` blend with source alpha `step(0.5, opacity)`.
  - TAA then reprojects the layer that carries most of the pixel. Transmission meshes have opacity 1, so they always write.
- **B: blend by opacity, like the colour.** Velocity becomes `a * glass + (1 - a) * behind`, which is the defined form of the legacy pass's nominal blend.
  - Where the glass and the background move differently, the mixed motion matches neither, so both ghost a little.

## Global Constraints

- **Off path:** `openpbr` off renders byte-identically, except MaterialX meshes whose constant `geometry_opacity` differs from their legacy alpha. Those now show the MaterialX opacity, by design: the legacy fields follow the constant inputs.
  - **User-visible test consequence.** Phase 6's `openpbr_forward.py --part identity` fixture has exactly such a mesh: `MtlxBlend`, with `geometry_opacity` 0.5 over the primitive's legacy alpha 1.0. Its off image changes inside that sphere.
  - Task 1 Step 1b redefines that part:
    - pixels outside the `MtlxBlend` footprint stay identical to the pre-change baseline;
    - the sphere's own pixels must equal a legacy alpha-blend sphere at alpha 0.5;
    - the baseline is then re-recorded on the new build.
  - `openpbr_vis.py`'s off-identity (saved scenes) runs once, to show whether any saved MaterialX state carries a different alpha.
  - `openpbr_mask.py` records its off baseline before assigning MaterialX, and sets the legacy alpha after `geometry_opacity`, so it does not move.
- **No module ABI change.** Only hand-written shaders and C++ change; the generated text is untouched.
- **Tests: crucial only.** One run per API per gate, full resolution, in the sandbox under the GPU lock. Sync only this plan's files.
- **Concurrency.** The DX12 fine-derivatives session (ABI 5) works in this folder and builds in the same `pe-p4-sandbox`, under the same GPU lock.
  - It edits `MaterialXGen.cpp`, `MaterialShadeMain.slang`, possibly `MaterialForward*.slang`, the ABI tests, and `OpenPBRMaterial.h` (the ABI line). This plan edits the comment above `MirrorOpenPBRToLegacy` in that same header.
  - Code edits here start now, on separate lines.
  - Sandbox builds and runs start only after that session has reported and its changes are committed or settled; I ask it via SendMessage. That way results never mix half-finished ABI 5 states with this work.
  - Other sessions' unstaged files stay untouched.
- **Hygiene:** `clang-format -i` on touched C++, the CLAUDE.md layout rules in shaders too, and no commit unless the user says so.

## Review Focus

1. **The velocity attachment's state.** The `velocity` target must be a colour attachment in the forward pass and keep the layout TAA reads after it. Expected: no validation or debug-layer messages on either API.
2. **The velocity format has no alpha channel (RG).** The blend factor must come from the shader's alpha output, not the target. Expected: the `half` case below (opacity 0.3) keeps the background velocity.
3. **Jitter.** The forward velocity must use the same jitter correction as `GBufferPS`. Expected: a static camera with TAA on gives zero velocity on a still forward sphere (`still` case).
4. **Mirroring order.** `geometry_opacity` set by Lua or by the inspector after assignment must re-mirror. Expected: `rt_opacity` sets it after `use_mtlx`.
5. **Connected opacity** (a texture or graph): the legacy alpha keeps the material's own value, as for the other inputs. Expected: unchanged, stated in the wiki.

---

### Task 1: RT opacity

**Files:**
- Modify: `Phasma/Runtime/Code/Scene/OpenPBRMaterial.cpp` (`MirrorOpenPBRToLegacy`) and the `OpenPBRMaterial.h` comment above it (the list of mirrored inputs).
- Test: `.testpool/tests/openpbr_forward.py`, new part `rt_opacity`.

- [ ] **Step 1: Write the failing part `rt_opacity`.**
  - Full RT (`settings.set_render_mode("ray_tracing")`), a ground plane and one sun.
  - Capture A: a legacy alpha-blend sphere with alpha 0.25 and base colour `convertLinearToSRGB(C)`, exactly what the mirror writes.
  - Capture B: a fresh sphere at the same place (legacy alpha 1.0), switched to the default MaterialX module, with `base_color` C (linear) and `geometry_opacity` 0.25 set after assignment.
  - Assert B's sphere and shadow region are within 1/255 of A's.
  - Expected: FAIL. B is opaque, because its legacy alpha stays 1.0.
- [ ] **Step 1b: Redefine `identity`** as Global Constraints describes, then re-record its baseline on the new build.
- [ ] **Step 2:** Mirror a constant `geometry_opacity`, clamped to [0, 1], into `baseColorFactor.a`.
  - Expected: `rt_opacity` passes on Vulkan.

### Task 2: Forward velocity

**Files:**
- Modify: `Phasma/Runtime/RuntimeAssets/Shaders/Common/Structures.hlsl`: dwords 23-26 `projJitter`, `prevProjJitter` under `PE_OPENPBR_FORWARD`.
- Modify: `MaterialForwardMain.slang`: output struct with `SV_TARGET1` velocity under `PE_FORWARD_VELOCITY`.
- Modify: `LightPass.cpp` (`OpenPBRForwardPass`):
  - `m_velocityRT`;
  - the define;
  - the second format and blend;
  - the `DeclareInputs` output;
  - the attachment;
  - the jitter constants from the same source `GbufferTransparentPass` uses;
  - and `LightPass.h` for the member.
- Test: `.testpool/tests/openpbr_forward.py`, new part `velocity`.

- [ ] **Step 1: Write the failing part `velocity`.**
  - A static camera with TAA on, and a background plane.
  - Three spheres: MaterialX alpha-blend at opacity 0.8 (`solid`) and at 0.3 (`half`), each oscillating ±0.05 in x every editor frame (an `update_editor` script, as in `openpbr_skinning.py`), plus a still opacity-0.8 sphere (`still`).
  - Capture `rt:velocity` (visualize `velocity`; the blue channel is |v|, so the sign of the oscillation doesn't matter). Assert:
    - `solid`'s interior pixels are above 2/255;
    - `half`'s interior equals the background (Decision A) or lies between the two (Decision B);
    - `still`'s interior is at most 1/255.
  - Expected: FAIL on `solid` (0 today).
- [ ] **Step 2: Implement per Decision 1.**
  - Expected: `velocity` passes on Vulkan.
  - Also check `identity` and `parity`, the existing parts, on the same build: forward colour unchanged.

### Task 3: Verification and docs

- [ ] **Step 1:** Run `openpbr_forward.py --part rt_opacity,velocity,identity,parity` once on DX12 with the debug layer on.
  - Expected: PASS, with no debug-layer messages.
- [ ] **Step 2: Perf, once each** (CLAUDE.md thresholds):
  - the off-mode Sponza A/B;
  - an `OpenPBRForwardPass` timing A/B on the `openpbr_forward` fixture, for the extra colour attachment and blend.
  - Expected: no regression with `openpbr` off. The forward pass stays within noise, or its cost is stated.
  - Also: one TAA-on viewport screenshot of the drifting `solid` sphere, old beside new, for you to judge the ghosting.
- [ ] **Step 3:** Update the wiki: the Phase 6 **Limits** in `rendering.md` lose these two items and gain the decision, the connected-opacity rule and the measurements. Run `bash docs/wiki/tools/lint.sh`.
  - Expected: lint clean.

## Out of scope (stated)

- Transmission tint at RT hits: legacy hits tint refraction by the mirrored base colour, not MaterialX's `transmission_color`.
- SSR, DOF and motion blur still see the surface behind (no transparent depth or normal from the forward pass).
- OIT and per-mesh sorting stay as Phase 6 left them.
