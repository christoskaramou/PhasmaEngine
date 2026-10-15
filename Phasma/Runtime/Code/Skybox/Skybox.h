#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    class SkyBox
    {
    public:
        bool LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames);
        bool LoadSkyBox(CommandBuffer *cmd, const std::string &path);
        void LoadSolidColor(CommandBuffer *cmd, const vec4 &color, const std::string &name = "Skybox_Cubemap_SolidColor");
        void Destroy();
        Image *GetCubeMap() const { return m_cubeMap; }
        Image *GetOpenPBRRadiance() const { return m_openpbrRadiance; }
        Image *GetOpenPBRIrradiance() const { return m_openpbrIrradiance; }
        void BuildOpenPBREnvironment(CommandBuffer *cmd, Image *equirect = nullptr);
        void DestroyOpenPBREnvironment();
        bool IsLoaded() const { return m_cubeMap != nullptr; }

    private:
        Image *m_cubeMap = nullptr;
        Image *m_openpbrRadiance = nullptr;
        Image *m_openpbrIrradiance = nullptr;
    };
} // namespace pe
