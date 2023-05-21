#pragma once

namespace pe
{
    class Downsampler
    {
    public:
        static void Init();

        static void Dispatch(CommandBuffer *cmd, Image *image);

        static void Destroy();

        inline static void ResetCounter() { s_currentIndex = 0; }

    private:
        static void UpdatePassInfo();

        static void CreateUniforms();

        static void UpdateDescriptorSet();

        static uvec2 SpdSetup();

        static void SetInputImage(Image *image);

        static void ResetInputImage();

        inline static std::mutex s_dispatchMutex{};

        inline static std::shared_ptr<PassInfo> s_passInfo{};

        // Downsampler is not reusable within a command recording, it can be with though with multiple descriptors
        inline static const uint32_t MAX_DESCRIPTORS_PER_CMD = 100;
        inline static uint32_t s_currentIndex{};
        inline static Descriptor *s_DSet[MAX_DESCRIPTORS_PER_CMD]{};

        inline static Image *s_image = nullptr; // max 12 mips/views

        inline static uint32_t s_counter[6]{};
        inline static Buffer *s_atomicCounter[MAX_DESCRIPTORS_PER_CMD]{};

        struct PushConstants
        {
            uint32_t mips;
            uint32_t numWorkGroupsPerSlice;
            uvec2 workGroupOffset;
        } inline static s_pushConstants{};
    };
}
