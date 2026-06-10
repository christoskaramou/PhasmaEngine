#include "Material.h"
#include "API/Reflection.h"
#include "Scene/PassInfoAsset.h"

namespace pe
{
    namespace
    {
        void WriteEffectiveAlphaCutoff(std::vector<uint8_t> &buffer,
                                       const std::vector<StructMemberInfo> &layout,
                                       RenderType renderType,
                                       float alphaCutoff)
        {
            for (const auto &member : layout)
            {
                if (member.name != "alphaCutoff")
                    continue;
                if (member.offset + sizeof(float) > buffer.size())
                    return;

                float effectiveAlphaCutoff = (renderType == RenderType::AlphaCut) ? alphaCutoff : 0.0f;
                WriteParamValue(buffer, member.offset, effectiveAlphaCutoff);
                return;
            }
        }
    } // namespace

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

        // [1] = attenuationColor rgb + nightEmissive
        factors[1][1][0] = attenuationColor.r;
        factors[1][1][1] = attenuationColor.g;
        factors[1][1][2] = attenuationColor.b;
        factors[1][1][3] = nightEmissive;
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
        nightEmissive = factors[1][1][3];

        textureMask = mask;

        if (!passInfoAsset)
            passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::Assets + "PassInfo/standard_pbr.pass");
        SyncParamsFromLegacy();
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
        if (nightEmissive != other.nightEmissive)
            return false;

        for (int i = 0; i < 5; i++)
        {
            if (textures[i].get() != other.textures[i].get())
                return false;
        }

        return true;
    }

    void Material::SyncParamsFromLegacy()
    {
        params["baseColorFactor"] = baseColorFactor;
        params["emissiveFactor"] = emissiveFactor;
        params["transmissionFactor"] = transmissionFactor;
        params["metallic"] = metallic;
        params["roughness"] = roughness;
        params["alphaCutoff"] = (renderType == RenderType::AlphaCut) ? alphaCutoff : 0.0f;
        params["occlusionStrength"] = occlusionStrength;
        params["thicknessFactor"] = thicknessFactor;
        params["attenuationDistance"] = attenuationDistance;
        params["ior"] = ior;
        params["attenuationColor"] = attenuationColor;
        params["nightEmissive"] = nightEmissive;
    }

    void Material::SyncLegacyFromParams()
    {
        auto getFloat = [&](const std::string &name, float fallback) -> float
        {
            auto it = params.find(name);
            if (it == params.end())
                return fallback;
            if (auto *f = std::get_if<float>(&it->second))
                return *f;
            return fallback;
        };
        auto getVec3 = [&](const std::string &name, const vec3 &fallback) -> vec3
        {
            auto it = params.find(name);
            if (it == params.end())
                return fallback;
            if (auto *v = std::get_if<vec3>(&it->second))
                return *v;
            return fallback;
        };
        auto getVec4 = [&](const std::string &name, const vec4 &fallback) -> vec4
        {
            auto it = params.find(name);
            if (it == params.end())
                return fallback;
            if (auto *v = std::get_if<vec4>(&it->second))
                return *v;
            return fallback;
        };

        baseColorFactor = getVec4("baseColorFactor", baseColorFactor);
        emissiveFactor = getVec3("emissiveFactor", emissiveFactor);
        transmissionFactor = getFloat("transmissionFactor", transmissionFactor);
        metallic = getFloat("metallic", metallic);
        roughness = getFloat("roughness", roughness);
        alphaCutoff = getFloat("alphaCutoff", alphaCutoff);
        occlusionStrength = getFloat("occlusionStrength", occlusionStrength);
        thicknessFactor = getFloat("thicknessFactor", thicknessFactor);
        attenuationDistance = getFloat("attenuationDistance", attenuationDistance);
        ior = getFloat("ior", ior);
        attenuationColor = getVec3("attenuationColor", attenuationColor);
        nightEmissive = getFloat("nightEmissive", nightEmissive);
    }

    MaterialGpuData Material::BuildGpuData() const
    {
        MaterialGpuData data{};
        data.baseColorFactor = baseColorFactor;
        data.emissiveTransmission = vec4(emissiveFactor, transmissionFactor);
        float effectiveAlphaCutoff = (renderType == RenderType::AlphaCut) ? alphaCutoff : 0.0f;
        data.pbrParams = vec4(metallic, roughness, effectiveAlphaCutoff, occlusionStrength);
        data.transmissionVolume = vec4(thicknessFactor, attenuationDistance, ior, 0.f);
        data.attenuationColor = vec4(attenuationColor, nightEmissive);
        return data;
    }

    // --- MaterialInstance ---

    MaterialInstance::MaterialInstance(Material *parent)
        : m_parent(parent)
    {
    }

    template <typename T, typename U>
    void MaterialInstance::SetScalarOverride(std::optional<T> &opt, const std::string &paramKey, const U &value)
    {
        opt = value;
        m_paramOverrides[paramKey] = value;
        dirty = true;
    }

    template <typename T>
    void MaterialInstance::ClearScalarOverride(std::optional<T> &opt, const std::string &paramKey)
    {
        opt.reset();
        m_paramOverrides.erase(paramKey);
        dirty = true;
    }

    void MaterialInstance::ResetOptionalForParamKey(const std::string &paramKey)
    {
        if (paramKey == "baseColorFactor")
            m_baseColorOverride.reset();
        else if (paramKey == "metallic")
            m_metallicOverride.reset();
        else if (paramKey == "roughness")
            m_roughnessOverride.reset();
        else if (paramKey == "alphaCutoff")
            m_alphaCutoffOverride.reset();
        else if (paramKey == "emissiveFactor")
            m_emissiveOverride.reset();
        else if (paramKey == "occlusionStrength")
            m_occlusionStrengthOverride.reset();
        else if (paramKey == "transmissionFactor")
            m_transmissionFactorOverride.reset();
        else if (paramKey == "thicknessFactor")
            m_thicknessFactorOverride.reset();
        else if (paramKey == "attenuationDistance")
            m_attenuationDistanceOverride.reset();
        else if (paramKey == "ior")
            m_iorOverride.reset();
        else if (paramKey == "attenuationColor")
            m_attenuationColorOverride.reset();
    }

    void MaterialInstance::SetOptionalForParamKey(const std::string &paramKey, const MaterialParamValue &value)
    {
        if (auto *vp = std::get_if<vec4>(&value))
        {
            if (paramKey == "baseColorFactor")
                m_baseColorOverride = *vp;
        }
        else if (auto *vp = std::get_if<vec3>(&value))
        {
            if (paramKey == "emissiveFactor")
                m_emissiveOverride = *vp;
            else if (paramKey == "attenuationColor")
                m_attenuationColorOverride = *vp;
        }
        else if (auto *fp = std::get_if<float>(&value))
        {
            if (paramKey == "metallic")
                m_metallicOverride = *fp;
            else if (paramKey == "roughness")
                m_roughnessOverride = *fp;
            else if (paramKey == "alphaCutoff")
                m_alphaCutoffOverride = *fp;
            else if (paramKey == "occlusionStrength")
                m_occlusionStrengthOverride = *fp;
            else if (paramKey == "transmissionFactor")
                m_transmissionFactorOverride = *fp;
            else if (paramKey == "thicknessFactor")
                m_thicknessFactorOverride = *fp;
            else if (paramKey == "attenuationDistance")
                m_attenuationDistanceOverride = *fp;
            else if (paramKey == "ior")
                m_iorOverride = *fp;
        }
    }

    void MaterialInstance::SetBaseColorFactor(const vec4 &v)
    {
        SetScalarOverride(m_baseColorOverride, "baseColorFactor", v);
    }
    void MaterialInstance::SetMetallic(float v)
    {
        SetScalarOverride(m_metallicOverride, "metallic", v);
    }
    void MaterialInstance::SetRoughness(float v)
    {
        SetScalarOverride(m_roughnessOverride, "roughness", v);
    }
    void MaterialInstance::SetAlphaCutoff(float v)
    {
        SetScalarOverride(m_alphaCutoffOverride, "alphaCutoff", v);
    }
    void MaterialInstance::SetEmissiveFactor(const vec3 &v)
    {
        SetScalarOverride(m_emissiveOverride, "emissiveFactor", v);
    }
    void MaterialInstance::SetOcclusionStrength(float v)
    {
        SetScalarOverride(m_occlusionStrengthOverride, "occlusionStrength", v);
    }
    void MaterialInstance::SetTransmissionFactor(float v)
    {
        SetScalarOverride(m_transmissionFactorOverride, "transmissionFactor", v);
    }
    void MaterialInstance::SetThicknessFactor(float v)
    {
        SetScalarOverride(m_thicknessFactorOverride, "thicknessFactor", v);
    }
    void MaterialInstance::SetAttenuationDistance(float v)
    {
        SetScalarOverride(m_attenuationDistanceOverride, "attenuationDistance", v);
    }
    void MaterialInstance::SetIor(float v)
    {
        SetScalarOverride(m_iorOverride, "ior", v);
    }
    void MaterialInstance::SetAttenuationColor(const vec3 &v)
    {
        SetScalarOverride(m_attenuationColorOverride, "attenuationColor", v);
    }

    // normalScale is packed into the legacy MaterialGpuData factors matrix
    // (see Material::ToFactors) and is not a top-level scalar in any byte-address
    // shader layout, so it has no m_paramOverrides key.
    void MaterialInstance::SetNormalScale(float v)
    {
        m_normalScaleOverride = v;
        dirty = true;
    }
    // RenderType / TextureMask drive render-path selection and aren't byte-buffer scalars.
    void MaterialInstance::SetRenderType(RenderType type)
    {
        m_renderTypeOverride = type;
        m_paramOverrides["alphaCutoff"] = (type == RenderType::AlphaCut) ? GetAlphaCutoff() : 0.0f;
        dirty = true;
    }
    void MaterialInstance::SetTextureMask(uint32_t mask)
    {
        m_textureMaskOverride = mask;
        dirty = true;
    }

    void MaterialInstance::SetTexture(TextureType slot, ResourceHandle<Image> img)
    {
        int idx = static_cast<int>(slot);
        if (idx < 0 || idx >= 5)
            return;
        m_textureOverrides[idx] = img;
        m_hasTextureOverride[idx] = true;
        dirty = true;
    }

    void MaterialInstance::ClearBaseColorOverride()
    {
        ClearScalarOverride(m_baseColorOverride, "baseColorFactor");
    }
    void MaterialInstance::ClearMetallicOverride()
    {
        ClearScalarOverride(m_metallicOverride, "metallic");
    }
    void MaterialInstance::ClearRoughnessOverride()
    {
        ClearScalarOverride(m_roughnessOverride, "roughness");
    }

    void MaterialInstance::ClearTextureOverride(TextureType slot)
    {
        int idx = static_cast<int>(slot);
        m_textureOverrides[idx] = ResourceHandle<Image>();
        m_hasTextureOverride[idx] = false;
        dirty = true;
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
        float effectiveAlphaCutoff = (GetRenderType() == RenderType::AlphaCut) ? GetAlphaCutoff() : 0.0f;
        data.pbrParams = vec4(GetMetallic(), GetRoughness(), effectiveAlphaCutoff, GetOcclusionStrength());
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

    void MaterialInstance::SetParam(const std::string &name, const MaterialParamValue &value)
    {
        m_paramOverrides[name] = value;
        SetOptionalForParamKey(name, value);
        dirty = true;
    }

    void MaterialInstance::ClearParam(const std::string &name)
    {
        m_paramOverrides.erase(name);
        ResetOptionalForParamKey(name);
        dirty = true;
    }

    bool MaterialInstance::HasParamOverride(const std::string &name) const
    {
        return m_paramOverrides.contains(name);
    }

    MaterialParamValue MaterialInstance::GetParam(const std::string &name) const
    {
        auto it = m_paramOverrides.find(name);
        if (it != m_paramOverrides.end())
            return it->second;

        auto pit = m_parent->params.find(name);
        if (pit != m_parent->params.end())
            return pit->second;

        return float(0.f);
    }

    void MaterialInstance::SetNamedTexture(const std::string &name, ResourceHandle<Image> img)
    {
        m_namedTextureOverrides[name] = img;
        dirty = true;
    }

    void MaterialInstance::ClearNamedTexture(const std::string &name)
    {
        m_namedTextureOverrides.erase(name);
        dirty = true;
    }

    Image *MaterialInstance::GetNamedTexture(const std::string &name) const
    {
        auto it = m_namedTextureOverrides.find(name);
        if (it != m_namedTextureOverrides.end() && it->second)
            return it->second.get();

        auto pit = m_parent->namedTextures.find(name);
        if (pit != m_parent->namedTextures.end() && pit->second)
            return pit->second.get();

        return nullptr;
    }

    std::vector<uint8_t> MaterialInstance::BuildByteAddressData(
        const std::vector<StructMemberInfo> &layout,
        const std::vector<MaterialTextureSlot> &textureSlots,
        uint32_t totalByteSize) const
    {
        std::vector<uint8_t> buffer = m_parent->BuildByteAddressData(layout, textureSlots, totalByteSize);
        if (buffer.empty())
            return buffer;

        // Override scalar params
        for (const auto &member : layout)
        {
            auto it = m_paramOverrides.find(member.name);
            if (it != m_paramOverrides.end())
                WriteParamValue(buffer, member.offset, it->second);
        }
        WriteEffectiveAlphaCutoff(buffer, layout, GetRenderType(), GetAlphaCutoff());

        // Override named texture bindless indices from instance's namedTextureIndices
        for (const auto &slot : textureSlots)
        {
            if (slot.byteOffset == 0xFFFFFFFF)
                continue;
            auto it = namedTextureIndices.find(slot.bindingName);
            if (it != namedTextureIndices.end() && slot.byteOffset + sizeof(uint32_t) <= buffer.size())
                memcpy(buffer.data() + slot.byteOffset, &it->second, sizeof(uint32_t));
        }

        return buffer;
    }

    std::vector<uint8_t> Material::BuildByteAddressData(
        const std::vector<StructMemberInfo> &layout,
        const std::vector<MaterialTextureSlot> &textureSlots,
        uint32_t totalByteSize) const
    {
        if (layout.empty())
            return {};

        uint32_t totalSize = totalByteSize;
        if (totalSize == 0)
        {
            for (const auto &member : layout)
                totalSize = std::max(totalSize, member.offset + member.size);
        }
        totalSize = (totalSize + 3u) & ~3u;

        std::vector<uint8_t> buffer(totalSize, 0);

        for (const auto &member : layout)
        {
            auto it = params.find(member.name);
            if (it != params.end())
            {
                WriteParamValue(buffer, member.offset, it->second);
            }
        }
        WriteEffectiveAlphaCutoff(buffer, layout, renderType, alphaCutoff);

        // Write named texture bindless indices
        for (const auto &slot : textureSlots)
        {
            if (slot.byteOffset == 0xFFFFFFFF)
                continue;
            auto it = namedTextureIndices.find(slot.bindingName);
            uint32_t idx = (it != namedTextureIndices.end()) ? it->second : 0xFFFFFFFF;
            if (slot.byteOffset + sizeof(uint32_t) <= buffer.size())
                memcpy(buffer.data() + slot.byteOffset, &idx, sizeof(uint32_t));
        }

        return buffer;
    }

} // namespace pe
