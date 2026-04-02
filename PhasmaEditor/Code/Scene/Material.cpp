#include "Material.h"

namespace pe
{
    Material::Material() = default;

    void Material::PackLegacyFactors(mat4 factors[2]) const
    {
        factors[0] = mat4(1.f);
        factors[1] = mat4(1.f);

        // factors[0] layout:
        // [0] = baseColor rgba
        factors[0][0][0] = baseColorFactor.r;
        factors[0][0][1] = baseColorFactor.g;
        factors[0][0][2] = baseColorFactor.b;
        factors[0][0][3] = baseColorFactor.a;

        // [1] = emissive rgb + transmissionFactor
        factors[0][1][0] = emissiveFactor.r;
        factors[0][1][1] = emissiveFactor.g;
        factors[0][1][2] = emissiveFactor.b;
        factors[0][1][3] = transmissionFactor;

        // [2] = metallic, roughness, alphaCutoff, occlusionStrength
        factors[0][2][0] = metallic;
        factors[0][2][1] = roughness;
        factors[0][2][2] = alphaCutoff;
        factors[0][2][3] = occlusionStrength;

        // [3] = unused, normalScale, unused, unused
        factors[0][3][1] = normalScale;

        // factors[1] layout:
        // [0] = thicknessFactor, attenuationDistance, ior
        factors[1][0][0] = thicknessFactor;
        factors[1][0][1] = attenuationDistance;
        factors[1][0][2] = ior;

        // [1] = attenuationColor rgb
        factors[1][1][0] = attenuationColor.r;
        factors[1][1][1] = attenuationColor.g;
        factors[1][1][2] = attenuationColor.b;
    }

    void Material::UnpackLegacyFactors(const mat4 factors[2], uint32_t mask)
    {
        baseColorFactor.r = factors[0][0][0];
        baseColorFactor.g = factors[0][0][1];
        baseColorFactor.b = factors[0][0][2];
        baseColorFactor.a = factors[0][0][3];

        emissiveFactor.r = factors[0][1][0];
        emissiveFactor.g = factors[0][1][1];
        emissiveFactor.b = factors[0][1][2];
        transmissionFactor = factors[0][1][3];

        metallic = factors[0][2][0];
        roughness = factors[0][2][1];
        alphaCutoff = factors[0][2][2];
        occlusionStrength = factors[0][2][3];

        normalScale = factors[0][3][1];

        thicknessFactor = factors[1][0][0];
        attenuationDistance = factors[1][0][1];
        ior = factors[1][0][2];

        attenuationColor.r = factors[1][1][0];
        attenuationColor.g = factors[1][1][1];
        attenuationColor.b = factors[1][1][2];

        textureMask = mask;
    }

    size_t Material::ComputeHash() const
    {
        size_t h = 0;
        auto combine = [&h](size_t val)
        {
            h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
        };

        combine(std::hash<float>{}(baseColorFactor.r));
        combine(std::hash<float>{}(baseColorFactor.g));
        combine(std::hash<float>{}(baseColorFactor.b));
        combine(std::hash<float>{}(baseColorFactor.a));
        combine(std::hash<float>{}(metallic));
        combine(std::hash<float>{}(roughness));
        combine(std::hash<float>{}(alphaCutoff));
        combine(std::hash<float>{}(emissiveFactor.r));
        combine(std::hash<float>{}(emissiveFactor.g));
        combine(std::hash<float>{}(emissiveFactor.b));
        combine(std::hash<float>{}(transmissionFactor));
        combine(std::hash<float>{}(occlusionStrength));
        combine(std::hash<float>{}(normalScale));
        combine(std::hash<float>{}(thicknessFactor));
        combine(std::hash<float>{}(ior));
        combine(static_cast<size_t>(renderType));
        combine(static_cast<size_t>(textureMask));
        combine(static_cast<size_t>(doubleSided));

        for (int i = 0; i < 5; i++)
            combine(std::hash<void *>{}(static_cast<void *>(textures[i].get())));

        return h;
    }

    bool Material::Equals(const Material &other) const
    {
        if (renderType != other.renderType)
            return false;
        if (textureMask != other.textureMask)
            return false;
        if (doubleSided != other.doubleSided)
            return false;

        if (baseColorFactor != other.baseColorFactor)
            return false;
        if (metallic != other.metallic)
            return false;
        if (roughness != other.roughness)
            return false;
        if (alphaCutoff != other.alphaCutoff)
            return false;
        if (emissiveFactor != other.emissiveFactor)
            return false;
        if (occlusionStrength != other.occlusionStrength)
            return false;
        if (normalScale != other.normalScale)
            return false;
        if (transmissionFactor != other.transmissionFactor)
            return false;
        if (thicknessFactor != other.thicknessFactor)
            return false;
        if (ior != other.ior)
            return false;
        if (attenuationDistance != other.attenuationDistance)
            return false;
        if (attenuationColor != other.attenuationColor)
            return false;

        for (int i = 0; i < 5; i++)
        {
            if (textures[i].get() != other.textures[i].get())
                return false;
        }

        return true;
    }

    MaterialGpuData Material::BuildGpuData() const
    {
        MaterialGpuData data{};
        data.baseColorFactor = baseColorFactor;
        data.emissiveTransmission = vec4(emissiveFactor, transmissionFactor);
        data.pbrParams = vec4(metallic, roughness, alphaCutoff, occlusionStrength);
        data.transmissionVolume = vec4(thicknessFactor, attenuationDistance, ior, 0.f);
        data.attenuationColor = vec4(attenuationColor, 0.f);
        return data;
    }

    // --- MaterialInstance ---

    MaterialInstance::MaterialInstance(Material *parent)
        : m_parent(parent)
    {
    }

    void MaterialInstance::SetBaseColorFactor(const vec4 &v)
    {
        m_baseColorOverride = v;
    }
    void MaterialInstance::SetMetallic(float v)
    {
        m_metallicOverride = v;
    }
    void MaterialInstance::SetRoughness(float v)
    {
        m_roughnessOverride = v;
    }
    void MaterialInstance::SetAlphaCutoff(float v)
    {
        m_alphaCutoffOverride = v;
    }
    void MaterialInstance::SetEmissiveFactor(const vec3 &v)
    {
        m_emissiveOverride = v;
    }
    void MaterialInstance::SetOcclusionStrength(float v)
    {
        m_occlusionStrengthOverride = v;
    }
    void MaterialInstance::SetNormalScale(float v)
    {
        m_normalScaleOverride = v;
    }
    void MaterialInstance::SetTransmissionFactor(float v)
    {
        m_transmissionFactorOverride = v;
    }
    void MaterialInstance::SetThicknessFactor(float v)
    {
        m_thicknessFactorOverride = v;
    }
    void MaterialInstance::SetAttenuationDistance(float v)
    {
        m_attenuationDistanceOverride = v;
    }
    void MaterialInstance::SetIor(float v)
    {
        m_iorOverride = v;
    }
    void MaterialInstance::SetAttenuationColor(const vec3 &v)
    {
        m_attenuationColorOverride = v;
    }
    void MaterialInstance::SetRenderType(RenderType type)
    {
        m_renderTypeOverride = type;
    }
    void MaterialInstance::SetTextureMask(uint32_t mask)
    {
        m_textureMaskOverride = mask;
    }

    void MaterialInstance::SetTexture(TextureType slot, ResourceHandle<Image> img)
    {
        int idx = static_cast<int>(slot);
        if (idx < 0 || idx >= 5)
            return;
        m_textureOverrides[idx] = img;
        m_hasTextureOverride[idx] = true;
    }

    void MaterialInstance::ClearBaseColorOverride()
    {
        m_baseColorOverride.reset();
    }
    void MaterialInstance::ClearMetallicOverride()
    {
        m_metallicOverride.reset();
    }
    void MaterialInstance::ClearRoughnessOverride()
    {
        m_roughnessOverride.reset();
    }

    void MaterialInstance::ClearTextureOverride(TextureType slot)
    {
        int idx = static_cast<int>(slot);
        m_textureOverrides[idx] = ResourceHandle<Image>();
        m_hasTextureOverride[idx] = false;
    }

    bool MaterialInstance::HasOverrides() const
    {
        if (m_baseColorOverride || m_metallicOverride || m_roughnessOverride ||
            m_alphaCutoffOverride || m_emissiveOverride || m_occlusionStrengthOverride ||
            m_normalScaleOverride || m_transmissionFactorOverride || m_thicknessFactorOverride ||
            m_attenuationDistanceOverride || m_iorOverride || m_attenuationColorOverride ||
            m_renderTypeOverride || m_textureMaskOverride)
            return true;

        for (int i = 0; i < 5; i++)
        {
            if (m_hasTextureOverride[i])
                return true;
        }

        return false;
    }

    Material MaterialInstance::Resolve() const
    {
        Material resolved = *m_parent;

        if (m_baseColorOverride)
            resolved.baseColorFactor = *m_baseColorOverride;
        if (m_metallicOverride)
            resolved.metallic = *m_metallicOverride;
        if (m_roughnessOverride)
            resolved.roughness = *m_roughnessOverride;
        if (m_alphaCutoffOverride)
            resolved.alphaCutoff = *m_alphaCutoffOverride;
        if (m_emissiveOverride)
            resolved.emissiveFactor = *m_emissiveOverride;
        if (m_occlusionStrengthOverride)
            resolved.occlusionStrength = *m_occlusionStrengthOverride;
        if (m_normalScaleOverride)
            resolved.normalScale = *m_normalScaleOverride;
        if (m_transmissionFactorOverride)
            resolved.transmissionFactor = *m_transmissionFactorOverride;
        if (m_thicknessFactorOverride)
            resolved.thicknessFactor = *m_thicknessFactorOverride;
        if (m_attenuationDistanceOverride)
            resolved.attenuationDistance = *m_attenuationDistanceOverride;
        if (m_iorOverride)
            resolved.ior = *m_iorOverride;
        if (m_attenuationColorOverride)
            resolved.attenuationColor = *m_attenuationColorOverride;
        if (m_renderTypeOverride)
            resolved.renderType = *m_renderTypeOverride;
        if (m_textureMaskOverride)
            resolved.textureMask = *m_textureMaskOverride;

        for (int i = 0; i < 5; i++)
        {
            if (m_hasTextureOverride[i])
                resolved.textures[i] = m_textureOverrides[i];
        }

        return resolved;
    }

    MaterialGpuData MaterialInstance::BuildGpuData() const
    {
        MaterialGpuData data{};
        data.baseColorFactor = GetBaseColorFactor();
        data.emissiveTransmission = vec4(GetEmissiveFactor(), GetTransmissionFactor());
        data.pbrParams = vec4(GetMetallic(), GetRoughness(), GetAlphaCutoff(), GetOcclusionStrength());
        data.transmissionVolume = vec4(GetThicknessFactor(), GetAttenuationDistance(), GetIor(), 0.f);
        data.attenuationColor = vec4(GetAttenuationColor(), 0.f);
        return data;
    }

    Image *MaterialInstance::GetTexture(int slot) const
    {
        if (slot < 0 || slot >= 5)
            return nullptr;
        if (m_hasTextureOverride[slot])
            return m_textureOverrides[slot].get();
        return m_parent->textures[slot].get();
    }

    void MaterialInstance::ApplyDiffFromResolved(const Material &resolved)
    {
        if (resolved.baseColorFactor != m_parent->baseColorFactor)
            SetBaseColorFactor(resolved.baseColorFactor);
        if (resolved.metallic != m_parent->metallic)
            SetMetallic(resolved.metallic);
        if (resolved.roughness != m_parent->roughness)
            SetRoughness(resolved.roughness);
        if (resolved.alphaCutoff != m_parent->alphaCutoff)
            SetAlphaCutoff(resolved.alphaCutoff);
        if (resolved.emissiveFactor != m_parent->emissiveFactor)
            SetEmissiveFactor(resolved.emissiveFactor);
        if (resolved.occlusionStrength != m_parent->occlusionStrength)
            SetOcclusionStrength(resolved.occlusionStrength);
        if (resolved.normalScale != m_parent->normalScale)
            SetNormalScale(resolved.normalScale);
        if (resolved.transmissionFactor != m_parent->transmissionFactor)
            SetTransmissionFactor(resolved.transmissionFactor);
        if (resolved.thicknessFactor != m_parent->thicknessFactor)
            SetThicknessFactor(resolved.thicknessFactor);
        if (resolved.attenuationDistance != m_parent->attenuationDistance)
            SetAttenuationDistance(resolved.attenuationDistance);
        if (resolved.ior != m_parent->ior)
            SetIor(resolved.ior);
        if (resolved.attenuationColor != m_parent->attenuationColor)
            SetAttenuationColor(resolved.attenuationColor);
        if (resolved.renderType != m_parent->renderType)
            SetRenderType(resolved.renderType);
        if (resolved.textureMask != m_parent->textureMask)
            SetTextureMask(resolved.textureMask);

        for (int i = 0; i < 5; i++)
        {
            if (resolved.textures[i].get() != m_parent->textures[i].get())
                SetTexture(static_cast<TextureType>(i), resolved.textures[i]);
        }
    }

} // namespace pe
