#include "API/WebGPU/WebGpuPipelineImpl.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "API/WebGPU/WebGpuDescriptorImpl.h"
#include "API/WebGPU/WebGpuFormat.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "API/WebGPU/WebGpuShaderImpl.h"

namespace pe
{
    namespace
    {
        // SpirvReflection gives the per-draw ID binding 1; it advances per instance (the engine's draw-ID buffer).
        constexpr const char *kDrawIdInput = "in.var.PE_DRAW_ID";

        WGPUVertexFormat ToVertexFormat(::PeFormat format, const std::string &pipeline)
        {
            switch (format)
            {
            case PE_FORMAT_R32_SFLOAT:
                return WGPUVertexFormat_Float32;
            case PE_FORMAT_R32G32_SFLOAT:
                return WGPUVertexFormat_Float32x2;
            case PE_FORMAT_R32G32B32_SFLOAT:
                return WGPUVertexFormat_Float32x3;
            case PE_FORMAT_R32G32B32A32_SFLOAT:
                return WGPUVertexFormat_Float32x4;
            case PE_FORMAT_R32_UINT:
                return WGPUVertexFormat_Uint32;
            case PE_FORMAT_R32G32_UINT:
                return WGPUVertexFormat_Uint32x2;
            case PE_FORMAT_R32G32B32_UINT:
                return WGPUVertexFormat_Uint32x3;
            case PE_FORMAT_R32G32B32A32_UINT:
                return WGPUVertexFormat_Uint32x4;
            case PE_FORMAT_R32_SINT:
                return WGPUVertexFormat_Sint32;
            case PE_FORMAT_R32G32_SINT:
                return WGPUVertexFormat_Sint32x2;
            case PE_FORMAT_R32G32B32_SINT:
                return WGPUVertexFormat_Sint32x3;
            case PE_FORMAT_R32G32B32A32_SINT:
                return WGPUVertexFormat_Sint32x4;
            default:
                PE_ERROR("WebGPU pipeline '%s': vertex format %u has no WebGPU vertex format",
                         pipeline.c_str(),
                         static_cast<uint32_t>(format));
                return WGPUVertexFormat_Float32;
            }
        }

        WGPUPrimitiveTopology ToTopology(PeTopology topology, const std::string &pipeline)
        {
            switch (topology)
            {
            case PE_TOPOLOGY_POINT_LIST:
                return WGPUPrimitiveTopology_PointList;
            case PE_TOPOLOGY_LINE_LIST:
                return WGPUPrimitiveTopology_LineList;
            case PE_TOPOLOGY_LINE_STRIP:
                return WGPUPrimitiveTopology_LineStrip;
            case PE_TOPOLOGY_TRIANGLE_LIST:
                return WGPUPrimitiveTopology_TriangleList;
            case PE_TOPOLOGY_TRIANGLE_STRIP:
                return WGPUPrimitiveTopology_TriangleStrip;
            default:
                PE_ERROR("WebGPU pipeline '%s': topology %u (fans, patches) does not exist in WebGPU",
                         pipeline.c_str(),
                         static_cast<uint32_t>(topology));
                return WGPUPrimitiveTopology_TriangleList;
            }
        }

        WGPUCullMode ToCullMode(PeCullMode mode, const std::string &pipeline)
        {
            switch (mode)
            {
            case PE_CULL_MODE_NONE:
                return WGPUCullMode_None;
            case PE_CULL_MODE_FRONT:
                return WGPUCullMode_Front;
            case PE_CULL_MODE_BACK:
                return WGPUCullMode_Back;
            default:
                PE_ERROR("WebGPU pipeline '%s': WebGPU cannot cull both faces", pipeline.c_str());
                return WGPUCullMode_None;
            }
        }

        // PeCompareOp runs Never..Always in WebGPU's order, one below it.
        WGPUCompareFunction ToCompare(PeCompareOp op)
        {
            return static_cast<WGPUCompareFunction>(static_cast<uint32_t>(WGPUCompareFunction_Never) + op);
        }

        WGPUStencilOperation ToStencilOp(PeStencilOp op)
        {
            switch (op)
            {
            case PE_STENCIL_OP_ZERO:
                return WGPUStencilOperation_Zero;
            case PE_STENCIL_OP_REPLACE:
                return WGPUStencilOperation_Replace;
            case PE_STENCIL_OP_INCREMENT_AND_CLAMP:
                return WGPUStencilOperation_IncrementClamp;
            case PE_STENCIL_OP_DECREMENT_AND_CLAMP:
                return WGPUStencilOperation_DecrementClamp;
            case PE_STENCIL_OP_INVERT:
                return WGPUStencilOperation_Invert;
            case PE_STENCIL_OP_INCREMENT_AND_WRAP:
                return WGPUStencilOperation_IncrementWrap;
            case PE_STENCIL_OP_DECREMENT_AND_WRAP:
                return WGPUStencilOperation_DecrementWrap;
            default:
                return WGPUStencilOperation_Keep;
            }
        }

        WGPUBlendFactor ToBlendFactor(PeBlendFactor factor)
        {
            switch (factor)
            {
            case PE_BLEND_FACTOR_ZERO:
                return WGPUBlendFactor_Zero;
            case PE_BLEND_FACTOR_SRC_ALPHA:
                return WGPUBlendFactor_SrcAlpha;
            case PE_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
                return WGPUBlendFactor_OneMinusSrcAlpha;
            case PE_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
                return WGPUBlendFactor_OneMinusSrc;
            default:
                return WGPUBlendFactor_One;
            }
        }

        // PeBlendOp runs Add..Max in WebGPU's order, one below it.
        WGPUBlendOperation ToBlendOp(PeBlendOp op)
        {
            return static_cast<WGPUBlendOperation>(static_cast<uint32_t>(WGPUBlendOperation_Add) + op);
        }

        // Vertex buffers from the vertex shader's reflected inputs, each binding packed in location order (as DX12).
        struct VertexInput
        {
            std::vector<std::vector<WGPUVertexAttribute>> attributes; // per binding
            std::vector<WGPUVertexBufferLayout> buffers;
        };

        VertexInput ReflectVertexInput(Shader *vertex, const std::string &pipeline)
        {
            VertexInput out;
            std::vector<uint64_t> strides;
            std::vector<bool> instanced;
            for (const ShaderInOutDesc &input : vertex->GetReflection().GetInputs())
            {
                if (input.binding == INT32_MIN)
                    continue; // a built-in such as SV_VertexID

                const size_t binding = static_cast<size_t>(input.binding);
                if (binding >= out.attributes.size())
                {
                    out.attributes.resize(binding + 1);
                    strides.resize(binding + 1, 0);
                    instanced.resize(binding + 1, false);
                }

                WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
                attribute.format = ToVertexFormat(input.format, pipeline);
                attribute.offset = strides[binding];
                attribute.shaderLocation = static_cast<uint32_t>(input.location);
                out.attributes[binding].push_back(attribute);
                strides[binding] += input.size;
                instanced[binding] = instanced[binding] || input.name == kDrawIdInput;
            }

            out.buffers.resize(out.attributes.size(), WGPU_VERTEX_BUFFER_LAYOUT_INIT);
            for (size_t i = 0; i < out.buffers.size(); ++i)
            {
                // An unused slot keeps the init layout (no step mode, no attributes), which is WebGPU's null slot:
                // PE_DRAW_ID sits at binding 1 even when binding 0 has nothing.
                if (out.attributes[i].empty())
                    continue;

                WGPUVertexBufferLayout &layout = out.buffers[i];
                layout.stepMode = instanced[i] ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
                layout.arrayStride = strides[i];
                layout.attributeCount = out.attributes[i].size();
                layout.attributes = out.attributes[i].data();
            }

            return out;
        }

        void FillDepthStencil(const PassInfo &info, WGPUDepthStencilState &depth)
        {
            depth.format = ToWgpuFormat(info.depthFormat);
            depth.depthWriteEnabled = info.depthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depth.depthCompare = info.depthTestEnable ? ToCompare(info.depthCompareOp) : WGPUCompareFunction_Always;

            WGPUStencilFaceState face = WGPU_STENCIL_FACE_STATE_INIT;
            face.compare = WGPUCompareFunction_Always;
            face.failOp = WGPUStencilOperation_Keep;
            face.depthFailOp = WGPUStencilOperation_Keep;
            face.passOp = WGPUStencilOperation_Keep;
            depth.stencilWriteMask = 0;
            if (info.stencilTestEnable)
            {
                face.compare = ToCompare(info.stencilCompareOp);
                face.failOp = ToStencilOp(info.stencilFailOp);
                face.depthFailOp = ToStencilOp(info.stencilDepthFailOp);
                face.passOp = ToStencilOp(info.stencilPassOp);
                depth.stencilReadMask = info.stencilCompareMask;
                depth.stencilWriteMask = info.stencilWriteMask;
            }

            depth.stencilFront = face;
            depth.stencilBack = face;
            if (!info.depthBiasEnable)
                return;

            depth.depthBias = static_cast<int32_t>(info.depthBiasConstantFactor);
            depth.depthBiasSlopeScale = info.depthBiasSlopeFactor;
            depth.depthBiasClamp = info.depthBiasClamp;
        }

        // `blends` must outlive the pipeline creation: targets point into it.
        void FillTargets(const PassInfo &info,
                         std::vector<WGPUColorTargetState> &targets,
                         std::vector<WGPUBlendState> &blends)
        {
            targets.assign(info.colorFormats.size(), WGPU_COLOR_TARGET_STATE_INIT);
            blends.assign(info.colorFormats.size(), WGPU_BLEND_STATE_INIT);
            for (size_t i = 0; i < targets.size(); ++i)
            {
                WGPUColorTargetState &target = targets[i];
                target.format = ToWgpuFormat(info.colorFormats[i]);
                target.writeMask = WGPUColorWriteMask_All;
                if (i >= info.colorBlendAttachments.size())
                    continue;

                const BlendState &attachment = info.colorBlendAttachments[i];
                target.writeMask = static_cast<WGPUColorWriteMask>(attachment.colorWriteMask); // same R, G, B, A bits
                if (!attachment.blendEnable || !info.blendEnable)
                    continue;

                blends[i].color.srcFactor = ToBlendFactor(attachment.srcColorBlendFactor);
                blends[i].color.dstFactor = ToBlendFactor(attachment.dstColorBlendFactor);
                blends[i].color.operation = ToBlendOp(attachment.colorBlendOp);
                blends[i].alpha.srcFactor = ToBlendFactor(attachment.srcAlphaBlendFactor);
                blends[i].alpha.dstFactor = ToBlendFactor(attachment.dstAlphaBlendFactor);
                blends[i].alpha.operation = ToBlendOp(attachment.alphaBlendOp);
                target.blend = &blends[i];
            }
        }

        WGPUStringView View(const std::string &text)
        {
            return {text.data(), text.size()};
        }
    } // namespace

    WebGpuPipelineImpl::WebGpuPipelineImpl(Pipeline *, PassInfo &info)
    {
        PE_ERROR_IF(info.acceleration.rayGen, "WebGPU pipeline '%s': WebGPU has no ray tracing", info.name.c_str());

        RecordPushRange(info);
        CreateLayout(info);
        if (info.pCompShader)
            CreateCompute(info);
        else
            CreateRender(info);
    }

    WebGpuPipelineImpl::~WebGpuPipelineImpl()
    {
        if (m_render)
            wgpuRenderPipelineRelease(m_render);

        if (m_compute)
            wgpuComputePipelineRelease(m_compute);

        if (m_layout)
            wgpuPipelineLayoutRelease(m_layout);
    }

    // One 0-based range for the stages that push, sized to the larger block as on DX12; a ring slot holds 128 bytes.
    void WebGpuPipelineImpl::RecordPushRange(PassInfo &info)
    {
        info.m_pushConstantStages.clear();
        info.m_pushConstantOffsets.clear();
        info.m_pushConstantSizes.clear();

        size_t size = 0;
        PeShaderStageFlags stages = PE_SHADER_STAGE_COMPUTE;
        if (info.pCompShader)
        {
            size = info.pCompShader->GetPushConstantDesc().size;
        }
        else
        {
            const size_t vert = info.pVertShader ? info.pVertShader->GetPushConstantDesc().size : 0;
            const size_t frag = info.pFragShader ? info.pFragShader->GetPushConstantDesc().size : 0;
            size = std::max(vert, frag);
            stages = PE_SHADER_STAGE_VERTEX | PE_SHADER_STAGE_FRAGMENT;
        }

        if (size == 0)
            return;

        PE_ERROR_IF(size > WebGpuRhiImpl::kPushBytes, "WebGPU pipeline '%s': push constants over 128 bytes", info.name.c_str());
        info.m_pushConstantStages.push_back(stages);
        info.m_pushConstantOffsets.push_back(0);
        info.m_pushConstantSizes.push_back(static_cast<uint32_t>(size));
    }

    void WebGpuPipelineImpl::CreateLayout(PassInfo &info)
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();
        const std::vector<Descriptor *> &sets = info.GetDescriptors(0);
        PE_ERROR_IF(sets.size() > WebGpuRhiImpl::kPushGroup,
                    "WebGPU pipeline '%s': set 3 is the push-constant slot on WebGPU (spec decision 8)",
                    info.name.c_str());

        DescriptorLayout *empty = DescriptorLayout::GetOrCreate({}, PE_SHADER_STAGE_ALL);
        WGPUBindGroupLayout groups[WebGpuRhiImpl::kPushGroup + 1] = {};
        for (uint32_t i = 0; i < WebGpuRhiImpl::kPushGroup; ++i)
        {
            Descriptor *set = i < sets.size() ? sets[i] : nullptr;
            groups[i] = WebGpuDescriptorLayoutImpl::From(set ? set->GetLayout() : empty)->m_layout;
            if (!set)
                m_emptyGroups |= 1u << i;
        }

        groups[WebGpuRhiImpl::kPushGroup] = web->PushLayout();

        WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        desc.label = View(info.name);
        desc.bindGroupLayoutCount = WebGpuRhiImpl::kPushGroup + 1;
        desc.bindGroupLayouts = groups;
        m_layout = wgpuDeviceCreatePipelineLayout(web->m_device, &desc);
    }

    void WebGpuPipelineImpl::CreateCompute(PassInfo &info)
    {
        const std::string entry = info.pCompShader->GetEntryName();
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.label = View(info.name);
        desc.layout = m_layout;
        desc.compute.module = WebGpuShaderImpl::From(info.pCompShader)->GetOrCreateModule();
        desc.compute.entryPoint = View(entry);
        m_compute = wgpuDeviceCreateComputePipeline(WebGpuRhiImpl::Get()->m_device, &desc);
        PE_ERROR_IF(!m_compute, "WebGPU pipeline '%s': compute pipeline creation failed", info.name.c_str());
    }

    void WebGpuPipelineImpl::CreateRender(PassInfo &info)
    {
        PE_ERROR_IF(!info.pVertShader, "WebGPU pipeline '%s': no vertex shader", info.name.c_str());
        PE_ERROR_IF(info.polygonMode != PE_POLYGON_MODE_FILL, "WebGPU pipeline '%s': WebGPU only fills polygons", info.name.c_str());
        PE_ERROR_IF(!info.pFragShader && !info.colorFormats.empty(),
                    "WebGPU pipeline '%s': colour targets without a fragment shader",
                    info.name.c_str());

        const std::string vertEntry = info.pVertShader->GetEntryName();
        const std::string fragEntry = info.pFragShader ? info.pFragShader->GetEntryName() : std::string();
        VertexInput input = ReflectVertexInput(info.pVertShader, info.name);
        std::vector<WGPUColorTargetState> targets;
        std::vector<WGPUBlendState> blends;
        FillTargets(info, targets, blends);

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.label = View(info.name);
        desc.layout = m_layout;
        desc.vertex.module = WebGpuShaderImpl::From(info.pVertShader)->GetOrCreateModule();
        desc.vertex.entryPoint = View(vertEntry);
        desc.vertex.bufferCount = input.buffers.size();
        desc.vertex.buffers = input.buffers.data();
        desc.primitive.topology = ToTopology(info.topology, info.name);
        desc.primitive.cullMode = ToCullMode(info.cullMode, info.name);
        // The engine's clockwise front faces: WebGPU and Vulkan both judge winding in framebuffer coordinates
        // (WebGPU section 23.2.5.4), and naga's Y flip keeps Vulkan's framebuffer image.
        desc.primitive.frontFace = WGPUFrontFace_CW;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFF;

        WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
        if (info.depthFormat != PE_FORMAT_UNDEFINED)
        {
            FillDepthStencil(info, depth);
            desc.depthStencil = &depth;
        }

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        if (info.pFragShader)
        {
            fragment.module = WebGpuShaderImpl::From(info.pFragShader)->GetOrCreateModule();
            fragment.entryPoint = View(fragEntry);
            fragment.targetCount = targets.size();
            fragment.targets = targets.data();
            desc.fragment = &fragment;
        }

        m_render = wgpuDeviceCreateRenderPipeline(WebGpuRhiImpl::Get()->m_device, &desc);
        PE_ERROR_IF(!m_render, "WebGPU pipeline '%s': render pipeline creation failed", info.name.c_str());
    }
} // namespace pe
