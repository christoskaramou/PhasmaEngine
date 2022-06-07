/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    constexpr uint32_t AUTO = UINT32_MAX;

    class Descriptor;
    class CommandPool;
    class CommandBuffer;
    class Fence;
    class Semaphore;
    class Buffer;
    class Pipeline;

    class Compute
    {
    public:
        static Compute Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, const std::string &name = {});

        static std::vector<Compute> Create(const std::string &shaderName, size_t sizeIn, size_t sizeOut, uint32_t count, const std::string &name = {});

        static void CreateResources();

        static void DestroyResources();

        Compute();

        ~Compute() = default;

        void UpdateInput(const void *srcData, size_t srcSize = 0, size_t offset = 0);

        void Dispatch(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);

        void WaitFence();

        void Destroy();

        template <class T>
        void CopyDataTo(T *ptr, size_t elements)
        {
            assert(elements * sizeof(T) <= SBOut->Size());

            SBOut->Map();
            memcpy(ptr, SBOut->Data(), elements * sizeof(T));
            SBOut->Unmap();
        }

        void CreatePipeline(const std::string &shaderName);

    private:
        inline static Queue *s_queue = nullptr;
        Buffer *SBIn;
        Buffer *SBOut;
        Pipeline *pipeline;
        Fence *fence;
        Semaphore *semaphore;
        Descriptor *DSCompute;
        CommandBuffer *commandBuffer;

        void CreateComputeStorageBuffers(size_t sizeIn, size_t sizeOut);

        void CreateDescriptorSet();

        void UpdateDescriptorSet();
    };
}