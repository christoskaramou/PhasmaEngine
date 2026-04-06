#include "Scene/PassInfoAsset.h"
#include <nlohmann/json.hpp>

namespace pe
{
    PassInfoAsset::PassInfoAsset() {}

    void PassInfoAsset::Load()
    {
        LoadFromFile(GetResourceId());
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
