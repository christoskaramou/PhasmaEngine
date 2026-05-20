#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class SkyBox;

    void LoadDefaultSceneSky(CommandBuffer *cmd, SkyBox &day, SkyBox &night, Image *&iblBrdfLut);
    void DestroyDefaultSceneSky(SkyBox &day, SkyBox &night, Image *&iblBrdfLut);
} // namespace pe
