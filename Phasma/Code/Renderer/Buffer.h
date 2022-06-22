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
    struct MemoryRange
    {
        void *data;    // source data
        size_t size;   // source data size in bytes
        size_t offset; // offset to destination data in bytes
    };

    class Buffer : public IHandle<Buffer, BufferHandle>
    {
    public:
        Buffer(size_t size,
               BufferUsageFlags usage,
               AllocationCreateFlags createFlags,
               const std::string &name);

        ~Buffer();

        void Map();

        void Unmap();

        void Flush(size_t size = 0, size_t offset = 0) const;

        void Zero() const;

        void Copy(uint32_t count, MemoryRange *ranges, bool persistent);

        size_t Size();

        void *Data();

    private:
        friend class CommandBuffer;

        void CopyBuffer(CommandBuffer* cmd, Buffer *src, size_t size, size_t srcOffset, size_t dstOffset);

        void CopyBufferStaged(CommandBuffer *cmd, void *data, size_t size, size_t dtsOffset);
        
        void CopyDataRaw(const void *data, size_t size, size_t offset = 0);

        size_t size;
        void *data;
        BufferUsageFlags usage;
        AllocationHandle allocation;
        std::string name;
    };
}