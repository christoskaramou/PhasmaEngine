#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    class SkyBox;

    std::string MakeSceneSkyPathSetting(const std::string &path);
    void LoadConfiguredSceneSkybox(CommandBuffer *cmd, SkyBox &skybox);
    bool LoadSceneSkyPath(CommandBuffer *cmd, SkyBox &skybox, const std::string &path);
    void LoadDefaultSceneSky(CommandBuffer *cmd, SkyBox &skybox, Image *&iblBrdfLut);
    void LoadFallbackSceneSky(CommandBuffer *cmd, SkyBox &skybox);
    void DestroyDefaultSceneSky(SkyBox &skybox, Image *&iblBrdfLut);
} // namespace pe
