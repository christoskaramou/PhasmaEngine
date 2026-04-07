#pragma once

#include "Base/ResourceManager.h"
#include "Scene/MaterialReflection.h"

namespace pe
{
    class Image;
    class Sampler;
    class PassInfoAsset;
    struct StructMemberInfo;

    // GPU-side material data — uploaded to a StructuredBuffer for shader access.
    // Must match the HLSL MaterialGpuData struct in Structures.hlsl exactly.
    struct MaterialGpuData
    {
        vec4 baseColorFactor;      // rgba
        vec4 emissiveTransmission; // xyz = emissive, w = transmissionFactor
        vec4 pbrParams;            // x = metallic, y = roughness, z = alphaCutoff, w = occlusionStrength
        vec4 transmissionVolume;   // x = thicknessFactor, y = attenuationDistance, z = ior, w = unused
        vec4 attenuationColor;     // xyz = attenuationColor, w = unused
    };

    // Identifies which VS/PS shaders a material uses per render pass.
    // nullptr in Material::shaderAsset means StandardPBR (existing GBufferVS/GBufferPS).
    // Future custom materials will set this to a non-default ShaderAsset.
    struct ShaderAsset
    {
        std::string name;
        // Per-pass shader file paths (relative to Assets/Shaders/).
        // Empty string = use the pass's default shader for that stage.
        std::string gBufferVS;
        std::string gBufferPS;
        std::string depthVS;
        std::string depthPS;
    };

    // Variant type for shader-driven material parameters
    using MaterialParamValue = std::variant<float, vec2, vec3, vec4, int32_t, uint32_t>;

    // Get byte size of a MaterialParamValue
    inline uint32_t GetParamValueSize(const MaterialParamValue &v)
    {
        return std::visit([](const auto &val) -> uint32_t
                          { return sizeof(val); },
                          v);
    }

    // Write a MaterialParamValue into a byte buffer at the given offset
    inline void WriteParamValue(std::vector<uint8_t> &buffer, uint32_t offset, const MaterialParamValue &v)
    {
        std::visit([&](const auto &val)
                   { memcpy(buffer.data() + offset, &val, sizeof(val)); },
                   v);
    }

    // First-class material asset. Stores named PBR parameters and texture slots.
    // Multiple meshes can reference the same Material for sharing.
    // Use MaterialInstance for per-mesh overrides of a shared parent.
    class Material
    {
    public:
        Material();
        ~Material() = default;

        // --- PBR parameters ---
        vec4 baseColorFactor = vec4(1.f);
        vec3 emissiveFactor = vec3(0.f);
        float metallic = 0.f;
        float roughness = 1.f;
        float alphaCutoff = 0.5f;
        float occlusionStrength = 1.f;
        float normalScale = 1.f;
        float transmissionFactor = 0.f;

        // --- Transmission / volume ---
        float thicknessFactor = 0.f;
        float attenuationDistance = std::numeric_limits<float>::infinity();
        float ior = 1.5f;
        vec3 attenuationColor = vec3(1.f);

        // --- Render state ---
        RenderType renderType = RenderType::Opaque;
        bool doubleSided = false;

        // --- Textures ---
        ResourceHandle<Image> textures[5];
        Sampler *samplers[5]{nullptr};
        uint32_t textureMask = 0;

        // --- Identity ---
        std::string name;
        std::filesystem::path filePath; // .mat file path if saved

        // --- Legacy serialization bridge ---
        // Pack parameters into the materialFactors[2] mat4 layout for scene file persistence.
        void PackLegacyFactors(mat4 factors[2]) const;

        // --- Deduplication ---
        // Compute a hash for dedup (based on params, textures, render type).
        size_t ComputeHash() const;

        // Value equality for dedup (all params, textures, render type).
        bool Equals(const Material &other) const;

        // Unpack legacy materialFactors into named fields.
        void UnpackLegacyFactors(const mat4 factors[2], uint32_t mask);

        // Build GPU-side material data for the material table.
        MaterialGpuData BuildGpuData() const;

        // GPU material table index (assigned by Scene::CreateMaterialTable).
        uint32_t gpuIndex = 0xFFFFFFFF;

        // Dirty flag — set when scalar/texture parameters change via UI.
        // Scene::UpdateDirtyMaterials() uses this for incremental GPU updates.
        bool dirty = false;

        // Shader override — nullptr means StandardPBR (pass uses its default VS/PS).
        ShaderAsset *shaderAsset = nullptr;

        // --- Shader-driven material (used when passInfoAsset is set) ---
        ResourceHandle<PassInfoAsset> passInfoAsset; // replaces shaderAsset for new materials

        // Key-value parameter storage — keys match reflected struct member names
        std::unordered_map<std::string, MaterialParamValue> params;

        // Texture assignments by binding name (e.g. "pe_tex_baseColor")
        std::unordered_map<std::string, ResourceHandle<Image>> namedTextures;

        // Bindless image view indices for named textures (populated by UpdateImageViews)
        std::unordered_map<std::string, uint32_t> namedTextureIndices;

        // Populate params map from legacy PBR fields.
        // Call after loading a material to bridge legacy fields to the shader-driven system.
        void SyncParamsFromLegacy();

        // Write params map back to legacy PBR fields.
        // Call after the material editor changes params so legacy consumers stay in sync.
        void SyncLegacyFromParams();

        // Build raw byte buffer for ByteAddressBuffer upload.
        // totalByteSize: if non-zero, overrides the computed buffer size (use raw struct size).
        std::vector<uint8_t> BuildByteAddressData(
            const std::vector<StructMemberInfo> &layout,
            const std::vector<MaterialTextureSlot> &textureSlots = {},
            uint32_t totalByteSize = 0) const;

        // Total byte size of this material's GPU data (from reflected struct size)
        uint32_t gpuByteSize = 0;

        // Byte offset in the scene's ByteAddressBuffer (assigned by scene)
        uint32_t gpuByteOffset = 0xFFFFFFFF;

        // Cached reflected layout — populated by CreateMaterialTable or MaterialEditorWidget
        MaterialLayout cachedLayout;
    };

    // Per-mesh overrides of a shared parent Material.
    // Only overridden fields are stored; everything else falls through to parent.
    class MaterialInstance
    {
    public:
        explicit MaterialInstance(Material *parent);
        ~MaterialInstance() = default;

        Material *GetParent() const { return m_parent; }

        // Override setters
        void SetBaseColorFactor(const vec4 &v);
        void SetMetallic(float v);
        void SetRoughness(float v);
        void SetAlphaCutoff(float v);
        void SetEmissiveFactor(const vec3 &v);
        void SetOcclusionStrength(float v);
        void SetNormalScale(float v);
        void SetTransmissionFactor(float v);
        void SetThicknessFactor(float v);
        void SetAttenuationDistance(float v);
        void SetIor(float v);
        void SetAttenuationColor(const vec3 &v);
        void SetRenderType(RenderType type);
        void SetTexture(TextureType slot, ResourceHandle<Image> img);
        void SetTextureMask(uint32_t mask);

        // Clear a specific override so it falls through to parent
        void ClearBaseColorOverride();
        void ClearMetallicOverride();
        void ClearRoughnessOverride();
        void ClearTextureOverride(TextureType slot);

        // Scalar getters — return override if set, otherwise fall through to parent (no Material copy)
        RenderType GetRenderType() const { return m_renderTypeOverride.value_or(m_parent->renderType); }
        float GetAlphaCutoff() const { return m_alphaCutoffOverride.value_or(m_parent->alphaCutoff); }
        vec4 GetBaseColorFactor() const { return m_baseColorOverride.value_or(m_parent->baseColorFactor); }
        float GetMetallic() const { return m_metallicOverride.value_or(m_parent->metallic); }
        float GetRoughness() const { return m_roughnessOverride.value_or(m_parent->roughness); }
        vec3 GetEmissiveFactor() const { return m_emissiveOverride.value_or(m_parent->emissiveFactor); }
        float GetOcclusionStrength() const { return m_occlusionStrengthOverride.value_or(m_parent->occlusionStrength); }
        float GetNormalScale() const { return m_normalScaleOverride.value_or(m_parent->normalScale); }
        float GetTransmissionFactor() const { return m_transmissionFactorOverride.value_or(m_parent->transmissionFactor); }
        float GetThicknessFactor() const { return m_thicknessFactorOverride.value_or(m_parent->thicknessFactor); }
        float GetAttenuationDistance() const { return m_attenuationDistanceOverride.value_or(m_parent->attenuationDistance); }
        float GetIor() const { return m_iorOverride.value_or(m_parent->ior); }
        vec3 GetAttenuationColor() const { return m_attenuationColorOverride.value_or(m_parent->attenuationColor); }
        uint32_t GetTextureMask() const { return m_textureMaskOverride.value_or(m_parent->textureMask); }

        // Build GPU data without a full Material copy
        MaterialGpuData BuildGpuData() const;

        // Get texture for a slot (override or parent), without copying
        Image *GetTexture(int slot) const;

        // Check if any overrides exist
        bool HasOverrides() const;

        // Set overrides by comparing resolved material against the parent.
        // Any field that differs from parent becomes an override.
        void ApplyDiffFromResolved(const Material &resolved);

        // Resolve: build a complete Material by merging parent + overrides.
        // The result is a temporary — caller should not store it long-term.
        Material Resolve() const;

        // --- Key-value param overrides (for shader-driven materials) ---
        void SetParam(const std::string &name, const MaterialParamValue &value);
        void ClearParam(const std::string &name);
        bool HasParamOverride(const std::string &name) const;
        MaterialParamValue GetParam(const std::string &name) const;

        void SetNamedTexture(const std::string &name, ResourceHandle<Image> img);
        void ClearNamedTexture(const std::string &name);
        Image *GetNamedTexture(const std::string &name) const;

        std::vector<uint8_t> BuildByteAddressData(
            const std::vector<StructMemberInfo> &layout,
            const std::vector<MaterialTextureSlot> &textureSlots = {},
            uint32_t totalByteSize = 0) const;

        // Check if any named texture overrides exist
        bool HasNamedTextureOverrides() const { return !m_namedTextureOverrides.empty(); }

        // Access named texture overrides (for UpdateImageViews)
        const std::unordered_map<std::string, ResourceHandle<Image>> &GetNamedTextureOverrides() const { return m_namedTextureOverrides; }

        // GPU byte offset in the scene's ByteAddressBuffer (per-instance, separate from parent)
        uint32_t gpuByteOffset = 0xFFFFFFFF;
        uint32_t gpuByteSize = 0;

        // Dirty flag for incremental GPU updates
        bool dirty = false;

        // Bindless image view indices for instance texture overrides (populated by UpdateImageViews)
        std::unordered_map<std::string, uint32_t> namedTextureIndices;

    private:
        Material *m_parent;

        std::optional<vec4> m_baseColorOverride;
        std::optional<float> m_metallicOverride;
        std::optional<float> m_roughnessOverride;
        std::optional<float> m_alphaCutoffOverride;
        std::optional<vec3> m_emissiveOverride;
        std::optional<float> m_occlusionStrengthOverride;
        std::optional<float> m_normalScaleOverride;
        std::optional<float> m_transmissionFactorOverride;
        std::optional<float> m_thicknessFactorOverride;
        std::optional<float> m_attenuationDistanceOverride;
        std::optional<float> m_iorOverride;
        std::optional<vec3> m_attenuationColorOverride;
        std::optional<RenderType> m_renderTypeOverride;
        std::optional<uint32_t> m_textureMaskOverride;

        ResourceHandle<Image> m_textureOverrides[5];
        bool m_hasTextureOverride[5] = {};

        // Key-value overrides (for shader-driven materials)
        std::unordered_map<std::string, MaterialParamValue> m_paramOverrides;
        std::unordered_map<std::string, ResourceHandle<Image>> m_namedTextureOverrides;
    };
} // namespace pe
