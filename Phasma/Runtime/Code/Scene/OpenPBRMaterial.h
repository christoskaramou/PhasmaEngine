#pragma once

#include "Scene/Material.h"

namespace pe
{
    inline constexpr uint32_t kOpenPBRModuleAbi = 3;

    struct Mesh;
    class Scene;
    class ImageView;

    // One uniform input of a generated MaterialX module (PhasmaCook --mtlx), identified by node path + input name.
    struct OpenPBRInput
    {
        std::string node;
        std::string input;    // e.g. base_color
        std::string identity; // node.input: the Lua name and the param-override key
        std::string type;     // bool, int, uint, float, float2/3/4 or SamplerTexture2D
        uint32_t offset = 0;  // byte offset inside the inputs block
    };

    struct OpenPBRLayout
    {
        std::vector<OpenPBRInput> inputs;
        std::string surfaceNode; // the open_pbr_surface node: its inputs also answer to their bare names
        uint32_t byteSize = 0;
        bool valid = false;

        const OpenPBRInput *Find(const std::string &name) const;
    };

    // Generated code, its byte layout and its pass pipelines, shared by every material source with the same code.
    // Index 0 is the build-time default module; an invalid default (one PE_WARN) leaves its materials legacy.
    struct OpenPBRModule
    {
        std::string hash;
        uint32_t index = 0; // runtime only: stamped into each record, never saved
        OpenPBRLayout layout;
        std::string thinSource;
        std::string shadeSource;
        std::string maskSource;
        std::string hitSource; // RT closest-hit wrapper (MaterialHit*.slang)
    };

    struct OpenPBRTexture
    {
        std::string file;
        ResourceHandle<Image> image;
    };

    // A .mtlx material: the module that renders it and the values its own document authored.
    struct OpenPBRMaterialSource
    {
        std::string path;
        std::string material;
        std::shared_ptr<const OpenPBRModule> module;
        std::unordered_map<std::string, MaterialParamValue> defaults; // by OpenPBRInput::identity
        std::unordered_map<std::string, OpenPBRTexture> textures;     // by OpenPBRInput::identity
    };

    // A MaterialX material's bytes in the scene material byte buffer: the legacy MaterialGpuData record first (the
    // raster G-buffer, RT hits and the off path read it, all but transmissionVolume.w), then the module's inputs.
    inline constexpr uint32_t kOpenPBRInputsOffset = sizeof(MaterialGpuData);
    inline constexpr uint32_t kOpenPBRModuleOffset = offsetof(MaterialGpuData, transmissionVolume) + 3 * sizeof(uint32_t);
    static_assert(kOpenPBRInputsOffset == 80, "PE_OPENPBR_INPUTS_OFFSET in VisibilityCommon.slang must match");
    static_assert(kOpenPBRModuleOffset == 60, "PE_OPENPBR_MODULE_OFFSET in VisibilityCommon.slang must match");

    std::shared_ptr<const OpenPBRMaterialSource> GetOpenPBRDefaultSource();
    const OpenPBRMaterialSource &GetOpenPBRSource(const MaterialInstance &instance); // the default when none is assigned
    const OpenPBRModule &GetOpenPBRModule(uint32_t index);

    // Loads a cooked source from Assets/MaterialX/Cooked, or generates it with PhasmaCook --mtlx when missing.
    // A relative path resolves like any asset. Null with `error` set (and logged once) when it cannot be used.
    std::shared_ptr<const OpenPBRMaterialSource> LoadOpenPBRMaterial(
        const std::string &path, const std::string &material, std::string &error);

    // The material can be shaded by the module: the module is valid and the material is standard PBR (no pass, or
    // standard_pbr.pass, whose byte record is MaterialGpuData). Shader-driven materials own their bytes.
    bool OpenPBREligible(Material &material, const OpenPBRModule &module);

    // Also requires an opaque or alpha-cut mesh: blended meshes stay legacy until the transparency phase.
    bool MeshUsesOpenPBR(const Mesh &mesh);

    // Production mutations shared by scripts and the inspector; failed validation preserves the instance.
    bool AssignOpenPBRMaterial(Scene &scene, Mesh &mesh,
                               std::shared_ptr<const OpenPBRMaterialSource> source, std::string &error);
    bool SetOpenPBREnabled(Scene &scene, Mesh &mesh, bool enabled, std::string &error);
    bool SetOpenPBRInput(Scene &scene, MaterialInstance &instance, const std::string &name,
                         const MaterialParamValue &value, std::string &error);

    // The value an input resolves to: the instance's param override, then its material's params, then the source's own.
    MaterialParamValue ResolveOpenPBRInput(const MaterialInstance &instance, const OpenPBRInput &input);
    std::vector<uint8_t> BuildOpenPBRBytes(const MaterialInstance &instance,
                                           const std::unordered_map<ImageView *, uint32_t> &imageViewIndices);

    // Legacy fields follow the constant inputs, so every legacy consumer (openpbr off, Android, unsupported GPUs, RT
    // hits, the raster G-buffer under the material passes) shows the same base colour, metalness, roughness, IOR and
    // emission. A connected input has no constant: its legacy field keeps the material's own value.
    void MirrorOpenPBRToLegacy(MaterialInstance &instance);
} // namespace pe
