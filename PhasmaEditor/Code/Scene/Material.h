#pragma once

#include "Base/ResourceManager.h"

namespace pe
{
    class Image;
    class Sampler;

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

    private:
        Material *m_parent;

        std::optional<vec4> m_baseColorOverride;
        std::optional<float> m_metallicOverride;
        std::optional<float> m_roughnessOverride;
        std::optional<float> m_alphaCutoffOverride;
        std::optional<vec3> m_emissiveOverride;
        std::optional<float> m_occlusionStrengthOverride;
        std::optional<RenderType> m_renderTypeOverride;
        std::optional<uint32_t> m_textureMaskOverride;

        ResourceHandle<Image> m_textureOverrides[5];
        bool m_hasTextureOverride[5] = {};
    };
} // namespace pe
