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
    class DescriptorPool : public IHandle<DescriptorPool, DescriptorPoolHandle>
    {
    public:
        DescriptorPool(uint32_t maxDescriptorSets, const std::string &name = {});

        ~DescriptorPool();
    };

    class Image;
    class Buffer;

    struct DescriptorBindingInfo
    {
        uint32_t binding = 0;
        Buffer *pBuffer = nullptr;
        Image *pImage = nullptr;
        ImageLayout imageLayout = ImageLayout::Undefined;
        DescriptorType type = DescriptorType::CombinedImageSampler;
    };

    struct DescriptorInfo
    {
        uint32_t count = 0;
        DescriptorBindingInfo *bindingInfos = nullptr;
        ShaderStage stage = ShaderStage::VertexBit;
    };

    class DescriptorLayout : public IHandle<DescriptorLayout, DescriptorSetLayoutHandle>
    {
    public:
        DescriptorLayout(DescriptorInfo *info, const std::string &name = {});

        ~DescriptorLayout();

        uint32_t GetVariableCount() { return m_variableCount; }

    private:
        uint32_t m_variableCount;
    };

    class Descriptor : public IHandle<Descriptor, DescriptorSetHandle>
    {
    public:
        Descriptor(DescriptorInfo *info, const std::string &name = {});

        ~Descriptor();

        void UpdateDescriptor(DescriptorInfo *info);

        DescriptorLayout *GetLayout() const { return m_layout; }

    private:
        DescriptorLayout *m_layout;
    };

}
