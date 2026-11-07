#include "API/WebGPU/WebGpuBlit.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        // Fullscreen triangle; the viewport is the destination rectangle, and each fragment maps its position in
        // that rectangle to the source rectangle in normalized coordinates.
        constexpr const char *kBlitWgsl = R"(
struct Params {
    dst : vec4f, // origin.xy and size.zw of the destination rectangle, in texels
    src : vec4f, // normalized source rectangle: min.xy, max.zw
}
@group(0) @binding(0) var srcTexture : texture_2d<f32>;
@group(0) @binding(1) var srcSampler : sampler;
@group(0) @binding(2) var<uniform> params : Params;

@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

@fragment fn fs(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    let t = (pos.xy - params.dst.xy) / params.dst.zw;
    return textureSampleLevel(srcTexture, srcSampler, mix(params.src.xy, params.src.zw, t), 0.0);
}
)";

        struct BlitState
        {
            WGPUShaderModule module = nullptr;
            WGPUBindGroupLayout groupLayout = nullptr;
            WGPUPipelineLayout pipelineLayout = nullptr;
            WGPUSampler samplers[2] = {};                               // nearest, linear
            std::unordered_map<uint32_t, WGPURenderPipeline> pipelines; // by destination format
        };

        BlitState &State()
        {
            static BlitState state;
            return state;
        }

        void CreateShared(BlitState &s, WGPUDevice device)
        {
            WGPUShaderSourceWGSL wgsl{};
            wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
            wgsl.code = {kBlitWgsl, WGPU_STRLEN};
            WGPUShaderModuleDescriptor moduleDesc{};
            moduleDesc.nextInChain = &wgsl.chain;
            moduleDesc.label = {"WebGPU_blit", WGPU_STRLEN};
            s.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].texture.sampleType = WGPUTextureSampleType_Float;
            entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].buffer.type = WGPUBufferBindingType_Uniform;

            WGPUBindGroupLayoutDescriptor groupDesc{};
            groupDesc.entryCount = 3;
            groupDesc.entries = entries;
            s.groupLayout = wgpuDeviceCreateBindGroupLayout(device, &groupDesc);

            WGPUPipelineLayoutDescriptor layoutDesc{};
            layoutDesc.bindGroupLayoutCount = 1;
            layoutDesc.bindGroupLayouts = &s.groupLayout;
            s.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

            for (int i = 0; i < 2; ++i)
            {
                WGPUSamplerDescriptor samplerDesc{};
                samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
                samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
                samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
                samplerDesc.magFilter = i ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
                samplerDesc.minFilter = samplerDesc.magFilter;
                samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
                samplerDesc.lodMaxClamp = 32.0f;
                samplerDesc.maxAnisotropy = 1;
                s.samplers[i] = wgpuDeviceCreateSampler(device, &samplerDesc);
            }
        }

        WGPURenderPipeline PipelineFor(WGPUTextureFormat format)
        {
            BlitState &s = State();
            WGPUDevice device = WebGpuRhiImpl::Get()->m_device;
            if (!s.module)
                CreateShared(s, device);

            auto it = s.pipelines.find(static_cast<uint32_t>(format));
            if (it != s.pipelines.end())
                return it->second;

            WGPUColorTargetState target{};
            target.format = format;
            target.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragment{};
            fragment.module = s.module;
            fragment.entryPoint = {"fs", WGPU_STRLEN};
            fragment.targetCount = 1;
            fragment.targets = &target;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"WebGPU_blit", WGPU_STRLEN};
            pipelineDesc.layout = s.pipelineLayout;
            pipelineDesc.vertex.module = s.module;
            pipelineDesc.vertex.entryPoint = {"vs", WGPU_STRLEN};
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragment;
            WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
            s.pipelines.emplace(static_cast<uint32_t>(format), pipeline);
            return pipeline;
        }

        WGPUTextureView SubresourceView(WGPUTexture texture, uint32_t mip, uint32_t layer)
        {
            WGPUTextureViewDescriptor desc{};
            desc.format = wgpuTextureGetFormat(texture);
            desc.dimension = WGPUTextureViewDimension_2D;
            desc.baseMipLevel = mip;
            desc.mipLevelCount = 1;
            desc.baseArrayLayer = layer;
            desc.arrayLayerCount = 1;
            desc.aspect = WGPUTextureAspect_All;
            return wgpuTextureCreateView(texture, &desc);
        }

        // ponytail: one 32-byte uniform buffer per blit (a mip chain makes one per level); a ring with dynamic
        // offsets would avoid the allocations if blits become frequent.
        WGPUBuffer ParamsBuffer(WGPUDevice device, const float (&params)[8])
        {
            WGPUBufferDescriptor desc{};
            desc.size = sizeof(params);
            desc.usage = WGPUBufferUsage_Uniform;
            desc.mappedAtCreation = true;
            WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
            std::memcpy(wgpuBufferGetMappedRange(buffer, 0, sizeof(params)), params, sizeof(params));
            wgpuBufferUnmap(buffer);
            return buffer;
        }
    } // namespace

    void WebGpuBlitTexture(WGPUCommandEncoder encoder,
                           WGPUTexture src,
                           WGPUTexture dst,
                           const WebGpuBlitRegion &region,
                           bool linear,
                           bool clear)
    {
        WGPUDevice device = WebGpuRhiImpl::Get()->m_device;
        WGPURenderPipeline pipeline = PipelineFor(wgpuTextureGetFormat(dst));

        // Sort the destination rectangle; a reversed axis flips the source on that axis instead.
        float srcMin[2] = {region.srcMin[0], region.srcMin[1]};
        float srcMax[2] = {region.srcMax[0], region.srcMax[1]};
        int32_t dstMin[2] = {region.dstFrom[0], region.dstFrom[1]};
        int32_t dstMax[2] = {region.dstTo[0], region.dstTo[1]};
        for (int axis = 0; axis < 2; ++axis)
        {
            if (dstMin[axis] <= dstMax[axis])
                continue;

            std::swap(dstMin[axis], dstMax[axis]);
            std::swap(srcMin[axis], srcMax[axis]);
        }

        const uint32_t srcWidth = std::max(1u, wgpuTextureGetWidth(src) >> region.srcMip);
        const uint32_t srcHeight = std::max(1u, wgpuTextureGetHeight(src) >> region.srcMip);
        const float width = static_cast<float>(dstMax[0] - dstMin[0]);
        const float height = static_cast<float>(dstMax[1] - dstMin[1]);
        const float params[8] = {
            static_cast<float>(dstMin[0]),
            static_cast<float>(dstMin[1]),
            width,
            height,
            srcMin[0] / srcWidth,
            srcMin[1] / srcHeight,
            srcMax[0] / srcWidth,
            srcMax[1] / srcHeight,
        };

        WGPUTextureView srcView = SubresourceView(src, region.srcMip, region.srcLayer);
        WGPUTextureView dstView = SubresourceView(dst, region.dstMip, region.dstLayer);
        WGPUBuffer paramsBuffer = ParamsBuffer(device, params);

        WGPUBindGroupEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].textureView = srcView;
        entries[1].binding = 1;
        entries[1].sampler = State().samplers[linear ? 1 : 0];
        entries[2].binding = 2;
        entries[2].buffer = paramsBuffer;
        entries[2].size = sizeof(params);
        WGPUBindGroupDescriptor groupDesc{};
        groupDesc.layout = State().groupLayout;
        groupDesc.entryCount = 3;
        groupDesc.entries = entries;
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &groupDesc);

        WGPURenderPassColorAttachment color{};
        color.view = dstView;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc{};
        passDesc.label = {"WebGPU_blit", WGPU_STRLEN};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuRenderPassEncoderSetViewport(pass, params[0], params[1], width, height, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass,
                                            static_cast<uint32_t>(dstMin[0]),
                                            static_cast<uint32_t>(dstMin[1]),
                                            static_cast<uint32_t>(width),
                                            static_cast<uint32_t>(height));
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        // WebGPU keeps each object alive while the recorded commands use it.
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(group);
        wgpuBufferRelease(paramsBuffer);
        wgpuTextureViewRelease(dstView);
        wgpuTextureViewRelease(srcView);
    }

    void WebGpuBlitShutdown()
    {
        BlitState &s = State();
        for (auto &[format, pipeline] : s.pipelines)
            wgpuRenderPipelineRelease(pipeline);

        for (WGPUSampler sampler : s.samplers)
        {
            if (sampler)
                wgpuSamplerRelease(sampler);
        }

        if (s.pipelineLayout)
            wgpuPipelineLayoutRelease(s.pipelineLayout);

        if (s.groupLayout)
            wgpuBindGroupLayoutRelease(s.groupLayout);

        if (s.module)
            wgpuShaderModuleRelease(s.module);

        s = BlitState{};
    }
} // namespace pe
