// A-Buffer order-independent transparency port.
//
// Three render passes:
//   1. Opaque     — writes color + depth for 8 cubes in a staggered grid.
//   2. Translucent — disables color writes; appends per-pixel fragments to a
//                    linked list via atomics (InterlockedAdd/InterlockedExchange).
//                    No pipeline depth test; early-discards against the opaque depth.
//   3. Composite  — fullscreen pass; walks the per-pixel list, insertion-sorts
//                    by depth, and blends back-to-front with premultiplied alpha.
//
// Each frame the heads buffer is reset via wgpuQueueWriteBuffer:
//   heads[0]   = 0          (global fragment counter)
//   heads[1..] = 0xFFFFFFFF (end-of-list sentinel)

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"
#include "TeapotMesh.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr uint32_t kInstanceCount = 8;
    constexpr uint32_t kAverageLayersPerFragment = 4;
    constexpr float kPi = 3.14159265358979323846f;

    // Packed 24-byte linked-list entry. Matches the HLSL `ListEntry` layout.
    struct ListEntry
    {
        uint32_t color;
        float depth;
        uint32_t next;
        uint32_t _pad0;
        uint32_t _pad1;
        uint32_t _pad2;
    };
    static_assert(sizeof(ListEntry) == 24, "ListEntry must be 24 bytes to match HLSL");

    struct SceneUniforms
    {
        float modelViewProjection[16];
        uint32_t maxStorableFragments;
        uint32_t targetWidth;
        uint32_t _pad0;
        uint32_t _pad1;
    };
    static_assert(sizeof(SceneUniforms) == 80);

    // sliceStartY is always 0 for our single-pass implementation, but the
    // int3 padding is required so the cbuffer matches HLSL's 16-byte alignment.
    struct SliceInfo
    {
        int32_t sliceStartY;
        int32_t _pad0;
        int32_t _pad1;
        int32_t _pad2;
    };
    static_assert(sizeof(SliceInfo) == 16);

    class ABufferSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_opaqueVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "opaque.vert.hlsl").c_str(), "abuf_opaque_vs");
            m_opaquePs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "opaque.pixel.hlsl").c_str(), "abuf_opaque_ps");
            m_translucentVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "translucent.vert.hlsl").c_str(), "abuf_trans_vs");
            m_translucentPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "translucent.pixel.hlsl").c_str(), "abuf_trans_ps");
            m_compositeVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "composite.vert.hlsl").c_str(), "abuf_comp_vs");
            m_compositePs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "composite.pixel.hlsl").c_str(), "abuf_comp_ps");
            if (!m_opaqueVs || !m_opaquePs || !m_translucentVs || !m_translucentPs ||
                !m_compositeVs || !m_compositePs)
            {
                return false;
            }

            if (!CreateGeometryBuffers(ctx))
                return false;
            if (!CreateUniformBuffers(ctx))
                return false;
            if (!CreateBindGroupLayouts(ctx))
                return false;
            if (!CreatePipelines(ctx))
                return false;

            SliceInfo slice{};
            slice.sliceStartY = 0;
            wgpuQueueWriteBuffer(ctx.queue, m_sliceInfoBuffer, 0, &slice, sizeof(slice));

            fprintf(stdout, "[ABuffer] Pipelines opaque/translucent/composite ready\n");
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseFramebufferResources();
            if (width == 0 || height == 0)
                return;

            m_depthTexture = CreateRenderTexture(ctx.device,
                                                 width,
                                                 height,
                                                 WGPUTextureFormat_Depth24Plus,
                                                 static_cast<WGPUTextureUsage>(
                                                     WGPUTextureUsage_RenderAttachment |
                                                     WGPUTextureUsage_TextureBinding),
                                                 "abuf_depth");
            if (!m_depthTexture)
                return;

            m_depthRenderView = CreateTextureView(m_depthTexture,
                                                  WGPUTextureFormat_Depth24Plus,
                                                  WGPUTextureAspect_All,
                                                  "abuf_depth_rtv");
            m_depthSampledView = CreateTextureView(m_depthTexture,
                                                   WGPUTextureFormat_Depth24Plus,
                                                   WGPUTextureAspect_DepthOnly,
                                                   "abuf_depth_srv");
            if (!m_depthRenderView || !m_depthSampledView)
                return;

            // Linked-list storage: width * height * kAverageLayersPerFragment entries.
            const uint64_t headsCount = static_cast<uint64_t>(width) * height;
            const uint64_t maxFragments = headsCount * kAverageLayersPerFragment;

            const uint64_t headsSize = (1u + headsCount) * sizeof(uint32_t);
            const uint64_t listSize = maxFragments * sizeof(ListEntry);

            m_headsBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                headsSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst),
                "abuf_heads");
            m_linkedListBuffer = pwgpu::test::CreateBuffer(
                ctx.device, listSize, WGPUBufferUsage_Storage, "abuf_list");
            if (!m_headsBuffer || !m_linkedListBuffer)
                return;

            // CPU-side init pattern used to reset the heads buffer every frame.
            m_headsInit.assign(static_cast<size_t>(1u + headsCount), 0xFFFFFFFFu);
            m_headsInit[0] = 0u;

            m_headsCount = headsCount;
            m_maxStorableFragments = static_cast<uint32_t>(maxFragments);
            m_targetWidth = width;

            RebuildResizeBindGroups(ctx.device);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect =
                ctx.height > 0
                    ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                    : 1.0f;

            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 0.1f, 2000.f);
            projection[1][1] *= -1.f; // Vulkan Y-down NDC

            const float radians = kPi * static_cast<float>(ctx.timing.elapsedSeconds) / 5.0f;
            vec3 eyeBase(0.f, 5.f, -100.f);
            vec3 eye(std::cos(radians) * eyeBase.x + std::sin(radians) * eyeBase.z,
                     eyeBase.y,
                     -std::sin(radians) * eyeBase.x + std::cos(radians) * eyeBase.z);
            mat4 view = glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 viewProj = projection * view;

            SceneUniforms scene{};
            memcpy(scene.modelViewProjection, glm::value_ptr(viewProj), sizeof(scene.modelViewProjection));
            scene.maxStorableFragments = m_maxStorableFragments;
            scene.targetWidth = m_targetWidth;
            wgpuQueueWriteBuffer(ctx.queue, m_sceneBuffer, 0, &scene, sizeof(scene));
        }

        bool Execute(pwgpu::test::SampleContext &ctx,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_opaquePipeline || !m_translucentPipeline || !m_compositePipeline ||
                !m_headsBuffer || !m_linkedListBuffer)
            {
                return true;
            }

            // Reset the heads buffer for this frame (counter=0, heads=0xFFFFFFFF).
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_headsBuffer,
                                 0,
                                 m_headsInit.data(),
                                 m_headsInit.size() * sizeof(uint32_t));

            // 1. Opaque pass: clears color + depth, draws 8 opaque cubes.
            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Clear;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_depthRenderView;
                depthAttachment.depthClearValue = 1.0f;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"abuf_opaque_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;
                passDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_opaquePipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_opaqueBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                     0,
                                                     m_vertexBuffer,
                                                     0,
                                                     static_cast<uint64_t>(m_vertexCount) *
                                                         3u * sizeof(float));
                wgpuRenderPassEncoderSetIndexBuffer(
                    pass, m_indexBuffer, WGPUIndexFormat_Uint16, 0,
                    static_cast<uint64_t>(m_indexCount) * sizeof(uint16_t));
                wgpuRenderPassEncoderDrawIndexed(pass, m_indexCount, kInstanceCount, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 2. Translucent pass: no color writes, no depth test, appends to list.
            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"abuf_translucent_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;
                passDesc.depthStencilAttachment = nullptr;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_translucentPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_translucentBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                     0,
                                                     m_vertexBuffer,
                                                     0,
                                                     static_cast<uint64_t>(m_vertexCount) *
                                                         3u * sizeof(float));
                wgpuRenderPassEncoderSetIndexBuffer(
                    pass, m_indexBuffer, WGPUIndexFormat_Uint16, 0,
                    static_cast<uint64_t>(m_indexCount) * sizeof(uint16_t));
                wgpuRenderPassEncoderDrawIndexed(pass, m_indexCount, kInstanceCount, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 3. Composite pass: fullscreen quad, walks the list per pixel.
            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Load;
                colorAttachment.storeOp = WGPUStoreOp_Store;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"abuf_composite_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_compositePipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_compositeBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseFramebufferResources();

            if (m_compositePipeline)
                wgpuRenderPipelineRelease(m_compositePipeline);
            if (m_translucentPipeline)
                wgpuRenderPipelineRelease(m_translucentPipeline);
            if (m_opaquePipeline)
                wgpuRenderPipelineRelease(m_opaquePipeline);
            if (m_compositePipelineLayout)
                wgpuPipelineLayoutRelease(m_compositePipelineLayout);
            if (m_translucentPipelineLayout)
                wgpuPipelineLayoutRelease(m_translucentPipelineLayout);
            if (m_opaquePipelineLayout)
                wgpuPipelineLayoutRelease(m_opaquePipelineLayout);
            if (m_opaqueBindGroup)
                wgpuBindGroupRelease(m_opaqueBindGroup);
            if (m_compositeBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_compositeBindGroupLayout);
            if (m_translucentBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_translucentBindGroupLayout);
            if (m_opaqueBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_opaqueBindGroupLayout);
            if (m_sliceInfoBuffer)
                wgpuBufferRelease(m_sliceInfoBuffer);
            if (m_sceneBuffer)
                wgpuBufferRelease(m_sceneBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);

            if (m_compositeVs)
                wgpuShaderModuleRelease(m_compositeVs);
            if (m_compositePs)
                wgpuShaderModuleRelease(m_compositePs);
            if (m_translucentVs)
                wgpuShaderModuleRelease(m_translucentVs);
            if (m_translucentPs)
                wgpuShaderModuleRelease(m_translucentPs);
            if (m_opaqueVs)
                wgpuShaderModuleRelease(m_opaqueVs);
            if (m_opaquePs)
                wgpuShaderModuleRelease(m_opaquePs);
        }

    private:
        static WGPUTexture CreateRenderTexture(WGPUDevice device,
                                               uint32_t width,
                                               uint32_t height,
                                               WGPUTextureFormat format,
                                               WGPUTextureUsage usage,
                                               const char *label)
        {
            WGPUTextureDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.format = format;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {width, height, 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.usage = usage;
            return wgpuDeviceCreateTexture(device, &desc);
        }

        static WGPUTextureView CreateTextureView(WGPUTexture texture,
                                                 WGPUTextureFormat format,
                                                 WGPUTextureAspect aspect,
                                                 const char *label)
        {
            WGPUTextureViewDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.format = format;
            desc.dimension = WGPUTextureViewDimension_2D;
            desc.mipLevelCount = 1;
            desc.arrayLayerCount = 1;
            desc.aspect = aspect;
            return wgpuTextureCreateView(texture, &desc);
        }

        bool CreateGeometryBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_vertexCount = pwgpu::test::teapot::kVertexCount;
            m_indexCount = pwgpu::test::teapot::kIndexCount;

            const uint64_t vbSize =
                static_cast<uint64_t>(m_vertexCount) * 3u * sizeof(float);
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                vbSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst),
                "abuf_teapot_vb");
            if (!m_vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0,
                                 pwgpu::test::teapot::kPositions, vbSize);

            // Uint16 indices; round the upload size up to 4 so writeBuffer's
            // alignment requirement is met.
            const uint64_t ibByteSize =
                static_cast<uint64_t>(m_indexCount) * sizeof(uint16_t);
            const uint64_t ibSize = (ibByteSize + 3u) & ~static_cast<uint64_t>(3u);
            m_indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                ibSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst),
                "abuf_teapot_ib");
            if (!m_indexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_indexBuffer, 0,
                                 pwgpu::test::teapot::kIndices, ibByteSize);
            return true;
        }

        bool CreateUniformBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_sceneBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(SceneUniforms),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "abuf_scene_ub");
            m_sliceInfoBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(SliceInfo),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "abuf_slice_ub");
            return m_sceneBuffer && m_sliceInfoBuffer;
        }

        bool CreateBindGroupLayouts(pwgpu::test::SampleContext &ctx)
        {
            // Opaque: just the scene uniform.
            {
                WGPUBindGroupLayoutEntry e{};
                e.binding = 0;
                e.visibility = WGPUShaderStage_Vertex;
                e.buffer.type = WGPUBufferBindingType_Uniform;
                e.buffer.minBindingSize = sizeof(SceneUniforms);

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"abuf_bgl_opaque", WGPU_STRLEN};
                desc.entryCount = 1;
                desc.entries = &e;
                m_opaqueBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_opaqueBindGroupLayout)
                    return false;
            }

            // Translucent: scene UBO + heads + list + depth SRV + slice UBO.
            {
                WGPUBindGroupLayoutEntry entries[5] = {};
                entries[0].binding = 0;
                entries[0].visibility =
                    static_cast<WGPUShaderStage>(WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
                entries[0].buffer.type = WGPUBufferBindingType_Uniform;
                entries[0].buffer.minBindingSize = sizeof(SceneUniforms);

                entries[1].binding = 1;
                entries[1].visibility = WGPUShaderStage_Fragment;
                entries[1].buffer.type = WGPUBufferBindingType_Storage;

                entries[2].binding = 2;
                entries[2].visibility = WGPUShaderStage_Fragment;
                entries[2].buffer.type = WGPUBufferBindingType_Storage;

                entries[3].binding = 3;
                entries[3].visibility = WGPUShaderStage_Fragment;
                entries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
                entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

                entries[4].binding = 4;
                entries[4].visibility = WGPUShaderStage_Fragment;
                entries[4].buffer.type = WGPUBufferBindingType_Uniform;
                entries[4].buffer.minBindingSize = sizeof(SliceInfo);

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"abuf_bgl_translucent", WGPU_STRLEN};
                desc.entryCount = 5;
                desc.entries = entries;
                m_translucentBindGroupLayout =
                    wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_translucentBindGroupLayout)
                    return false;
            }

            // Composite: scene UBO + heads + list + slice UBO (no depth SRV).
            {
                WGPUBindGroupLayoutEntry entries[4] = {};
                entries[0].binding = 0;
                entries[0].visibility = WGPUShaderStage_Fragment;
                entries[0].buffer.type = WGPUBufferBindingType_Uniform;
                entries[0].buffer.minBindingSize = sizeof(SceneUniforms);

                entries[1].binding = 1;
                entries[1].visibility = WGPUShaderStage_Fragment;
                entries[1].buffer.type = WGPUBufferBindingType_Storage;

                entries[2].binding = 2;
                entries[2].visibility = WGPUShaderStage_Fragment;
                entries[2].buffer.type = WGPUBufferBindingType_Storage;

                entries[3].binding = 3;
                entries[3].visibility = WGPUShaderStage_Fragment;
                entries[3].buffer.type = WGPUBufferBindingType_Uniform;
                entries[3].buffer.minBindingSize = sizeof(SliceInfo);

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"abuf_bgl_composite", WGPU_STRLEN};
                desc.entryCount = 4;
                desc.entries = entries;
                m_compositeBindGroupLayout =
                    wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_compositeBindGroupLayout)
                    return false;
            }

            // Build the static opaque bind group (it only references the scene UBO).
            {
                WGPUBindGroupEntry entry{};
                entry.binding = 0;
                entry.buffer = m_sceneBuffer;
                entry.size = sizeof(SceneUniforms);

                WGPUBindGroupDescriptor desc{};
                desc.label = {"abuf_bg_opaque", WGPU_STRLEN};
                desc.layout = m_opaqueBindGroupLayout;
                desc.entryCount = 1;
                desc.entries = &entry;
                m_opaqueBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
                if (!m_opaqueBindGroup)
                    return false;
            }

            return true;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            WGPUVertexAttribute posAttrib{};
            posAttrib.format = WGPUVertexFormat_Float32x3;
            posAttrib.offset = 0;
            posAttrib.shaderLocation = 0;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = 3 * sizeof(float);
            vbLayout.stepMode = WGPUVertexStepMode_Vertex;
            vbLayout.attributeCount = 1;
            vbLayout.attributes = &posAttrib;

            // Opaque pipeline: color + depth, back-face culled.
            {
                m_opaquePipelineLayout =
                    CreatePipelineLayout(ctx.device, &m_opaqueBindGroupLayout, 1, "abuf_pl_opaque");
                if (!m_opaquePipelineLayout)
                    return false;

                WGPUColorTargetState colorTarget{};
                colorTarget.format = ctx.surfaceFormat;
                colorTarget.writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fragment{};
                fragment.module = m_opaquePs;
                fragment.entryPoint = {"PSMain", WGPU_STRLEN};
                fragment.targetCount = 1;
                fragment.targets = &colorTarget;

                WGPUDepthStencilState depth{};
                depth.format = WGPUTextureFormat_Depth24Plus;
                depth.depthWriteEnabled = WGPUOptionalBool_True;
                depth.depthCompare = WGPUCompareFunction_Less;
                depth.stencilFront.compare = WGPUCompareFunction_Always;
                depth.stencilBack.compare = WGPUCompareFunction_Always;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"abuf_pipeline_opaque", WGPU_STRLEN};
                desc.layout = m_opaquePipelineLayout;
                desc.vertex.module = m_opaqueVs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 1;
                desc.vertex.buffers = &vbLayout;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.primitive.cullMode = WGPUCullMode_Back;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = &depth;
                desc.fragment = &fragment;
                m_opaquePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!m_opaquePipeline)
                    return false;
            }

            // Translucent pipeline: writeMask=None, no depth state, atomic appends.
            {
                m_translucentPipelineLayout = CreatePipelineLayout(
                    ctx.device, &m_translucentBindGroupLayout, 1, "abuf_pl_translucent");
                if (!m_translucentPipelineLayout)
                    return false;

                WGPUColorTargetState colorTarget{};
                colorTarget.format = ctx.surfaceFormat;
                colorTarget.writeMask = WGPUColorWriteMask_None;

                WGPUFragmentState fragment{};
                fragment.module = m_translucentPs;
                fragment.entryPoint = {"PSMain", WGPU_STRLEN};
                fragment.targetCount = 1;
                fragment.targets = &colorTarget;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"abuf_pipeline_translucent", WGPU_STRLEN};
                desc.layout = m_translucentPipelineLayout;
                desc.vertex.module = m_translucentVs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 1;
                desc.vertex.buffers = &vbLayout;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = nullptr;
                desc.fragment = &fragment;
                m_translucentPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!m_translucentPipeline)
                    return false;
            }

            // Composite pipeline: fullscreen premultiplied-alpha blend.
            {
                m_compositePipelineLayout = CreatePipelineLayout(
                    ctx.device, &m_compositeBindGroupLayout, 1, "abuf_pl_composite");
                if (!m_compositePipelineLayout)
                    return false;

                WGPUBlendState blend{};
                blend.color.srcFactor = WGPUBlendFactor_One;
                blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                blend.color.operation = WGPUBlendOperation_Add;
                blend.alpha.srcFactor = WGPUBlendFactor_One;
                blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                blend.alpha.operation = WGPUBlendOperation_Add;

                WGPUColorTargetState colorTarget{};
                colorTarget.format = ctx.surfaceFormat;
                colorTarget.writeMask = WGPUColorWriteMask_All;
                colorTarget.blend = &blend;

                WGPUFragmentState fragment{};
                fragment.module = m_compositePs;
                fragment.entryPoint = {"PSMain", WGPU_STRLEN};
                fragment.targetCount = 1;
                fragment.targets = &colorTarget;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"abuf_pipeline_composite", WGPU_STRLEN};
                desc.layout = m_compositePipelineLayout;
                desc.vertex.module = m_compositeVs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = nullptr;
                desc.fragment = &fragment;
                m_compositePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!m_compositePipeline)
                    return false;
            }

            return true;
        }

        static WGPUPipelineLayout CreatePipelineLayout(WGPUDevice device,
                                                       WGPUBindGroupLayout *layouts,
                                                       uint32_t count,
                                                       const char *label)
        {
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.bindGroupLayoutCount = count;
            desc.bindGroupLayouts = layouts;
            return wgpuDeviceCreatePipelineLayout(device, &desc);
        }

        void RebuildResizeBindGroups(WGPUDevice device)
        {
            if (m_translucentBindGroup)
            {
                wgpuBindGroupRelease(m_translucentBindGroup);
                m_translucentBindGroup = nullptr;
            }
            if (m_compositeBindGroup)
            {
                wgpuBindGroupRelease(m_compositeBindGroup);
                m_compositeBindGroup = nullptr;
            }

            if (!m_translucentBindGroupLayout || !m_compositeBindGroupLayout ||
                !m_headsBuffer || !m_linkedListBuffer || !m_depthSampledView ||
                !m_sceneBuffer || !m_sliceInfoBuffer)
            {
                return;
            }

            const uint64_t headsSize = (1u + m_headsCount) * sizeof(uint32_t);
            const uint64_t listSize =
                static_cast<uint64_t>(m_maxStorableFragments) * sizeof(ListEntry);

            {
                WGPUBindGroupEntry entries[5] = {};
                entries[0].binding = 0;
                entries[0].buffer = m_sceneBuffer;
                entries[0].size = sizeof(SceneUniforms);
                entries[1].binding = 1;
                entries[1].buffer = m_headsBuffer;
                entries[1].size = headsSize;
                entries[2].binding = 2;
                entries[2].buffer = m_linkedListBuffer;
                entries[2].size = listSize;
                entries[3].binding = 3;
                entries[3].textureView = m_depthSampledView;
                entries[4].binding = 4;
                entries[4].buffer = m_sliceInfoBuffer;
                entries[4].size = sizeof(SliceInfo);

                WGPUBindGroupDescriptor desc{};
                desc.label = {"abuf_bg_translucent", WGPU_STRLEN};
                desc.layout = m_translucentBindGroupLayout;
                desc.entryCount = 5;
                desc.entries = entries;
                m_translucentBindGroup = wgpuDeviceCreateBindGroup(device, &desc);
            }

            {
                WGPUBindGroupEntry entries[4] = {};
                entries[0].binding = 0;
                entries[0].buffer = m_sceneBuffer;
                entries[0].size = sizeof(SceneUniforms);
                entries[1].binding = 1;
                entries[1].buffer = m_headsBuffer;
                entries[1].size = headsSize;
                entries[2].binding = 2;
                entries[2].buffer = m_linkedListBuffer;
                entries[2].size = listSize;
                entries[3].binding = 3;
                entries[3].buffer = m_sliceInfoBuffer;
                entries[3].size = sizeof(SliceInfo);

                WGPUBindGroupDescriptor desc{};
                desc.label = {"abuf_bg_composite", WGPU_STRLEN};
                desc.layout = m_compositeBindGroupLayout;
                desc.entryCount = 4;
                desc.entries = entries;
                m_compositeBindGroup = wgpuDeviceCreateBindGroup(device, &desc);
            }
        }

        void ReleaseFramebufferResources()
        {
            if (m_translucentBindGroup)
            {
                wgpuBindGroupRelease(m_translucentBindGroup);
                m_translucentBindGroup = nullptr;
            }
            if (m_compositeBindGroup)
            {
                wgpuBindGroupRelease(m_compositeBindGroup);
                m_compositeBindGroup = nullptr;
            }
            if (m_linkedListBuffer)
            {
                wgpuBufferRelease(m_linkedListBuffer);
                m_linkedListBuffer = nullptr;
            }
            if (m_headsBuffer)
            {
                wgpuBufferRelease(m_headsBuffer);
                m_headsBuffer = nullptr;
            }
            if (m_depthSampledView)
            {
                wgpuTextureViewRelease(m_depthSampledView);
                m_depthSampledView = nullptr;
            }
            if (m_depthRenderView)
            {
                wgpuTextureViewRelease(m_depthRenderView);
                m_depthRenderView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }

            m_headsInit.clear();
            m_headsCount = 0;
            m_maxStorableFragments = 0;
            m_targetWidth = 0;
        }

        WGPUShaderModule m_opaqueVs = nullptr;
        WGPUShaderModule m_opaquePs = nullptr;
        WGPUShaderModule m_translucentVs = nullptr;
        WGPUShaderModule m_translucentPs = nullptr;
        WGPUShaderModule m_compositeVs = nullptr;
        WGPUShaderModule m_compositePs = nullptr;

        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_sceneBuffer = nullptr;
        WGPUBuffer m_sliceInfoBuffer = nullptr;
        WGPUBuffer m_headsBuffer = nullptr;
        WGPUBuffer m_linkedListBuffer = nullptr;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthRenderView = nullptr;
        WGPUTextureView m_depthSampledView = nullptr;

        WGPUBindGroupLayout m_opaqueBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_translucentBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_compositeBindGroupLayout = nullptr;

        WGPUPipelineLayout m_opaquePipelineLayout = nullptr;
        WGPUPipelineLayout m_translucentPipelineLayout = nullptr;
        WGPUPipelineLayout m_compositePipelineLayout = nullptr;

        WGPUBindGroup m_opaqueBindGroup = nullptr;
        WGPUBindGroup m_translucentBindGroup = nullptr;
        WGPUBindGroup m_compositeBindGroup = nullptr;

        WGPURenderPipeline m_opaquePipeline = nullptr;
        WGPURenderPipeline m_translucentPipeline = nullptr;
        WGPURenderPipeline m_compositePipeline = nullptr;

        std::vector<uint32_t> m_headsInit{};
        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;
        uint64_t m_headsCount = 0;
        uint32_t m_maxStorableFragments = 0;
        uint32_t m_targetWidth = 0;
    };

    pwgpu::test::SampleAppDesc MakeABufferDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "ABufferTest";
        desc.windowTitle = "PhasmaWebGPU A-Buffer";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    ABufferSample sample;
    pwgpu::test::SampleApp app(sample, MakeABufferDesc());
    return app.Run(argc, argv);
}
