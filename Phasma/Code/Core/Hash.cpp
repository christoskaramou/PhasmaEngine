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

// #include "Core/Hash.h"
// #include "Renderer/Descriptor.h"
// #include "Renderer/RenderPass.h"
// #include "Renderer/Pipeline.h"

// namespace pe
// {
//     Hash DescriptorBindingsHash(const std::vector<DescriptorBinding> &bindings)
//     {
//         Hash hash;

//         for (const auto &binding : bindings)
//         {
//             hash.Combine(binding.binding);
//             hash.Combine(binding.descriptorType);
//             hash.Combine(binding.descriptorCount);
//             hash.Combine((uint32_t)binding.stage);
//             if (binding.pImmutableSamplers)
//                 hash.Combine(reinterpret_cast<uintptr_t>(binding.pImmutableSamplers));
//         }

//         return hash;
//     }

//     template <>
//     void Hashable<std::vector<DescriptorBinding>>::CreateHash(std::vector<DescriptorBinding> &&bindings)
//     {
//         m_hash = DescriptorBindingsHash(bindings);
//     }

//     template <>
//     void Hashable<std::vector<DescriptorBinding> &&>::CreateHash(std::vector<DescriptorBinding> &&bindings)
//     {
//         m_hash = DescriptorBindingsHash(bindings);
//     }

//     template <>
//     void Hashable<std::vector<DescriptorBinding> &>::CreateHash(std::vector<DescriptorBinding> &bindings)
//     {
//         m_hash = DescriptorBindingsHash(bindings);
//     }

//     Hash AttachmentsHash(const std::vector<Attachment> &attachments)
//     {
//         Hash hash = {};
//         for (const auto &attachment : attachments)
//         {
//             hash.Combine(attachment.format);
//             hash.Combine(attachment.samples);
//             hash.Combine(attachment.loadOp);
//             hash.Combine(attachment.storeOp);
//             hash.Combine(attachment.stencilLoadOp);
//             hash.Combine(attachment.stencilStoreOp);
//             hash.Combine(attachment.initialLayout);
//             hash.Combine(attachment.finalLayout);
//         }

//         return hash;
//     }

//     template <>
//     void Hashable<Attachment &>::CreateHash(Attachment &attachment)
//     {
//         m_hash = AttachmentsHash({attachment});
//     }

//     template <>
//     void Hashable<Attachment>::CreateHash(Attachment &&attachment)
//     {
//         m_hash = AttachmentsHash({attachment});
//     }

//     template <>
//     void Hashable<Attachment &&>::CreateHash(Attachment &&attachment)
//     {
//         m_hash = AttachmentsHash({attachment});
//     }

//     template <>
//     void Hashable<std::vector<Attachment> &>::CreateHash(std::vector<Attachment> &attachments)
//     {

//         m_hash = AttachmentsHash(attachments);
//     }

//     template <>
//     void Hashable<std::vector<Attachment>>::CreateHash(std::vector<Attachment> &&attachments)
//     {

//         m_hash = AttachmentsHash(attachments);
//     }

//     template <>
//     void Hashable<std::vector<Attachment> &&>::CreateHash(std::vector<Attachment> &&attachments)
//     {
//         m_hash = AttachmentsHash(attachments);
//     }

//     Hash PipelineCreateInfoHash(const PipelineCreateInfo &pipelineInfo)
//     {
//         Hash hash;

//         if (pipelineInfo.pVertShader)
//             hash.Combine(pipelineInfo.pVertShader->GetCache().GetHash());

//         if (pipelineInfo.pFragShader)
//             hash.Combine(pipelineInfo.pFragShader->GetCache().GetHash());

//         if (pipelineInfo.pCompShader)
//             hash.Combine(pipelineInfo.pCompShader->GetCache().GetHash());

//         for (auto &binding : pipelineInfo.vertexInputBindingDescriptions)
//         {
//             hash.Combine(binding.binding);
//             hash.Combine(binding.stride);
//             hash.Combine(binding.inputRate);
//         }

//         for (auto &attribute : pipelineInfo.vertexInputAttributeDescriptions)
//         {
//             hash.Combine(attribute.location);
//             hash.Combine(attribute.binding);
//             hash.Combine(attribute.format);
//             hash.Combine(attribute.offset);
//         }

//         hash.Combine(pipelineInfo.width);
//         hash.Combine(pipelineInfo.height);
//         hash.Combine(pipelineInfo.cullMode);

//         for (auto &attachment : pipelineInfo.colorBlendAttachments)
//         {
//             hash.Combine(attachment.blendEnable);
//             hash.Combine(attachment.srcColorBlendFactor);
//             hash.Combine(attachment.dstColorBlendFactor);
//             hash.Combine(attachment.colorBlendOp);
//             hash.Combine(attachment.srcAlphaBlendFactor);
//             hash.Combine(attachment.dstAlphaBlendFactor);
//             hash.Combine(attachment.alphaBlendOp);
//             hash.Combine(attachment.colorWriteMask);
//         }

//         for (auto &dynamic : pipelineInfo.dynamicStates)
//             hash.Combine(dynamic);

//         hash.Combine(pipelineInfo.pushConstantStage);
//         hash.Combine(pipelineInfo.pushConstantSize);

//         for (auto &layout : pipelineInfo.descriptorSetLayouts)
//             hash.Combine(reinterpret_cast<uintptr_t>(layout));

//         if (pipelineInfo.renderPass)
//             hash.Combine(reinterpret_cast<uintptr_t>(pipelineInfo.renderPass));

//         VkPipelineCache pipelineCache = pipelineInfo.pipelineCache;
//         if (pipelineCache)
//             hash.Combine(reinterpret_cast<uintptr_t>(pipelineCache));

//         return hash;
//     }

//     template <>
//     void Hashable<PipelineCreateInfo &>::CreateHash(PipelineCreateInfo &pipelineInfo)
//     {
//         m_hash = PipelineCreateInfoHash(pipelineInfo);
//     }

//     template <>
//     void Hashable<PipelineCreateInfo>::CreateHash(PipelineCreateInfo &&pipelineInfo)
//     {
//         m_hash = PipelineCreateInfoHash(pipelineInfo);
//     }

//     template <>
//     void Hashable<PipelineCreateInfo &&>::CreateHash(PipelineCreateInfo &&pipelineInfo)
//     {
//         m_hash = PipelineCreateInfoHash(pipelineInfo);
//     }
// }