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
               AllocationCreateFlags createFlags = 0,
               const std::string &name = {});

        ~Buffer();

        void Map();

        void Unmap();

        void Zero() const;

        void CopyData(const void *srcData, size_t srcSize = 0, size_t offset = 0);

        void CopyBuffer(Buffer *srcBuffer, size_t srcSize = 0);

        void Copy(MemoryRange *ranges, uint32_t count, bool isPersistent);

        void Flush(size_t offset = 0, size_t flushSize = 0) const;

        size_t Size();

        void *Data();

    private:
        size_t size;
        void *data;
        AllocationHandle allocation;
        std::string name;
    };
}