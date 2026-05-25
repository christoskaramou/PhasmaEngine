#pragma once

namespace pe
{
    class CommandBuffer;
    class Image;

    namespace SSAOCacaoBackend
    {
        void Init(Image *depth, Image *normalRT, Image *ssaoRT);
        void UpdateSettings();
        void Draw(CommandBuffer *cmd, Image *ssaoRT, mat4 projection, mat4 normalsToView);
        void Resize(Image *depth, Image *normalRT, Image *ssaoRT);
        void Destroy();
    } // namespace SSAOCacaoBackend
} // namespace pe
