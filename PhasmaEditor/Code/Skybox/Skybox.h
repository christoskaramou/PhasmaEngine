#pragma once

namespace pe
{
    class Image;

    class SkyBox
    {
    public:
        void LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames, uint32_t imageSideSize);
        void Destroy();
        Image *GetCubeMap() const { return m_cubeMap; }

    private:
        Image *m_cubeMap;
    };
}
