#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;
    struct Attachment;

    namespace GUIBackend
    {
        bool IsSupported();
        bool SupportsPlatformWindows();
        void ConfigureIO();
        void Init(Attachment *attachment);
        void Shutdown();
        void CreateFontsTexture();
        void RenderDrawData(CommandBuffer *cmd);
        void *RegisterImageTexture(Image *image);
        void ReleaseImageTexture(void *&textureID);
    } // namespace GUIBackend
} // namespace pe
