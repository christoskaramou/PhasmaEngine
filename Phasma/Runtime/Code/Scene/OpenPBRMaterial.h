#pragma once

#include "Scene/Material.h"
#include "Scene/OpenPBRShapes.h"

namespace pe
{
    inline constexpr uint32_t kOpenPBRModuleAbi = 9;

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
        uint32_t index = 0;   // runtime only: stamped into each record, never saved
        OpenPBRLayout layout; // the first source's names: read only for valid, byteSize and types/offsets
        std::string code;     // the generated module file (every pass compiles it in its shape group)
        std::string document; // the first document with this code, named by its compile warning
        // Runtime only: see RejectOpenPBRModule.
        mutable std::atomic<bool> rejected = false;
        // Runtime only: the session's shape list position, set by the first GetOpenPBRPassSources (main thread).
        mutable std::atomic<uint32_t> shapeIndex = UINT32_MAX;
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
        OpenPBRLayout layout;                                         // its own input names; types, offsets and byteSize equal the module's
        std::unordered_map<std::string, MaterialParamValue> defaults; // by OpenPBRInput::identity
        std::unordered_map<std::string, OpenPBRTexture> textures;     // by OpenPBRInput::identity
    };

    // A MaterialX material's bytes in the scene material byte buffer: the legacy MaterialGpuData record first (the
    // raster G-buffer, RT hits and the off path read it, all but transmissionVolume.w), then the module's inputs.
    inline constexpr uint32_t kOpenPBRInputsOffset = sizeof(MaterialGpuData);
    inline constexpr uint32_t kOpenPBRModuleOffset = offsetof(MaterialGpuData, transmissionVolume) + 3 * sizeof(uint32_t);
    static_assert(kOpenPBRInputsOffset == 96, "PE_OPENPBR_INPUTS_OFFSET in VisibilityCommon.slang must match");
    static_assert(kOpenPBRModuleOffset == 60, "PE_OPENPBR_MODULE_OFFSET in VisibilityCommon.slang must match");

    std::shared_ptr<const OpenPBRMaterialSource> GetOpenPBRDefaultSource();
    const OpenPBRMaterialSource &GetOpenPBRSource(const MaterialInstance &instance); // the default when none is assigned
    const OpenPBRModule &GetOpenPBRModule(uint32_t index);

    // Passes compile shape groups, not modules: one combined module (OpenPBRShapes.h) per group of the project's
    // append-only shape list (Assets/MaterialX/Cooked/shapes_abi<N>.txt), every shape on Vulkan, 8 on DX12, which inlines
    // each shape into the entry function. A module's slot in its group is fixed for the session, so pipelines built
    // from any version of its group still match it.
    struct OpenPBRPassSources
    {
        std::string thin, shade, mask, forward, hit; // ShaderCache/MaterialX/group_<key>.<pass>.slang
        std::string key;                             // FNV-1a 64 of the combined text
        uint32_t members = 0;
    };

    // Main thread. The group's current members: registered or cooked in the project, not rejected. A group marked broken
    // compiles the module's shape alone.
    OpenPBRPassSources GetOpenPBRPassSources(const OpenPBRModule &module);
    uint32_t GetOpenPBRShapeSlot(const OpenPBRModule &module); // pushed by thin, shade, forward and masks; RT's table
    void MarkOpenPBRGroupBroken(const std::string &key);

    // Main thread. The RT hit groups of a module list: each module's current shape group (GetOpenPBRPassSources), one
    // hit group per distinct group, in shape-list order. Every RT pass builds from the same set so their SBTs agree.
    OpenPBRHitSet MakeOpenPBRHitSet(const std::vector<uint32_t> &modules);

    // Loads a cooked source from Assets/MaterialX/Cooked, or generates it with PhasmaCook --mtlx when missing.
    // A relative path resolves like any asset. Null with `error` set (and logged once) when it cannot be used.
    std::shared_ptr<const OpenPBRMaterialSource> LoadOpenPBRMaterial(
        const std::string &path, const std::string &material, std::string &error);

    // The material can be shaded by the module: the module is valid and the material is standard PBR (no pass, or
    // standard_pbr.pass, whose byte record is MaterialGpuData). Shader-driven materials own their bytes.
    bool OpenPBREligible(Material &material, const OpenPBRModule &module);

    // A module whose pass pipelines failed to build (a generated shader that does not compile) is rejected instead of
    // ending the editor: one "[OpenPBR] <document>: shader compile failed: <first error line>" warning, and the mesh
    // predicates below then refuse it, so the next material repack drops it and its meshes render legacy. Scene restores
    // and saves keep the assignment (OpenPBREligible ignores it). Lasts the session. Thread-safe (RT hit builds call it on
    // a worker).
    void RejectOpenPBRModule(const OpenPBRModule &module, const std::string &error);
    uint32_t OpenPBRRejectionCount(); // bumped by each rejection: Scene::Update repacks when it changes

    // The visibility path: also requires an opaque or alpha-cut mesh.
    bool MeshUsesOpenPBR(const Mesh &mesh);

    // The forward pass (Phase 6): an eligible MaterialX mesh whose render type is alpha-blend or transmission.
    bool MeshUsesOpenPBRForward(const Mesh &mesh);

    // MaterialX draws a surface with transmission as transparent, refracting the environment. On the visibility path
    // the traced-reflection override answers the refraction lobe too, so glass renders as chrome. When a document is
    // assigned (AssignOpenPBRMaterial, a cooked model's own document), an opaque mesh whose instance has a constant
    // transmission_weight > 0 moves to the transmission render type, mesh and instance, as set_render_type does. A
    // graph-driven weight keeps the render type: MaterialX counts it as transparent, but the forward pass writes no
    // depth, which would break a mostly opaque textured mesh's self-occlusion. Other render types, later
    // set_render_type calls, set_openpbr edits and scene restores are left alone. Returns whether the routing changed.
    bool SelectOpenPBRRenderType(Mesh &mesh);

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
    // hits, the raster G-buffer under the material passes) shows the same base colour, opacity (its alpha), metalness,
    // roughness, IOR and emission. A connected input has no constant: its legacy field keeps the material's own value.
    void MirrorOpenPBRToLegacy(MaterialInstance &instance);

    // Light a sun shadow keeps through one surface of a Transmission mesh (MaterialGpuData::shadowTransmittance): the
    // uncovered share, plus the covered share's non-metallic transmission tinted by transmission_color and the coat.
    vec3 OpenPBRShadowTransmittance(const MaterialInstance &instance);
} // namespace pe
