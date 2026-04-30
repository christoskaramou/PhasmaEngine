// Primitive picking port.
//
// Three pipelines:
//   1. Forward rendering  - draws the teapot, outputs shaded color to the
//                           surface and per-primitive IDs to an r32uint texture.
//                           The pixel shader highlights the picked primitive
//                           (value read from the shared Frame buffer).
//   2. Debug view (opt.)  - fullscreen triangle pair that colorizes every
//                           primitive id from the r32uint texture.
//   3. Compute pick       - reads (pickCoord) from the Frame buffer, samples
//                           the primitive-id texture at that pixel, and writes
//                           the id back into the Frame buffer. The highlight
//                           is therefore one frame behind, matching upstream.
//
// The upstream sample picks at the mouse cursor; we poll SDL every Update and
// fall back to the window center when the mouse is outside the window.

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"
#include "../ABuffer/TeapotMesh.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    // Frame uniform block: shared between vertex/fragment (as uniform) and the
    // compute picking pass (as read-write storage).
    //   modelMatrix        : 64 bytes
    //   normalModelMatrix  : 64 bytes
    // Model uniform block is separate from Frame per the upstream sample.
    struct ModelUniforms
    {
        float modelMatrix[16];
        float normalModelMatrix[16];
    };
    static_assert(sizeof(ModelUniforms) == 128, "ModelUniforms size mismatch");

    // Frame layout:
    //   [  0, 64) viewProjectionMatrix
    //   [ 64,128) invViewProjectionMatrix
    //   [128,136) pickCoord (float2)
    //   [136,140) pickedPrimitive (uint) - written by the compute pass
    //   [140,144) padding to 16-byte alignment
    struct FrameUniforms
    {
        float viewProjectionMatrix[16];
        float invViewProjectionMatrix[16];
        float pickCoord[2];
        uint32_t pickedPrimitive;
        uint32_t _pad0;
    };
    static_assert(sizeof(FrameUniforms) == 144, "FrameUniforms size mismatch");

    class PrimitivePickingSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_forwardVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "vertexForwardRendering.vert.hlsl").c_str(),
                "pp_forward_vs");
            m_forwardPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "fragmentForwardRendering.pixel.hlsl").c_str(),
                "pp_forward_ps");
            m_quadVs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "vertexTextureQuad.vert.hlsl").c_str(),
                "pp_quad_vs");
            m_debugPs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "fragmentPrimitivesDebugView.pixel.hlsl").c_str(),
                "pp_debug_ps");
            m_pickCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "computePickPrimitive.comp.hlsl").c_str(),
                "pp_pick_cs");
            if (!m_forwardVs || !m_forwardPs || !m_quadVs || !m_debugPs || !m_pickCs)
                return false;

            if (!CreateGeometry(ctx))
                return false;
            if (!CreateUniforms(ctx))
                return false;
            if (!CreatePipelines(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseFramebufferResources();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"pp_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor depthViewDesc{};
            depthViewDesc.format = WGPUTextureFormat_Depth24Plus;
            depthViewDesc.dimension = WGPUTextureViewDimension_2D;
            depthViewDesc.mipLevelCount = 1;
            depthViewDesc.arrayLayerCount = 1;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);

            WGPUTextureDescriptor idDesc{};
            idDesc.label = {"pp_prim_id", WGPU_STRLEN};
            idDesc.dimension = WGPUTextureDimension_2D;
            idDesc.size = {width, height, 1};
            idDesc.mipLevelCount = 1;
            idDesc.sampleCount = 1;
            idDesc.format = WGPUTextureFormat_R32Uint;
            idDesc.usage = static_cast<WGPUTextureUsage>(
                WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
            m_primIdTexture = wgpuDeviceCreateTexture(ctx.device, &idDesc);
            if (!m_primIdTexture)
                return;

            WGPUTextureViewDescriptor idViewDesc{};
            idViewDesc.format = WGPUTextureFormat_R32Uint;
            idViewDesc.dimension = WGPUTextureViewDimension_2D;
            idViewDesc.mipLevelCount = 1;
            idViewDesc.arrayLayerCount = 1;
            idViewDesc.aspect = WGPUTextureAspect_All;
            m_primIdView = wgpuTextureCreateView(m_primIdTexture, &idViewDesc);

            RebuildFramebufferBindGroups(ctx.device);
        }

        void HandleEvent(pwgpu::test::SampleContext &ctx,
                         const SDL_Event &event,
                         bool &running) override
        {
            (void)ctx;
            (void)running;
            if (event.type == SDL_MOUSEMOTION)
            {
                m_mouseX = static_cast<float>(event.motion.x);
                m_mouseY = static_cast<float>(event.motion.y);
                m_mouseValid = true;
            }
            else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                     event.key.keysym.sym == SDLK_SPACE)
            {
                m_debugView = !m_debugView;
            }
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect =
                ctx.height > 0
                    ? static_cast<float>(ctx.width) / static_cast<float>(ctx.height)
                    : 1.0f;

            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 2000.f);
            projection[1][1] *= -1.f; // Vulkan Y-down NDC

            const float rad = kPi * static_cast<float>(ctx.timing.elapsedSeconds) / 5.f;
            const vec3 eyeBase(0.f, 12.f, -25.f);
            mat4 rotation = glm::rotate(mat4(1.f), rad, vec3(0.f, 1.f, 0.f));
            vec3 rotatedEye = vec3(rotation * vec4(eyeBase, 1.f));
            mat4 view = glm::lookAtRH(rotatedEye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 viewProj = projection * view;
            mat4 invViewProj = glm::inverse(viewProj);

            // Write VP, invVP, and pickCoord in three chunks so the compute
            // pass's pickedPrimitive write at offset 136 survives across
            // frames. The highlight is therefore one frame behind the cursor,
            // matching upstream exactly.
            wgpuQueueWriteBuffer(ctx.queue, m_frameBuffer, 0,
                                 glm::value_ptr(viewProj), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_frameBuffer, 64,
                                 glm::value_ptr(invViewProj), 64);

            // Poll the mouse. Fall back to the window center if the cursor has
            // never entered the window yet.
            float px = 0.f;
            float py = 0.f;
            if (m_mouseValid)
            {
                px = m_mouseX;
                py = m_mouseY;
            }
            else
            {
                px = static_cast<float>(ctx.width) * 0.5f;
                py = static_cast<float>(ctx.height) * 0.5f;
            }

            const float pickCoord[2] = {px, py};
            wgpuQueueWriteBuffer(ctx.queue, m_frameBuffer, 128, pickCoord,
                                 sizeof(pickCoord));
        }

        bool Execute(pwgpu::test::SampleContext &,
                     pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView || !m_primIdView || !m_forwardBindGroup ||
                !m_debugBindGroup || !m_pickBindGroup)
            {
                return true;
            }

            // 1. Forward rendering pass: color + primitive-id + depth.
            {
                WGPURenderPassColorAttachment colorAttachments[2] = {};
                colorAttachments[0].view = frame.surfaceView;
                colorAttachments[0].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachments[0].loadOp = WGPULoadOp_Clear;
                colorAttachments[0].storeOp = WGPUStoreOp_Store;
                colorAttachments[0].clearValue = {0.0, 0.0, 1.0, 1.0};

                colorAttachments[1].view = m_primIdView;
                colorAttachments[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachments[1].loadOp = WGPULoadOp_Clear;
                colorAttachments[1].storeOp = WGPUStoreOp_Store;
                colorAttachments[1].clearValue = {0.0, 0.0, 0.0, 0.0};

                WGPURenderPassDepthStencilAttachment depthAttachment{};
                depthAttachment.view = m_depthView;
                depthAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthAttachment.depthClearValue = 1.0f;
                depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
                depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"pp_forward_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 2;
                passDesc.colorAttachments = colorAttachments;
                passDesc.depthStencilAttachment = &depthAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_forwardPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_forwardBindGroup, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertexBuffer, 0,
                                                     static_cast<uint64_t>(m_vertexCount) *
                                                         kVertexStrideBytes);
                wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer,
                                                    WGPUIndexFormat_Uint16, 0,
                                                    static_cast<uint64_t>(m_indexCount) *
                                                        sizeof(uint16_t));
                wgpuRenderPassEncoderDrawIndexed(pass, m_indexCount, 1, 0, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 2. Optional debug-view pass: overwrites the surface with the
            // primitive-id visualization.
            if (m_debugView)
            {
                WGPURenderPassColorAttachment colorAttachment{};
                colorAttachment.view = frame.surfaceView;
                colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                colorAttachment.loadOp = WGPULoadOp_Clear;
                colorAttachment.storeOp = WGPUStoreOp_Store;
                colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

                WGPURenderPassDescriptor passDesc{};
                passDesc.label = {"pp_debug_pass", WGPU_STRLEN};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &colorAttachment;

                WGPURenderPassEncoder pass =
                    wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, m_debugPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, m_debugBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }

            // 3. Pick compute pass. Writes the primitive id under the cursor
            // into the Frame buffer. Used next frame for highlighting.
            {
                WGPUComputePassDescriptor passDesc{};
                passDesc.label = {"pp_pick_pass", WGPU_STRLEN};
                WGPUComputePassEncoder pass =
                    wgpuCommandEncoderBeginComputePass(frame.encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, m_pickPipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, m_pickBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }

            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseFramebufferResources();

            if (m_pickPipeline)
                wgpuComputePipelineRelease(m_pickPipeline);
            if (m_debugPipeline)
                wgpuRenderPipelineRelease(m_debugPipeline);
            if (m_forwardPipeline)
                wgpuRenderPipelineRelease(m_forwardPipeline);
            if (m_pickPipelineLayout)
                wgpuPipelineLayoutRelease(m_pickPipelineLayout);
            if (m_debugPipelineLayout)
                wgpuPipelineLayoutRelease(m_debugPipelineLayout);
            if (m_forwardPipelineLayout)
                wgpuPipelineLayoutRelease(m_forwardPipelineLayout);
            if (m_forwardBindGroup)
                wgpuBindGroupRelease(m_forwardBindGroup);
            if (m_pickBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_pickBindGroupLayout);
            if (m_debugBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_debugBindGroupLayout);
            if (m_forwardBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_forwardBindGroupLayout);
            if (m_frameBuffer)
                wgpuBufferRelease(m_frameBuffer);
            if (m_modelBuffer)
                wgpuBufferRelease(m_modelBuffer);
            if (m_indexBuffer)
                wgpuBufferRelease(m_indexBuffer);
            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
            if (m_pickCs)
                wgpuShaderModuleRelease(m_pickCs);
            if (m_debugPs)
                wgpuShaderModuleRelease(m_debugPs);
            if (m_quadVs)
                wgpuShaderModuleRelease(m_quadVs);
            if (m_forwardPs)
                wgpuShaderModuleRelease(m_forwardPs);
            if (m_forwardVs)
                wgpuShaderModuleRelease(m_forwardVs);
        }

    private:
        static constexpr uint32_t kVertexStrideBytes = 6u * sizeof(float); // pos + normal

        // Upstream computeSurfaceNormals: accumulate face normals onto each
        // vertex and normalize. Uses (v1-v0) x (v2-v0) with both edges
        // pre-normalized to match the upstream TypeScript port exactly.
        static void ComputeSurfaceNormals(const float *positions,
                                          uint32_t vertexCount,
                                          const uint16_t *indices,
                                          uint32_t triangleCount,
                                          std::vector<float> &outNormals)
        {
            outNormals.assign(static_cast<size_t>(vertexCount) * 3u, 0.0f);
            auto vsub = [](const float *a, const float *b, float *out)
            {
                out[0] = a[0] - b[0];
                out[1] = a[1] - b[1];
                out[2] = a[2] - b[2];
            };
            auto vnormalize = [](float *v)
            {
                float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (len > 0.f)
                {
                    v[0] /= len;
                    v[1] /= len;
                    v[2] /= len;
                }
            };
            auto vcross = [](const float *a, const float *b, float *out)
            {
                out[0] = a[1] * b[2] - a[2] * b[1];
                out[1] = a[2] * b[0] - a[0] * b[2];
                out[2] = a[0] * b[1] - a[1] * b[0];
            };

            for (uint32_t t = 0; t < triangleCount; ++t)
            {
                uint32_t i0 = indices[t * 3 + 0];
                uint32_t i1 = indices[t * 3 + 1];
                uint32_t i2 = indices[t * 3 + 2];
                const float *p0 = &positions[i0 * 3];
                const float *p1 = &positions[i1 * 3];
                const float *p2 = &positions[i2 * 3];
                float v0[3];
                float v1[3];
                vsub(p1, p0, v0);
                vsub(p2, p0, v1);
                vnormalize(v0);
                vnormalize(v1);
                float n[3];
                vcross(v0, v1, n);
                for (uint32_t k = 0; k < 3; ++k)
                {
                    outNormals[i0 * 3 + k] += n[k];
                    outNormals[i1 * 3 + k] += n[k];
                    outNormals[i2 * 3 + k] += n[k];
                }
            }

            for (uint32_t v = 0; v < vertexCount; ++v)
                vnormalize(&outNormals[v * 3]);
        }

        bool CreateGeometry(pwgpu::test::SampleContext &ctx)
        {
            m_vertexCount = pwgpu::test::teapot::kVertexCount;
            m_indexCount = pwgpu::test::teapot::kIndexCount;
            const uint32_t triangleCount = pwgpu::test::teapot::kTriangleCount;

            std::vector<float> normals;
            ComputeSurfaceNormals(pwgpu::test::teapot::kPositions,
                                  m_vertexCount,
                                  pwgpu::test::teapot::kIndices,
                                  triangleCount,
                                  normals);

            // Interleave position + normal into a single float6 vertex buffer.
            std::vector<float> interleaved(static_cast<size_t>(m_vertexCount) * 6u);
            for (uint32_t i = 0; i < m_vertexCount; ++i)
            {
                interleaved[i * 6 + 0] = pwgpu::test::teapot::kPositions[i * 3 + 0];
                interleaved[i * 6 + 1] = pwgpu::test::teapot::kPositions[i * 3 + 1];
                interleaved[i * 6 + 2] = pwgpu::test::teapot::kPositions[i * 3 + 2];
                interleaved[i * 6 + 3] = normals[i * 3 + 0];
                interleaved[i * 6 + 4] = normals[i * 3 + 1];
                interleaved[i * 6 + 5] = normals[i * 3 + 2];
            }

            const uint64_t vbSize = interleaved.size() * sizeof(float);
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, vbSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst),
                "pp_vb");
            if (!m_vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0, interleaved.data(), vbSize);

            const uint64_t ibByteSize =
                static_cast<uint64_t>(m_indexCount) * sizeof(uint16_t);
            const uint64_t ibSize = (ibByteSize + 3u) & ~static_cast<uint64_t>(3u);
            m_indexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, ibSize,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst),
                "pp_ib");
            if (!m_indexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_indexBuffer, 0,
                                 pwgpu::test::teapot::kIndices, ibByteSize);

            return true;
        }

        bool CreateUniforms(pwgpu::test::SampleContext &ctx)
        {
            m_modelBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(ModelUniforms),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "pp_model_ub");
            m_frameBuffer = pwgpu::test::CreateBuffer(
                ctx.device, sizeof(FrameUniforms),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform |
                                             WGPUBufferUsage_Storage |
                                             WGPUBufferUsage_CopyDst),
                "pp_frame_ub");
            if (!m_modelBuffer || !m_frameBuffer)
                return false;

            // Model matrix is identity (upstream translates by [0,0,0]); normal
            // matrix is the inverse-transpose of the model (also identity).
            ModelUniforms model{};
            mat4 identity = mat4(1.f);
            memcpy(model.modelMatrix, glm::value_ptr(identity), sizeof(model.modelMatrix));
            memcpy(model.normalModelMatrix, glm::value_ptr(identity),
                   sizeof(model.normalModelMatrix));
            wgpuQueueWriteBuffer(ctx.queue, m_modelBuffer, 0, &model, sizeof(model));

            // Zero-init the frame buffer so the first forward pass reads a
            // defined pickedPrimitive (no highlight) before the compute pass
            // has run.
            FrameUniforms frameInit{};
            wgpuQueueWriteBuffer(ctx.queue, m_frameBuffer, 0, &frameInit, sizeof(frameInit));
            return true;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            // Forward BGL: model UBO (vertex) + frame UBO (vertex + fragment).
            {
                WGPUBindGroupLayoutEntry entries[2] = {};
                entries[0].binding = 0;
                entries[0].visibility = WGPUShaderStage_Vertex;
                entries[0].buffer.type = WGPUBufferBindingType_Uniform;
                entries[0].buffer.minBindingSize = sizeof(ModelUniforms);
                entries[1].binding = 1;
                entries[1].visibility = static_cast<WGPUShaderStage>(
                    WGPUShaderStage_Vertex | WGPUShaderStage_Fragment);
                entries[1].buffer.type = WGPUBufferBindingType_Uniform;
                entries[1].buffer.minBindingSize = sizeof(FrameUniforms);

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"pp_forward_bgl", WGPU_STRLEN};
                desc.entryCount = 2;
                desc.entries = entries;
                m_forwardBindGroupLayout =
                    wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_forwardBindGroupLayout)
                    return false;
            }

            // Debug-view BGL: a single uint2D texture.
            {
                WGPUBindGroupLayoutEntry entry{};
                entry.binding = 0;
                entry.visibility = WGPUShaderStage_Fragment;
                entry.texture.sampleType = WGPUTextureSampleType_Uint;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"pp_debug_bgl", WGPU_STRLEN};
                desc.entryCount = 1;
                desc.entries = &entry;
                m_debugBindGroupLayout =
                    wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_debugBindGroupLayout)
                    return false;
            }

            // Pick BGL: storage Frame buffer + uint2D texture.
            {
                WGPUBindGroupLayoutEntry entries[2] = {};
                entries[0].binding = 0;
                entries[0].visibility = WGPUShaderStage_Compute;
                entries[0].buffer.type = WGPUBufferBindingType_Storage;
                entries[0].buffer.minBindingSize = sizeof(FrameUniforms);
                entries[1].binding = 1;
                entries[1].visibility = WGPUShaderStage_Compute;
                entries[1].texture.sampleType = WGPUTextureSampleType_Uint;
                entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

                WGPUBindGroupLayoutDescriptor desc{};
                desc.label = {"pp_pick_bgl", WGPU_STRLEN};
                desc.entryCount = 2;
                desc.entries = entries;
                m_pickBindGroupLayout =
                    wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
                if (!m_pickBindGroupLayout)
                    return false;
            }

            // Build the static forward bind group now (model + frame buffers
            // are both framebuffer-independent).
            {
                WGPUBindGroupEntry entries[2] = {};
                entries[0].binding = 0;
                entries[0].buffer = m_modelBuffer;
                entries[0].size = sizeof(ModelUniforms);
                entries[1].binding = 1;
                entries[1].buffer = m_frameBuffer;
                entries[1].size = sizeof(FrameUniforms);

                WGPUBindGroupDescriptor desc{};
                desc.label = {"pp_forward_bg", WGPU_STRLEN};
                desc.layout = m_forwardBindGroupLayout;
                desc.entryCount = 2;
                desc.entries = entries;
                m_forwardBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
                if (!m_forwardBindGroup)
                    return false;
            }

            m_forwardPipelineLayout = CreatePipelineLayout(
                ctx.device, &m_forwardBindGroupLayout, 1, "pp_forward_pl");
            m_debugPipelineLayout = CreatePipelineLayout(
                ctx.device, &m_debugBindGroupLayout, 1, "pp_debug_pl");
            m_pickPipelineLayout = CreatePipelineLayout(
                ctx.device, &m_pickBindGroupLayout, 1, "pp_pick_pl");
            if (!m_forwardPipelineLayout || !m_debugPipelineLayout || !m_pickPipelineLayout)
                return false;

            // Forward render pipeline: 2 color targets + depth, no culling (the
            // teapot has gaps that expose backfaces in the upstream sample).
            {
                WGPUVertexAttribute attribs[2] = {};
                attribs[0].format = WGPUVertexFormat_Float32x3;
                attribs[0].offset = 0;
                attribs[0].shaderLocation = 0;
                attribs[1].format = WGPUVertexFormat_Float32x3;
                attribs[1].offset = 3 * sizeof(float);
                attribs[1].shaderLocation = 1;

                WGPUVertexBufferLayout vbLayout{};
                vbLayout.arrayStride = kVertexStrideBytes;
                vbLayout.stepMode = WGPUVertexStepMode_Vertex;
                vbLayout.attributeCount = 2;
                vbLayout.attributes = attribs;

                WGPUColorTargetState colorTargets[2] = {};
                colorTargets[0].format = ctx.surfaceFormat;
                colorTargets[0].writeMask = WGPUColorWriteMask_All;
                colorTargets[1].format = WGPUTextureFormat_R32Uint;
                colorTargets[1].writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fragment{};
                fragment.module = m_forwardPs;
                fragment.entryPoint = {"PSMain", WGPU_STRLEN};
                fragment.targetCount = 2;
                fragment.targets = colorTargets;

                WGPUDepthStencilState depth{};
                depth.format = WGPUTextureFormat_Depth24Plus;
                depth.depthWriteEnabled = WGPUOptionalBool_True;
                depth.depthCompare = WGPUCompareFunction_Less;
                depth.stencilFront.compare = WGPUCompareFunction_Always;
                depth.stencilBack.compare = WGPUCompareFunction_Always;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"pp_forward_pipe", WGPU_STRLEN};
                desc.layout = m_forwardPipelineLayout;
                desc.vertex.module = m_forwardVs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 1;
                desc.vertex.buffers = &vbLayout;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = &depth;
                desc.fragment = &fragment;
                m_forwardPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!m_forwardPipeline)
                    return false;
            }

            // Debug view pipeline: fullscreen quad, no depth, no cull.
            {
                WGPUColorTargetState colorTarget{};
                colorTarget.format = ctx.surfaceFormat;
                colorTarget.writeMask = WGPUColorWriteMask_All;

                WGPUFragmentState fragment{};
                fragment.module = m_debugPs;
                fragment.entryPoint = {"PSMain", WGPU_STRLEN};
                fragment.targetCount = 1;
                fragment.targets = &colorTarget;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"pp_debug_pipe", WGPU_STRLEN};
                desc.layout = m_debugPipelineLayout;
                desc.vertex.module = m_quadVs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = nullptr;
                desc.fragment = &fragment;
                m_debugPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!m_debugPipeline)
                    return false;
            }

            // Pick compute pipeline.
            {
                WGPUComputePipelineDescriptor desc{};
                desc.label = {"pp_pick_pipe", WGPU_STRLEN};
                desc.layout = m_pickPipelineLayout;
                desc.compute.module = m_pickCs;
                desc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
                m_pickPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &desc);
                if (!m_pickPipeline)
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

        void RebuildFramebufferBindGroups(WGPUDevice device)
        {
            if (m_debugBindGroup)
            {
                wgpuBindGroupRelease(m_debugBindGroup);
                m_debugBindGroup = nullptr;
            }
            if (m_pickBindGroup)
            {
                wgpuBindGroupRelease(m_pickBindGroup);
                m_pickBindGroup = nullptr;
            }

            if (!m_debugBindGroupLayout || !m_pickBindGroupLayout ||
                !m_primIdView || !m_frameBuffer)
            {
                return;
            }

            {
                WGPUBindGroupEntry entry{};
                entry.binding = 0;
                entry.textureView = m_primIdView;

                WGPUBindGroupDescriptor desc{};
                desc.label = {"pp_debug_bg", WGPU_STRLEN};
                desc.layout = m_debugBindGroupLayout;
                desc.entryCount = 1;
                desc.entries = &entry;
                m_debugBindGroup = wgpuDeviceCreateBindGroup(device, &desc);
            }

            {
                WGPUBindGroupEntry entries[2] = {};
                entries[0].binding = 0;
                entries[0].buffer = m_frameBuffer;
                entries[0].size = sizeof(FrameUniforms);
                entries[1].binding = 1;
                entries[1].textureView = m_primIdView;

                WGPUBindGroupDescriptor desc{};
                desc.label = {"pp_pick_bg", WGPU_STRLEN};
                desc.layout = m_pickBindGroupLayout;
                desc.entryCount = 2;
                desc.entries = entries;
                m_pickBindGroup = wgpuDeviceCreateBindGroup(device, &desc);
            }
        }

        void ReleaseFramebufferResources()
        {
            if (m_debugBindGroup)
            {
                wgpuBindGroupRelease(m_debugBindGroup);
                m_debugBindGroup = nullptr;
            }
            if (m_pickBindGroup)
            {
                wgpuBindGroupRelease(m_pickBindGroup);
                m_pickBindGroup = nullptr;
            }
            if (m_primIdView)
            {
                wgpuTextureViewRelease(m_primIdView);
                m_primIdView = nullptr;
            }
            if (m_primIdTexture)
            {
                wgpuTextureRelease(m_primIdTexture);
                m_primIdTexture = nullptr;
            }
            if (m_depthView)
            {
                wgpuTextureViewRelease(m_depthView);
                m_depthView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }
        }

        WGPUShaderModule m_forwardVs = nullptr;
        WGPUShaderModule m_forwardPs = nullptr;
        WGPUShaderModule m_quadVs = nullptr;
        WGPUShaderModule m_debugPs = nullptr;
        WGPUShaderModule m_pickCs = nullptr;

        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        WGPUBuffer m_modelBuffer = nullptr;
        WGPUBuffer m_frameBuffer = nullptr;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
        WGPUTexture m_primIdTexture = nullptr;
        WGPUTextureView m_primIdView = nullptr;

        WGPUBindGroupLayout m_forwardBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_debugBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_pickBindGroupLayout = nullptr;
        WGPUPipelineLayout m_forwardPipelineLayout = nullptr;
        WGPUPipelineLayout m_debugPipelineLayout = nullptr;
        WGPUPipelineLayout m_pickPipelineLayout = nullptr;

        WGPUBindGroup m_forwardBindGroup = nullptr;
        WGPUBindGroup m_debugBindGroup = nullptr;
        WGPUBindGroup m_pickBindGroup = nullptr;

        WGPURenderPipeline m_forwardPipeline = nullptr;
        WGPURenderPipeline m_debugPipeline = nullptr;
        WGPUComputePipeline m_pickPipeline = nullptr;

        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;

        float m_mouseX = 0.f;
        float m_mouseY = 0.f;
        bool m_mouseValid = false;
        bool m_debugView = false;
    };

    pwgpu::test::SampleAppDesc MakePrimitivePickingDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "PrimitivePickingTest";
        desc.windowTitle = "PhasmaWebGPU Primitive Picking";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        desc.requiredFeatures = {WGPUFeatureName_PrimitiveIndex};
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    PrimitivePickingSample sample;
    pwgpu::test::SampleApp app(sample, MakePrimitivePickingDesc());
    return app.Run(argc, argv);
}
