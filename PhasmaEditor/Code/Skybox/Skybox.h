#pragma once

namespace pe
{
    class Image;

    class SkyBox
    {
    public:
        void LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames);
        void LoadSkyBox(CommandBuffer *cmd, const std::string &path);
        void Destroy();
        Image *GetCubeMap() const { return m_cubeMap; }

    private:
        Image *m_cubeMap;
    };
} // namespace pe
