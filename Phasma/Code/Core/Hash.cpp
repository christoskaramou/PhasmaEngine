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

#include "Core/Hash.h"
#include "Renderer/Descriptor.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Pipeline.h"

namespace pe
{
    void Hashable<std::vector<DescriptorBinding> &>::CreateHash(std::vector<DescriptorBinding> &bindings)
    {
        const std::vector<DescriptorBinding> &cbindings = bindings;
        Hashable<const std::vector<DescriptorBinding> &> hashable;
        hashable.CreateHash(cbindings);
        m_hash = hashable.GetHash();
    }

    template <>
    void Hashable<const std::vector<DescriptorBinding> &>::CreateHash(const std::vector<DescriptorBinding> &bindings)
    {
        m_hash = {};
        for (const auto &binding : bindings)
        {
            m_hash.Combine(binding.binding);
            m_hash.Combine(binding.descriptorType);
            m_hash.Combine(binding.descriptorCount);
            m_hash.Combine(binding.stageFlags);
            if (binding.pImmutableSamplers)
                m_hash.Combine(binding.pImmutableSamplers);
        }
    }

    void Hashable<Attachment &>::CreateHash(Attachment &attachment)
    {
        const std::vector<Attachment> cattachments = {attachment};
        Hashable<const std::vector<Attachment> &> hashable;
        hashable.CreateHash(cattachments);
        m_hash = hashable.GetHash();
    }

    void Hashable<const Attachment &>::CreateHash(const Attachment &attachment)
    {
        const std::vector<Attachment> cattachments = {attachment};
        Hashable<const std::vector<Attachment> &> hashable;
        hashable.CreateHash(cattachments);
        m_hash = hashable.GetHash();
    }

    void Hashable<std::vector<Attachment> &>::CreateHash(std::vector<Attachment> &attachments)
    {
        const std::vector<Attachment> &cattachments = attachments;
        Hashable<const std::vector<Attachment> &> hashable;
        hashable.CreateHash(cattachments);
        m_hash = hashable.GetHash();
    }

    template <>
    void Hashable<const std::vector<Attachment> &>::CreateHash(const std::vector<Attachment> &attachments)
    {
        m_hash = {};
        for (const Attachment &attachment : attachments)
        {
            m_hash.Combine(attachment.flags);
            m_hash.Combine(attachment.format);
            m_hash.Combine(attachment.samples);
            m_hash.Combine(attachment.loadOp);
            m_hash.Combine(attachment.storeOp);
            m_hash.Combine(attachment.stencilLoadOp);
            m_hash.Combine(attachment.stencilStoreOp);
            m_hash.Combine(attachment.initialLayout);
            m_hash.Combine(attachment.finalLayout);
        }
    }

    void Hashable<PipelineCreateInfo &>::CreateHash(PipelineCreateInfo &pipelineInfo)
    {
        const PipelineCreateInfo &info = pipelineInfo;
        Hashable<const PipelineCreateInfo &> hashable;
        hashable.CreateHash(info);
        m_hash = hashable.GetHash();
    }

    template <>
    void Hashable<const PipelineCreateInfo &>::CreateHash(const PipelineCreateInfo &pipelineInfo)
    {
        if (pipelineInfo.pVertShader)
            m_hash.Combine(pipelineInfo.pVertShader->GetCache().GetHash());

        if (pipelineInfo.pFragShader)
            m_hash.Combine(pipelineInfo.pFragShader->GetCache().GetHash());

        if (pipelineInfo.pCompShader)
            m_hash.Combine(pipelineInfo.pCompShader->GetCache().GetHash());

        for (auto &binding : pipelineInfo.vertexInputBindingDescriptions)
        {
            m_hash.Combine(binding.binding);
            m_hash.Combine(binding.stride);
            m_hash.Combine(binding.inputRate);
        }

        for (auto &attribute : pipelineInfo.vertexInputAttributeDescriptions)
        {
            m_hash.Combine(attribute.location);
            m_hash.Combine(attribute.binding);
            m_hash.Combine(attribute.format);
            m_hash.Combine(attribute.offset);
        }

        m_hash.Combine(pipelineInfo.width);
        m_hash.Combine(pipelineInfo.height);
        m_hash.Combine(pipelineInfo.cullMode);

        for (auto &attachment : pipelineInfo.colorBlendAttachments)
        {
            m_hash.Combine(attachment.blendEnable);
            m_hash.Combine(attachment.srcColorBlendFactor);
            m_hash.Combine(attachment.dstColorBlendFactor);
            m_hash.Combine(attachment.colorBlendOp);
            m_hash.Combine(attachment.srcAlphaBlendFactor);
            m_hash.Combine(attachment.dstAlphaBlendFactor);
            m_hash.Combine(attachment.alphaBlendOp);
            m_hash.Combine(attachment.colorWriteMask);
        }

        for (auto &dynamic : pipelineInfo.dynamicStates)
            m_hash.Combine(dynamic);

        m_hash.Combine(pipelineInfo.pushConstantStage);
        m_hash.Combine(pipelineInfo.pushConstantSize);

        for (auto &layout : pipelineInfo.descriptorSetLayouts)
            m_hash.Combine(layout);

        if (pipelineInfo.renderPass)
            m_hash.Combine(pipelineInfo.renderPass);

        VkPipelineCache pipelineCache = pipelineInfo.pipelineCache;
        if (pipelineCache)
            m_hash.Combine(pipelineCache);
    }
}