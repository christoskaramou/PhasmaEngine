#pragma once

#include "Scene/Material.h"

namespace pe
{
    struct Mesh;

    // One input of PE's default OpenPBR module (PhasmaCook --mtlx; RuntimeAssets/Shaders/MaterialX/Generated).
    struct OpenPBRInput
    {
        std::string input;           // OpenPBR input name, e.g. base_color
        std::string type;            // float, float3 or bool (the default module's types)
        uint32_t offset = 0;         // byte offset inside the inputs block
        std::vector<float> defaults; // MaterialX default (bool as 0/1)
    };

    struct OpenPBRLayout
    {
        std::vector<OpenPBRInput> inputs;
        uint32_t byteSize = 0;
        bool valid = false;

        const OpenPBRInput *Find(const std::string &input) const;
    };

    // A MaterialX material's bytes in the scene material byte buffer: the legacy MaterialGpuData record first (the
    // raster G-buffer, RT hits and the off path read it), then the module's inputs.
    inline constexpr uint32_t kOpenPBRInputsOffset = sizeof(MaterialGpuData);
    static_assert(kOpenPBRInputsOffset == 80, "PE_OPENPBR_INPUTS_OFFSET in VisibilityCommon.slang must match");

    // Loaded once from the generated .json; invalid (after one PE_WARN) when the .json or the .slang module is missing or
    // the layout is malformed. Invalid means: no material is eligible and the material passes stay disabled.
    const OpenPBRLayout &GetOpenPBRDefaultLayout();

    // The material can be shaded by the default module: the module is ready and the material is standard PBR
    // (no pass, or standard_pbr.pass, whose byte record is MaterialGpuData). Shader-driven materials own their bytes.
    bool OpenPBREligible(Material &material);
    bool MeshUsesOpenPBR(const Mesh &mesh);

    // The value an input resolves to: the instance's param override, then its material's params, then the default.
    void ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input, float out[3]);
    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance);

    // Legacy fields follow the constant inputs, so every legacy consumer (openpbr off, Android, unsupported GPUs, RT
    // hits, the raster G-buffer under the material passes) shows the same base colour, metalness, roughness, IOR and emission.
    void MirrorOpenPBRToLegacy(MaterialInstance &instance);
} // namespace pe
