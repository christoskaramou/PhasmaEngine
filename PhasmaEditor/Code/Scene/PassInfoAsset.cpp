#include "Scene/PassInfoAsset.h"
#include "API/Pipeline.h"
#include "API/Shader.h"
#include "Base/Path.h"
#include <nlohmann/json.hpp>

namespace pe
{
    PassInfoAsset::PassInfoAsset() {}

    void PassInfoAsset::Load()
    {
        LoadFromFile(GetResourceId());
    }

    static vk::CullModeFlags ParseCullMode(const std::string &s)
    {
        if (s == "front")
            return vk::CullModeFlagBits::eFront;
        if (s == "back")
            return vk::CullModeFlagBits::eBack;
        if (s == "none")
            return vk::CullModeFlagBits::eNone;
        if (s == "frontAndBack")
            return vk::CullModeFlagBits::eFrontAndBack;
        return vk::CullModeFlagBits::eFront;
    }

    static vk::CompareOp ParseCompareOp(const std::string &s)
    {
        if (s == "less")
            return vk::CompareOp::eLess;
        if (s == "equal")
            return vk::CompareOp::eEqual;
        if (s == "lessOrEqual")
            return vk::CompareOp::eLessOrEqual;
        if (s == "greater")
            return vk::CompareOp::eGreater;
        if (s == "greaterOrEqual")
            return vk::CompareOp::eGreaterOrEqual;
        if (s == "always")
            return vk::CompareOp::eAlways;
        if (s == "never")
            return vk::CompareOp::eNever;
        if (s == "notEqual")
            return vk::CompareOp::eNotEqual;
        return vk::CompareOp::eLessOrEqual;
    }

    static vk::PrimitiveTopology ParseTopology(const std::string &s)
    {
        if (s == "triangleList")
            return vk::PrimitiveTopology::eTriangleList;
        if (s == "lineList")
            return vk::PrimitiveTopology::eLineList;
        if (s == "lineStrip")
            return vk::PrimitiveTopology::eLineStrip;
        if (s == "pointList")
            return vk::PrimitiveTopology::ePointList;
        if (s == "triangleStrip")
            return vk::PrimitiveTopology::eTriangleStrip;
        if (s == "triangleFan")
            return vk::PrimitiveTopology::eTriangleFan;
        return vk::PrimitiveTopology::eTriangleList;
    }

    static vk::PolygonMode ParsePolygonMode(const std::string &s)
    {
        if (s == "fill")
            return vk::PolygonMode::eFill;
        if (s == "line")
            return vk::PolygonMode::eLine;
        if (s == "point")
            return vk::PolygonMode::ePoint;
        return vk::PolygonMode::eFill;
    }

    static vk::StencilOp ParseStencilOp(const std::string &s)
    {
        if (s == "keep")
            return vk::StencilOp::eKeep;
        if (s == "zero")
            return vk::StencilOp::eZero;
        if (s == "replace")
            return vk::StencilOp::eReplace;
        if (s == "incrementAndClamp")
            return vk::StencilOp::eIncrementAndClamp;
        if (s == "decrementAndClamp")
            return vk::StencilOp::eDecrementAndClamp;
        if (s == "invert")
            return vk::StencilOp::eInvert;
        if (s == "incrementAndWrap")
            return vk::StencilOp::eIncrementAndWrap;
        if (s == "decrementAndWrap")
            return vk::StencilOp::eDecrementAndWrap;
        return vk::StencilOp::eKeep;
    }

    static vk::PipelineColorBlendAttachmentState ParseBlendAttachment(const std::string &s)
    {
        if (s == "additive")
            return PipelineColorBlendAttachmentState::AdditiveColor;
        if (s == "transparency")
            return PipelineColorBlendAttachmentState::TransparencyBlend;
        if (s == "particles")
            return PipelineColorBlendAttachmentState::ParticlesBlend;
        return PipelineColorBlendAttachmentState::Default;
    }

    static vk::DynamicState ParseDynamicState(const std::string &s)
    {
        if (s == "viewport")
            return vk::DynamicState::eViewport;
        if (s == "scissor")
            return vk::DynamicState::eScissor;
        if (s == "lineWidth")
            return vk::DynamicState::eLineWidth;
        if (s == "depthBias")
            return vk::DynamicState::eDepthBias;
        if (s == "blendConstants")
            return vk::DynamicState::eBlendConstants;
        if (s == "stencilCompareMask")
            return vk::DynamicState::eStencilCompareMask;
        if (s == "stencilWriteMask")
            return vk::DynamicState::eStencilWriteMask;
        if (s == "stencilReference")
            return vk::DynamicState::eStencilReference;
        return vk::DynamicState::eViewport;
    }

    static vk::Format ParseFormat(const std::string &s)
    {
        if (s == "r8g8b8a8Unorm")
            return vk::Format::eR8G8B8A8Unorm;
        if (s == "r8g8b8a8Srgb")
            return vk::Format::eR8G8B8A8Srgb;
        if (s == "b8g8r8a8Unorm")
            return vk::Format::eB8G8R8A8Unorm;
        if (s == "r16g16b16a16Sfloat")
            return vk::Format::eR16G16B16A16Sfloat;
        if (s == "r16g16Sfloat")
            return vk::Format::eR16G16Sfloat;
        if (s == "r32g32b32a32Sfloat")
            return vk::Format::eR32G32B32A32Sfloat;
        if (s == "r32Sfloat")
            return vk::Format::eR32Sfloat;
        if (s == "r8Unorm")
            return vk::Format::eR8Unorm;
        if (s == "d32Sfloat")
            return vk::Format::eD32Sfloat;
        if (s == "d24UnormS8Uint")
            return vk::Format::eD24UnormS8Uint;
        if (s == "d32SfloatS8Uint")
            return vk::Format::eD32SfloatS8Uint;
        return vk::Format::eUndefined;
    }

    bool PassInfoAsset::LoadFromFile(const std::filesystem::path &path)
    {
        m_filePath = path;

        std::ifstream file(path);
        if (!file.is_open())
        {
            Log::Error(PeFormat("[PassInfoAsset] Failed to open: %s", path.string().c_str()));
            return false;
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const std::exception &e)
        {
            Log::Error(PeFormat("[PassInfoAsset] Parse error in %s: %s", path.string().c_str(), e.what()));
            return false;
        }

        m_name = j.value("name", path.stem().string());
        m_variants.clear();

        for (auto &[key, val] : j.items())
        {
            if (key == "name" || key == "version")
                continue;

            PassVariant variant;
            variant.vertexShader = val.value("vertexShader", "");
            variant.fragmentShader = val.value("fragmentShader", "");

            variant.cullMode = val.value("cullMode", "");
            variant.depthCompareOp = val.value("depthCompareOp", "");
            variant.depthWriteEnable = val.value("depthWriteEnable", true);
            variant.depthTestEnable = val.value("depthTestEnable", true);
            variant.blendEnable = val.value("blendEnable", false);

            variant.topology = val.value("topology", "");
            variant.polygonMode = val.value("polygonMode", "");
            variant.lineWidth = val.value("lineWidth", 1.0f);

            variant.stencilTestEnable = val.value("stencilTestEnable", false);
            variant.stencilFailOp = val.value("stencilFailOp", "");
            variant.stencilPassOp = val.value("stencilPassOp", "");
            variant.stencilDepthFailOp = val.value("stencilDepthFailOp", "");
            variant.stencilCompareOp = val.value("stencilCompareOp", "");
            variant.stencilCompareMask = val.value("stencilCompareMask", 0xFFu);
            variant.stencilWriteMask = val.value("stencilWriteMask", 0xFFu);
            variant.stencilReference = val.value("stencilReference", 0u);

            if (val.contains("colorBlendAttachments"))
                for (auto &att : val["colorBlendAttachments"])
                    variant.colorBlendAttachments.push_back(att.get<std::string>());

            if (val.contains("dynamicStates"))
                for (auto &ds : val["dynamicStates"])
                    variant.dynamicStates.push_back(ds.get<std::string>());

            if (val.contains("colorFormats"))
                for (auto &cf : val["colorFormats"])
                    variant.colorFormats.push_back(cf.get<std::string>());
            variant.depthFormat = val.value("depthFormat", "");

            if (val.contains("materialBinding"))
            {
                auto &mb = val["materialBinding"];
                variant.materialBufferName = mb.value("bufferName", "");
                variant.materialAnnotation = mb.value("annotation", "");
            }

            m_variants[key] = std::move(variant);
        }

        return true;
    }

    void PassInfoAsset::SaveToFile(const std::filesystem::path &path) const
    {
        nlohmann::json j;
        j["name"] = m_name;
        j["version"] = 1;

        for (auto &[key, variant] : m_variants)
        {
            nlohmann::json vj;

            if (!variant.vertexShader.empty())
                vj["vertexShader"] = variant.vertexShader;
            if (!variant.fragmentShader.empty())
                vj["fragmentShader"] = variant.fragmentShader;

            if (!variant.cullMode.empty())
                vj["cullMode"] = variant.cullMode;
            if (!variant.depthCompareOp.empty())
                vj["depthCompareOp"] = variant.depthCompareOp;
            vj["depthWriteEnable"] = variant.depthWriteEnable;
            vj["depthTestEnable"] = variant.depthTestEnable;
            vj["blendEnable"] = variant.blendEnable;

            if (!variant.topology.empty())
                vj["topology"] = variant.topology;
            if (!variant.polygonMode.empty())
                vj["polygonMode"] = variant.polygonMode;
            if (variant.lineWidth != 1.0f)
                vj["lineWidth"] = variant.lineWidth;

            if (variant.stencilTestEnable)
            {
                vj["stencilTestEnable"] = variant.stencilTestEnable;
                if (!variant.stencilFailOp.empty())
                    vj["stencilFailOp"] = variant.stencilFailOp;
                if (!variant.stencilPassOp.empty())
                    vj["stencilPassOp"] = variant.stencilPassOp;
                if (!variant.stencilDepthFailOp.empty())
                    vj["stencilDepthFailOp"] = variant.stencilDepthFailOp;
                if (!variant.stencilCompareOp.empty())
                    vj["stencilCompareOp"] = variant.stencilCompareOp;
                if (variant.stencilCompareMask != 0xFF)
                    vj["stencilCompareMask"] = variant.stencilCompareMask;
                if (variant.stencilWriteMask != 0xFF)
                    vj["stencilWriteMask"] = variant.stencilWriteMask;
                if (variant.stencilReference != 0)
                    vj["stencilReference"] = variant.stencilReference;
            }

            if (!variant.colorBlendAttachments.empty())
                vj["colorBlendAttachments"] = variant.colorBlendAttachments;

            if (!variant.dynamicStates.empty())
                vj["dynamicStates"] = variant.dynamicStates;

            if (!variant.colorFormats.empty())
                vj["colorFormats"] = variant.colorFormats;
            if (!variant.depthFormat.empty())
                vj["depthFormat"] = variant.depthFormat;

            if (!variant.materialBufferName.empty())
            {
                nlohmann::json mb;
                mb["bufferName"] = variant.materialBufferName;
                if (!variant.materialAnnotation.empty())
                    mb["annotation"] = variant.materialAnnotation;
                vj["materialBinding"] = mb;
            }

            j[key] = vj;
        }

        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << j.dump(2);
    }

    bool PassInfoAsset::ApplyToPassInfo(PassInfo &passInfo, const std::string &variantName) const
    {
        auto it = m_variants.find(variantName);
        if (it == m_variants.end())
            return false;

        const PassVariant &variant = it->second;

        if (!variant.vertexShader.empty())
        {
            passInfo.pVertShader = Shader::Create(
                Path::Assets + variant.vertexShader,
                vk::ShaderStageFlagBits::eVertex,
                "mainVS",
                std::vector<Define>{},
                ShaderCodeType::HLSL);
        }
        if (!variant.fragmentShader.empty())
        {
            passInfo.pFragShader = Shader::Create(
                Path::Assets + variant.fragmentShader,
                vk::ShaderStageFlagBits::eFragment,
                "mainPS",
                std::vector<Define>{},
                ShaderCodeType::HLSL);
        }

        if (!variant.cullMode.empty())
            passInfo.cullMode = ParseCullMode(variant.cullMode);
        if (!variant.depthCompareOp.empty())
            passInfo.depthCompareOp = ParseCompareOp(variant.depthCompareOp);
        passInfo.depthWriteEnable = variant.depthWriteEnable;
        passInfo.depthTestEnable = variant.depthTestEnable;
        passInfo.blendEnable = variant.blendEnable;

        if (!variant.topology.empty())
            passInfo.topology = ParseTopology(variant.topology);
        if (!variant.polygonMode.empty())
            passInfo.polygonMode = ParsePolygonMode(variant.polygonMode);
        passInfo.lineWidth = variant.lineWidth;

        passInfo.stencilTestEnable = variant.stencilTestEnable;
        if (!variant.stencilFailOp.empty())
            passInfo.stencilFailOp = ParseStencilOp(variant.stencilFailOp);
        if (!variant.stencilPassOp.empty())
            passInfo.stencilPassOp = ParseStencilOp(variant.stencilPassOp);
        if (!variant.stencilDepthFailOp.empty())
            passInfo.stencilDepthFailOp = ParseStencilOp(variant.stencilDepthFailOp);
        if (!variant.stencilCompareOp.empty())
            passInfo.stencilCompareOp = ParseCompareOp(variant.stencilCompareOp);
        passInfo.stencilCompareMask = variant.stencilCompareMask;
        passInfo.stencilWriteMask = variant.stencilWriteMask;
        passInfo.stencilReference = variant.stencilReference;

        if (!variant.colorBlendAttachments.empty())
        {
            passInfo.colorBlendAttachments.clear();
            for (const auto &att : variant.colorBlendAttachments)
                passInfo.colorBlendAttachments.push_back(ParseBlendAttachment(att));
        }

        if (!variant.dynamicStates.empty())
        {
            passInfo.dynamicStates.clear();
            for (const auto &ds : variant.dynamicStates)
                passInfo.dynamicStates.push_back(ParseDynamicState(ds));
        }

        if (!variant.colorFormats.empty())
        {
            passInfo.colorFormats.clear();
            for (const auto &cf : variant.colorFormats)
                passInfo.colorFormats.push_back(ParseFormat(cf));
        }
        if (!variant.depthFormat.empty())
            passInfo.depthFormat = ParseFormat(variant.depthFormat);

        return true;
    }

    const PassVariant *PassInfoAsset::GetVariant(const std::string &name) const
    {
        auto it = m_variants.find(name);
        return (it != m_variants.end()) ? &it->second : nullptr;
    }

    bool PassInfoAsset::HasVariant(const std::string &name) const
    {
        return m_variants.contains(name);
    }
} // namespace pe
