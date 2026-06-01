#include "Render/SceneSky.h"
#include "API/Image.h"
#include "Skybox/Skybox.h"

namespace pe
{
    namespace
    {
        std::filesystem::path ResolveSceneSkyPath(const std::string &path)
        {
            if (path.empty())
                return {};

            std::filesystem::path skyboxPath(path);
            if (skyboxPath.is_relative())
                skyboxPath = std::filesystem::path(Path::Assets) / skyboxPath;

            return skyboxPath.lexically_normal();
        }

        std::string PathToUtf8String(const std::filesystem::path &path)
        {
            auto u8 = path.u8string();
            return std::string(u8.begin(), u8.end());
        }

        std::string NormalizeSeparators(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        bool IsRelativeInsideAssets(const std::filesystem::path &relative)
        {
            if (relative.empty() || relative.is_absolute())
                return false;

            for (const auto &part : relative)
            {
                if (part == "..")
                    return false;
            }
            return true;
        }

        vec4 FallbackSceneSkyColor()
        {
            return vec4(0.48f, 0.58f, 0.68f, 1.0f);
        }

        const std::string &ConfiguredSceneSkyPath()
        {
            const auto &settings = Settings::Get<GlobalSettings>();
            return settings.skybox_path;
        }
    } // namespace

    std::string MakeSceneSkyPathSetting(const std::string &path)
    {
        if (path.empty())
            return {};

        std::error_code ec;
        const std::filesystem::path absolutePath =
            std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
        const std::filesystem::path normalizedPath = ec ? std::filesystem::path(path).lexically_normal() : absolutePath;

        ec.clear();
        const std::filesystem::path assetsRoot = std::filesystem::weakly_canonical(std::filesystem::path(Path::Assets), ec);
        if (!ec)
        {
            std::error_code relEc;
            const std::filesystem::path relativeToAssets = std::filesystem::relative(normalizedPath, assetsRoot, relEc);
            if (!relEc && IsRelativeInsideAssets(relativeToAssets))
                return NormalizeSeparators(PathToUtf8String(relativeToAssets));
        }

        return NormalizeSeparators(PathToUtf8String(normalizedPath));
    }

    bool LoadSceneSkyPath(CommandBuffer *cmd, SkyBox &skybox, const std::string &path)
    {
        const std::filesystem::path resolvedPath = ResolveSceneSkyPath(path);
        if (resolvedPath.empty())
        {
            LoadFallbackSceneSky(cmd, skybox);
            return false;
        }

        const std::string skyboxPath = NormalizeSeparators(PathToUtf8String(resolvedPath));
        if (skybox.LoadSkyBox(cmd, skyboxPath))
            return true;

        PE_WARN("[SceneSky] Using solid-color sky fallback for missing skybox: %s", skyboxPath.c_str());
        LoadFallbackSceneSky(cmd, skybox);
        return false;
    }

    void LoadConfiguredSceneSkybox(CommandBuffer *cmd, SkyBox &skybox)
    {
        LoadSceneSkyPath(cmd, skybox, ConfiguredSceneSkyPath());
    }

    void LoadDefaultSceneSky(CommandBuffer *cmd, SkyBox &skybox, Image *&iblBrdfLut)
    {
        LoadConfiguredSceneSkybox(cmd, skybox);

        Image::LoadRawParams loadImageParams = {256, 256, PE_FORMAT_R16G16_SFLOAT, false, true, 0.0f};
        iblBrdfLut = Image::LoadRaw(cmd, Path::Assets + "Objects/ibl_brdf_lut_rg16f_256.bin", loadImageParams);
    }

    void LoadFallbackSceneSky(CommandBuffer *cmd, SkyBox &skybox)
    {
        skybox.LoadSolidColor(cmd, FallbackSceneSkyColor(), "Skybox_SolidColor");
    }

    void DestroyDefaultSceneSky(SkyBox &skybox, Image *&iblBrdfLut)
    {
        skybox.Destroy();
        Image::Destroy(iblBrdfLut);
    }
} // namespace pe
