#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class SkyBox;

    std::string MakeSceneSkyPathSetting(const std::string &path);
    void LoadConfiguredSceneSkyboxes(CommandBuffer *cmd, SkyBox &day, SkyBox &night);
    bool LoadSceneSkyPath(CommandBuffer *cmd, SkyBox &skybox, bool day, const std::string &path);
    void LoadDefaultSceneSky(CommandBuffer *cmd, SkyBox &day, SkyBox &night, Image *&iblBrdfLut);
    void LoadFallbackSceneSky(CommandBuffer *cmd, SkyBox &skybox, bool day);
    void DestroyDefaultSceneSky(SkyBox &day, SkyBox &night, Image *&iblBrdfLut);
} // namespace pe
