#include "PipelineCreation.h"

#include "Device.h"
#include "FormatMap.h"
#include "PipelineLayout.h"

#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"

#include <exception>
#include <utility>

namespace pwgpu
{
    namespace
    {
        PipelineCreationResult InternalError(std::string message)
        {
            PipelineCreationResult result;
            result.errorType = WGPUErrorType_Internal;
            result.message = std::move(message);
            return result;
        }

        PeGraphicsApi DeviceApi(WGPUDeviceImpl *device)
        {
            return (device && device->rhi) ? device->rhi->GetApi() : pe::RHII.GetApi();
        }

        PipelineCreationResult BackendUnsupported(const char *operation, WGPUDeviceImpl *device)
        {
            return InternalError(std::string(operation) + ": " +
                                 PeGraphicsApiName(DeviceApi(device)) +
                                 " backend pipeline creation is not implemented");
        }

        vk::ShaderModule CreateShaderModule(vk::Device vkDev, const std::vector<uint32_t> &spirv)
        {
            vk::ShaderModuleCreateInfo ci{};
            ci.codeSize = spirv.size() * sizeof(uint32_t);
            ci.pCode = spirv.data();
            return vkDev.createShaderModule(ci);
        }

        WGPUCullMode ResolveCullMode(WGPUCullMode mode)
        {
            return mode == WGPUCullMode(0) ? WGPUCullMode_None : mode;
        }

        WGPUFrontFace ResolveFrontFace(WGPUFrontFace face)
        {
            return face == WGPUFrontFace(0) ? WGPUFrontFace_CCW : face;
        }

        WGPUBlendOperation ResolveBlendOperation(WGPUBlendOperation op)
        {
            return op == WGPUBlendOperation(0) ? WGPUBlendOperation_Add : op;
        }

        WGPUBlendFactor ResolveBlendSrcFactor(WGPUBlendFactor factor)
        {
            return factor == WGPUBlendFactor(0) ? WGPUBlendFactor_One : factor;
        }

        WGPUBlendFactor ResolveBlendDstFactor(WGPUBlendFactor factor)
        {
            return factor == WGPUBlendFactor(0) ? WGPUBlendFactor_Zero : factor;
        }

        vk::PipelineColorBlendAttachmentState ToVkBlendAttachment(const WGPUColorTargetState &target)
        {
            vk::PipelineColorBlendAttachmentState attachment{};
            attachment.colorWriteMask =
                vk::ColorComponentFlags(static_cast<uint32_t>(target.writeMask));

            if (!target.blend)
                return attachment;

            attachment.blendEnable = VK_TRUE;
            attachment.colorBlendOp = ToVkBlendOp(ResolveBlendOperation(target.blend->color.operation));
            attachment.srcColorBlendFactor =
                ToVkBlendFactor(ResolveBlendSrcFactor(target.blend->color.srcFactor));
            attachment.dstColorBlendFactor =
                ToVkBlendFactor(ResolveBlendDstFactor(target.blend->color.dstFactor));
            attachment.alphaBlendOp = ToVkBlendOp(ResolveBlendOperation(target.blend->alpha.operation));
            attachment.srcAlphaBlendFactor =
                ToVkBlendFactor(ResolveBlendSrcFactor(target.blend->alpha.srcFactor));
            attachment.dstAlphaBlendFactor =
                ToVkBlendFactor(ResolveBlendDstFactor(target.blend->alpha.dstFactor));
            return attachment;
        }
    } // namespace

    PipelineCreationResult CreateWebGPUComputePipelineBackend(const ComputePipelineBackendDesc &desc)
    {
        if (!desc.device || !desc.layout || !desc.spirv || !desc.entryPoint ||
            desc.layout->backendLayout == 0)
        {
            return InternalError("createComputePipeline: invalid backend creation descriptor");
        }

        switch (DeviceApi(desc.device))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            auto vkDev = pe::VulkanRhi::Device();
            vk::ShaderModule shaderModule{};
            try
            {
                shaderModule = CreateShaderModule(vkDev, *desc.spirv);
            }
            catch (...)
            {
                return InternalError("createComputePipeline: failed to create VkShaderModule");
            }

            vk::ComputePipelineCreateInfo cpci{};
            cpci.stage.stage = vk::ShaderStageFlagBits::eCompute;
            cpci.stage.module = shaderModule;
            cpci.stage.pName = desc.entryPoint;
            cpci.layout =
                vk::PipelineLayout{PeFromBackendHandle<VkPipelineLayout>(desc.layout->backendLayout)};

            vk::Pipeline pipeline{};
            try
            {
                auto result = vkDev.createComputePipeline(nullptr, cpci);
                if (result.result != vk::Result::eSuccess)
                {
                    vkDev.destroyShaderModule(shaderModule);
                    return InternalError("createComputePipeline: vkCreateComputePipelines failed");
                }
                pipeline = result.value;
            }
            catch (...)
            {
                vkDev.destroyShaderModule(shaderModule);
                return InternalError("createComputePipeline: vkCreateComputePipelines threw");
            }

            vkDev.destroyShaderModule(shaderModule);

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(static_cast<VkPipeline>(pipeline));
            return result;
        }
        default:
            return BackendUnsupported("createComputePipeline", desc.device);
        }
    }

    PipelineCreationResult CreateWebGPURenderPipelineBackend(const RenderPipelineBackendDesc &desc)
    {
        if (!desc.device || !desc.layout || !desc.descriptor || !desc.vertexSpirv ||
            !desc.vertexEntryPoint || desc.layout->backendLayout == 0)
        {
            return InternalError("createRenderPipeline: invalid backend creation descriptor");
        }
        if (desc.hasFragment && (!desc.fragmentSpirv || !desc.fragmentEntryPoint))
            return InternalError("createRenderPipeline: invalid fragment backend creation descriptor");

        switch (DeviceApi(desc.device))
        {
        case PE_GRAPHICS_API_VULKAN:
        {
            auto vkDev = pe::VulkanRhi::Device();

            std::vector<vk::PipelineShaderStageCreateInfo> stages;
            std::vector<vk::ShaderModule> tempModules;
            stages.reserve(2);

            vk::ShaderModule vkVertModule{};
            try
            {
                vkVertModule = CreateShaderModule(vkDev, *desc.vertexSpirv);
            }
            catch (...)
            {
                return InternalError("createRenderPipeline: failed to create vertex VkShaderModule");
            }
            tempModules.push_back(vkVertModule);

            vk::PipelineShaderStageCreateInfo vertStage{};
            vertStage.stage = vk::ShaderStageFlagBits::eVertex;
            vertStage.module = vkVertModule;
            vertStage.pName = desc.vertexEntryPoint;
            stages.push_back(vertStage);

            if (desc.hasFragment)
            {
                vk::ShaderModule vkFragModule{};
                try
                {
                    vkFragModule = CreateShaderModule(vkDev, *desc.fragmentSpirv);
                }
                catch (...)
                {
                    for (auto m : tempModules)
                        vkDev.destroyShaderModule(m);
                    return InternalError("createRenderPipeline: failed to create fragment VkShaderModule");
                }
                tempModules.push_back(vkFragModule);

                vk::PipelineShaderStageCreateInfo fragStage{};
                fragStage.stage = vk::ShaderStageFlagBits::eFragment;
                fragStage.module = vkFragModule;
                fragStage.pName = desc.fragmentEntryPoint;
                stages.push_back(fragStage);
            }

            const WGPURenderPipelineDescriptor &descriptor = *desc.descriptor;

            std::vector<vk::VertexInputBindingDescription> vertBindings;
            std::vector<vk::VertexInputAttributeDescription> vertAttrs;

            for (uint32_t slot = 0; slot < descriptor.vertex.bufferCount; ++slot)
            {
                const auto &vbuf = descriptor.vertex.buffers[slot];
                if (vbuf.attributeCount == 0 && vbuf.arrayStride == 0)
                    continue;

                vk::VertexInputBindingDescription binding{};
                binding.binding = slot;
                binding.stride = static_cast<uint32_t>(vbuf.arrayStride);
                binding.inputRate = (vbuf.stepMode == WGPUVertexStepMode_Instance)
                                        ? vk::VertexInputRate::eInstance
                                        : vk::VertexInputRate::eVertex;
                vertBindings.push_back(binding);

                for (size_t a = 0; a < vbuf.attributeCount; ++a)
                {
                    vk::VertexInputAttributeDescription attr{};
                    attr.location = vbuf.attributes[a].shaderLocation;
                    attr.binding = slot;
                    attr.format =
                        static_cast<vk::Format>(VertexFormatToVk(vbuf.attributes[a].format));
                    attr.offset = static_cast<uint32_t>(vbuf.attributes[a].offset);
                    vertAttrs.push_back(attr);
                }
            }

            vk::PipelineVertexInputStateCreateInfo vertexInputState{};
            vertexInputState.vertexBindingDescriptionCount =
                static_cast<uint32_t>(vertBindings.size());
            vertexInputState.pVertexBindingDescriptions = vertBindings.data();
            vertexInputState.vertexAttributeDescriptionCount =
                static_cast<uint32_t>(vertAttrs.size());
            vertexInputState.pVertexAttributeDescriptions = vertAttrs.data();

            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = ToVkTopology(desc.primitiveTopology);
            inputAssembly.primitiveRestartEnable =
                IsStripTopology(desc.primitiveTopology) ? VK_TRUE : VK_FALSE;

            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            vk::PipelineRasterizationStateCreateInfo rasterState{};
            rasterState.depthClampEnable = VK_FALSE;
            rasterState.rasterizerDiscardEnable =
                (!desc.hasFragment && !desc.hasDepthStencil) ? VK_TRUE : VK_FALSE;
            rasterState.polygonMode = vk::PolygonMode::eFill;
            rasterState.cullMode = ToVkCullMode(ResolveCullMode(descriptor.primitive.cullMode));
            rasterState.frontFace =
                (ResolveFrontFace(descriptor.primitive.frontFace) == WGPUFrontFace_CW)
                    ? vk::FrontFace::eClockwise
                    : vk::FrontFace::eCounterClockwise;
            rasterState.lineWidth = 1.0f;

            if (desc.hasDepthStencil)
            {
                const auto &ds = *descriptor.depthStencil;
                bool hasBias = (ds.depthBias != 0 ||
                                ds.depthBiasSlopeScale != 0.0f ||
                                ds.depthBiasClamp != 0.0f);
                rasterState.depthBiasEnable = hasBias ? VK_TRUE : VK_FALSE;
                rasterState.depthBiasConstantFactor = static_cast<float>(ds.depthBias);
                rasterState.depthBiasSlopeFactor = ds.depthBiasSlopeScale;
                rasterState.depthBiasClamp = ds.depthBiasClamp;
            }

            vk::PipelineRasterizationDepthClipStateCreateInfoEXT depthClipState{};
            const bool needsUnclippedDepth = descriptor.primitive.unclippedDepth;
            const bool needsFragDepthClamp =
                desc.fragmentWritesFragDepth &&
                desc.device->supportsDepthClamp &&
                desc.device->supportsDepthClipEnable;
            if (needsUnclippedDepth || needsFragDepthClamp)
            {
                depthClipState.depthClipEnable = needsUnclippedDepth ? VK_FALSE : VK_TRUE;
                depthClipState.pNext = rasterState.pNext;
                rasterState.pNext = &depthClipState;
                rasterState.depthClampEnable = VK_TRUE;
            }

            vk::PipelineMultisampleStateCreateInfo multisampleState{};
            multisampleState.rasterizationSamples = ToVkSampleCount(desc.sampleCount);
            uint32_t msMask = descriptor.multisample.mask;
            multisampleState.pSampleMask = &msMask;
            multisampleState.alphaToCoverageEnable =
                descriptor.multisample.alphaToCoverageEnabled ? VK_TRUE : VK_FALSE;

            vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
            if (desc.hasDepthStencil)
            {
                const auto &ds = *descriptor.depthStencil;
                bool hasDepthAspect = HasDepthAspect(ds.format);

                depthStencilState.depthTestEnable = hasDepthAspect ? VK_TRUE : VK_FALSE;
                depthStencilState.depthWriteEnable =
                    (ds.depthWriteEnabled == WGPUOptionalBool_True) ? VK_TRUE : VK_FALSE;

                auto dc = ds.depthCompare;
                if (dc == WGPUCompareFunction_Undefined)
                    dc = WGPUCompareFunction_Always;
                depthStencilState.depthCompareOp = ToVkCompareOp(dc);

                bool hasStencilAspect = HasStencilAspect(ds.format);
                depthStencilState.stencilTestEnable = hasStencilAspect ? VK_TRUE : VK_FALSE;

                auto mapFace = [](const WGPUStencilFaceState &face,
                                  uint32_t readMask,
                                  uint32_t writeMask) -> vk::StencilOpState
                {
                    vk::StencilOpState s{};
                    auto cmp = face.compare;
                    if (cmp == WGPUCompareFunction_Undefined || cmp == WGPUCompareFunction(0))
                        cmp = WGPUCompareFunction_Always;
                    s.compareOp = ToVkCompareOp(cmp);

                    auto fail = face.failOp;
                    if (fail == WGPUStencilOperation(0))
                        fail = WGPUStencilOperation_Keep;
                    s.failOp = ToVkStencilOp(fail);

                    auto dfail = face.depthFailOp;
                    if (dfail == WGPUStencilOperation(0))
                        dfail = WGPUStencilOperation_Keep;
                    s.depthFailOp = ToVkStencilOp(dfail);

                    auto pass = face.passOp;
                    if (pass == WGPUStencilOperation(0))
                        pass = WGPUStencilOperation_Keep;
                    s.passOp = ToVkStencilOp(pass);

                    s.compareMask = readMask;
                    s.writeMask = writeMask;
                    s.reference = 0;
                    return s;
                };

                uint32_t stencilRead = ds.stencilReadMask ? ds.stencilReadMask : 0xFFFFFFFFu;
                uint32_t stencilWrite = ds.stencilWriteMask ? ds.stencilWriteMask : 0xFFFFFFFFu;
                depthStencilState.front = mapFace(ds.stencilFront, stencilRead, stencilWrite);
                depthStencilState.back = mapFace(ds.stencilBack, stencilRead, stencilWrite);
            }

            std::vector<vk::PipelineColorBlendAttachmentState> blendAttachments;
            std::vector<vk::Format> colorVkFormats;
            if (desc.hasFragment)
            {
                blendAttachments.reserve(descriptor.fragment->targetCount);
                colorVkFormats.reserve(descriptor.fragment->targetCount);
                for (size_t i = 0; i < descriptor.fragment->targetCount; ++i)
                {
                    const auto &target = descriptor.fragment->targets[i];
                    if (target.format == WGPUTextureFormat_Undefined)
                    {
                        blendAttachments.emplace_back();
                        colorVkFormats.push_back(vk::Format::eUndefined);
                    }
                    else
                    {
                        blendAttachments.push_back(ToVkBlendAttachment(target));
                        colorVkFormats.push_back(static_cast<vk::Format>(ToVkFormat(target.format)));
                    }
                }
            }

            vk::PipelineColorBlendStateCreateInfo colorBlendState{};
            colorBlendState.logicOpEnable = VK_FALSE;
            colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
            colorBlendState.pAttachments = blendAttachments.data();

            std::vector<vk::DynamicState> dynStates = {
                vk::DynamicState::eViewport,
                vk::DynamicState::eScissor,
            };
            if (desc.hasDepthStencil && HasStencilAspect(descriptor.depthStencil->format))
                dynStates.push_back(vk::DynamicState::eStencilReference);
            dynStates.push_back(vk::DynamicState::eBlendConstants);

            vk::PipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
            dynamicState.pDynamicStates = dynStates.data();

            vk::PipelineRenderingCreateInfo renderingInfo{};
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorVkFormats.size());
            renderingInfo.pColorAttachmentFormats = colorVkFormats.data();
            if (desc.hasDepthStencil)
            {
                vk::Format dsVkFmt = static_cast<vk::Format>(
                    ResolveVkTextureFormat(descriptor.depthStencil->format,
                                           desc.device->resolvedDepth24Plus,
                                           desc.device->resolvedDepth24PlusStencil8,
                                           desc.device->resolvedStencil8));
                if (HasDepthAspect(descriptor.depthStencil->format))
                    renderingInfo.depthAttachmentFormat = dsVkFmt;
                if (HasStencilAspect(descriptor.depthStencil->format))
                    renderingInfo.stencilAttachmentFormat = dsVkFmt;
            }

            vk::GraphicsPipelineCreateInfo pipeInfo{};
            pipeInfo.pNext = &renderingInfo;
            pipeInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipeInfo.pStages = stages.data();
            pipeInfo.pVertexInputState = &vertexInputState;
            pipeInfo.pInputAssemblyState = &inputAssembly;
            pipeInfo.pViewportState = &viewportState;
            pipeInfo.pRasterizationState = &rasterState;
            pipeInfo.pMultisampleState = &multisampleState;
            pipeInfo.pDepthStencilState = desc.hasDepthStencil ? &depthStencilState : nullptr;
            pipeInfo.pColorBlendState = &colorBlendState;
            pipeInfo.pDynamicState = &dynamicState;
            pipeInfo.layout =
                vk::PipelineLayout{PeFromBackendHandle<VkPipelineLayout>(desc.layout->backendLayout)};
            pipeInfo.renderPass = nullptr;
            pipeInfo.subpass = 0;
            pipeInfo.basePipelineHandle = nullptr;
            pipeInfo.basePipelineIndex = -1;

            vk::Pipeline pipeline{};
            try
            {
                auto result = vkDev.createGraphicsPipeline(nullptr, pipeInfo);
                if (result.result != vk::Result::eSuccess)
                {
                    for (auto m : tempModules)
                        vkDev.destroyShaderModule(m);
                    return InternalError("createRenderPipeline: vkCreateGraphicsPipelines failed");
                }
                pipeline = result.value;
            }
            catch (const std::exception &e)
            {
                for (auto m : tempModules)
                    vkDev.destroyShaderModule(m);
                return InternalError(std::string("createRenderPipeline: vkCreateGraphicsPipelines threw: ") +
                                     e.what());
            }

            for (auto m : tempModules)
                vkDev.destroyShaderModule(m);

            PipelineCreationResult result;
            result.backendPipeline = PeToBackendHandle(static_cast<VkPipeline>(pipeline));
            return result;
        }
        default:
            return BackendUnsupported("createRenderPipeline", desc.device);
        }
    }

    void DestroyWebGPUPipelineBackend(WGPUDeviceImpl *device, PeBackendHandle backendPipeline)
    {
        if (!device || !device->rhi || backendPipeline == 0)
            return;

        switch (DeviceApi(device))
        {
        case PE_GRAPHICS_API_VULKAN:
            pe::VulkanRhi::Device().destroyPipeline(
                vk::Pipeline{PeFromBackendHandle<VkPipeline>(backendPipeline)});
            break;
        default:
            PE_ERROR("[WebGPU] %s backend pipeline destruction is not implemented",
                     PeGraphicsApiName(DeviceApi(device)));
            break;
        }
    }
} // namespace pwgpu
