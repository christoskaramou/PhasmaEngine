#pragma once

#include "Renderer/Buffer.h"

namespace pe
{
    constexpr uint32_t AUTO = UINT32_MAX;

    class Descriptor;
    class CommandBuffer;
    class Buffer;
    class PassInfo;

    class Compute
    {
    public:
        static Compute Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, const std::string &name);

        static std::vector<Compute> Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, uint32_t count, const std::string &name);

        static void DestroyResources();

        Compute();

        ~Compute() = default;

        void UpdateInput(void *data, size_t size, size_t offset = 0);

        void Dispatch(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);

        void Wait();

        void Destroy();

        template <class T>
        void CopyDataTo(T *ptr, size_t elements)
        {
            assert(elements * sizeof(T) <= SBOut->Size());

            SBOut->Map();
            memcpy(ptr, SBOut->Data(), elements * sizeof(T));
            SBOut->Unmap();
        }

        void CreateUniforms(size_t sizeIn, size_t sizeOut);

        void UpdateDescriptorSet();

        void UpdatePassInfo(const std::string &shaderName);

    private:
        Buffer *SBIn;
        Buffer *SBOut;
        Descriptor *DSet;
        CommandBuffer *commandBuffer;
        std::shared_ptr<PassInfo> passInfo;
    };
}