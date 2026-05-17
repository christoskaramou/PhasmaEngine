#pragma once
#include "API/Pipeline.h"
#include "Base/ResourceManager.h"

namespace pe
{

    class PassInfoAsset : public Resource
    {
    public:
        PassInfoAsset();
        ~PassInfoAsset() override = default;

        void Load() override;
        void Unload() override {}

        bool LoadFromFile(const std::filesystem::path &path);
        void SaveToFile(const std::filesystem::path &path) const;

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
