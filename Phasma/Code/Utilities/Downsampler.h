#pragma once

namespace pe
{
    class Downsampler
    {
    public:
        static void Init();

        static void Dispatch(CommandBuffer *cmd, Image *image);

        static void Destroy();

    private:

        static void UpdatePipelineInfo();

        static void CreateUniforms();

        static void UpdateDescriptorSet();

        static uvec2 SpdSetup();

        static void SetInputImage(Image *image);

        static void ResetInputImage();

        inline static std::mutex s_dispatchMutex{};

        inline static Pipeline *s_pipeline;
        inline static std::shared_ptr<PipelineCreateInfo> s_pipelineInfo;
        inline static Descriptor *s_DSet;

        inline static uint32_t s_counter[6];

        // Shader data
        inline static Image *s_image;  // max 12 mips/views, 1st is the image itself
        inline static Buffer *s_atomicCounter;
        struct PushConstants
        {
            uint32_t mips;
            uint32_t numWorkGroupsPerSlice;
            uvec2 workGroupOffset;
        }
        inline static s_pushConstants;
    };
}
