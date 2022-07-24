#pragma once

namespace pe
{
    class Descriptor;
    class Image;

    class SkyBox
    {
    public:
        SkyBox();

        ~SkyBox();

        Image *cubeMap;
        Descriptor *DSet;

        void createDescriptorSet();

        void loadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames, uint32_t imageSideSize, bool show = true);

        void loadTextures(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSideSize);

        void destroy();
    };
}