#pragma once
#include "Base/ResourceManager.h"

namespace pe
{
    class Shader;
    class PassInfo;

    struct PassVariant
    {
        std::string vertexShader;
        std::string fragmentShader;

        std::string cullMode;
        std::string depthCompareOp;
        bool depthWriteEnable = true;
        bool depthTestEnable = true;
        bool blendEnable = false;

        std::string topology;
        std::string polygonMode;
        float lineWidth = 1.0f;

        bool stencilTestEnable = false;
        std::string stencilFailOp;
        std::string stencilPassOp;
        std::string stencilDepthFailOp;
        std::string stencilCompareOp;
        uint32_t stencilCompareMask = 0xFFu;
        uint32_t stencilWriteMask = 0xFFu;
        uint32_t stencilReference = 0u;

        std::vector<std::string> colorBlendAttachments;
        std::vector<std::string> dynamicStates;
        std::vector<std::string> colorFormats;
        std::string depthFormat;

        std::string materialBufferName;
        std::string materialAnnotation;

        bool HasShaders() const { return !vertexShader.empty() || !fragmentShader.empty(); }
    };

    class PassInfoAsset : public Resource
    {
    public:
        PassInfoAsset();
        ~PassInfoAsset() override = default;

        void Load() override;
        void Unload() override {}

        bool LoadFromFile(const std::filesystem::path &path);
        void SaveToFile(const std::filesystem::path &path) const;

        bool ApplyToPassInfo(PassInfo &passInfo, const std::string &variantName = "surface") const;

        const PassVariant *GetVariant(const std::string &name) const;
        bool HasVariant(const std::string &name) const;

        const std::string &GetName() const { return m_name; }
        const std::filesystem::path &GetFilePath() const { return m_filePath; }
        const std::unordered_map<std::string, PassVariant> &GetVariants() const { return m_variants; }

    private:
        std::string m_name;
        std::filesystem::path m_filePath;
        std::unordered_map<std::string, PassVariant> m_variants;
    };
} // namespace pe
