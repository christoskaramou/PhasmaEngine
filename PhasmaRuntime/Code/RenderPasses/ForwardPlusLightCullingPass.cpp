#include "ForwardPlusLightCullingPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1u) / divisor;
        }
    } // namespace

    void ForwardPlusLightCullingPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_uniforms.resize(RHII.GetSwapchainImageCount());
        m_tileLightData.resize(RHII.GetSwapchainImageCount());
        m_pointLightIndices.resize(RHII.GetSwapchainImageCount());
        m_spotLightIndices.resize(RHII.GetSwapchainImageCount());
        m_boundLightUniforms.assign(RHII.GetSwapchainImageCount(), nullptr);
        m_boundLightStorage.assign(RHII.GetSwapchainImageCount(), nullptr);
        EnsureBuffers();
    }

    void ForwardPlusLightCullingPass::UpdatePassInfo()
    {
        std::vector<Define> defines{
            Define{"FORWARD_PLUS_TILE_SIZE", std::to_string(TileSize)},
            Define{"FORWARD_PLUS_MAX_LIGHTS_PER_TILE", std::to_string(MaxLightsPerTile)}};

        m_passInfo->name = "ForwardPlusLightCulling_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/ForwardPlusLightCullingCS.hlsl",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = defines});
        m_passInfo->Update();
    }

    void ForwardPlusLightCullingPass::CreateUniforms(CommandBuffer *cmd)
    {
        (void)cmd;

        for (auto &uniform : m_uniforms)
        {
            uniform = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(ForwardPlusLightCullingUBO)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "ForwardPlusLightCulling_uniform_buffer",
            });
            uniform->Map();
            uniform->Zero();
            uniform->Flush();
            uniform->Unmap();
        }

        UpdateDescriptorSets();
    }

    void ForwardPlusLightCullingPass::UpdateDescriptorSets()
    {
        if (m_uniforms.size() != RHII.GetSwapchainImageCount())
            return;

        Scene &scene = *GetActiveScene();
        EnsureBuffers();
        if (!HasLiveResources())
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (descriptors.empty())
                continue;

            Descriptor *set = descriptors[0];
            set->SetBuffer(0, m_tileLightData[i]);
            set->SetBuffer(1, m_pointLightIndices[i]);
            set->SetBuffer(2, m_spotLightIndices[i]);
            set->SetBuffer(3, scene.GetLightUniform(i));
            set->SetBuffer(4, m_uniforms[i]);
            set->SetBuffer(5, scene.GetLightStorage(i));
            set->Update();

            m_boundLightUniforms[i] = scene.GetLightUniform(i);
            m_boundLightStorage[i] = scene.GetLightStorage(i);
        }
    }

    void ForwardPlusLightCullingPass::Update()
    {
        if (!m_viewportRT)
            return;

        EnsureBuffers();

        Scene &scene = *GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();
        if (frame < m_boundLightUniforms.size() &&
            (m_boundLightUniforms[frame] != scene.GetLightUniform(frame) ||
             m_boundLightStorage[frame] != scene.GetLightStorage(frame)))
        {
            UpdateDescriptorSets();
        }

        Camera *camera = scene.GetActiveCamera();
        m_ubo.viewProj = camera->GetViewProjection();
        m_ubo.cameraRight = vec4(camera->GetRight(), 0.0f);
        m_ubo.cameraUp = vec4(camera->GetUp(), 0.0f);
        m_ubo.framebufferAndTiles = vec4(m_viewportRT->GetWidth_f(),
                                         m_viewportRT->GetHeight_f(),
                                         static_cast<float>(m_tileCountX),
                                         static_cast<float>(m_tileCountY));

        BufferRange range{};
        range.data = &m_ubo;
        range.size = sizeof(m_ubo);
        range.offset = 0;
        m_uniforms[frame]->Copy(1, &range, false);
    }

    void ForwardPlusLightCullingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_viewportRT)
            return;

        Scene &scene = *GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();

        std::vector<BufferBarrierInfo> barriers;
        barriers.reserve(6);
        auto addBarrier = [&](Buffer *buffer, PeBarrierSync stage, PeBarrierAccess access)
        {
            if (!buffer)
                return;

            BufferBarrierInfo barrier{};
            barrier.buffer = buffer;
            barrier.stageMask = stage;
            barrier.accessMask = access;
            barrier.size = PE_WHOLE_SIZE;
            barriers.push_back(barrier);
        };

        addBarrier(scene.GetLightUniform(frame), PE_STAGE_COMPUTE_SHADER, PE_ACCESS_UNIFORM_READ);
        addBarrier(m_uniforms[frame], PE_STAGE_COMPUTE_SHADER, PE_ACCESS_UNIFORM_READ);
        addBarrier(scene.GetLightStorage(frame),
                   PE_STAGE_COMPUTE_SHADER,
                   PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ);
        addBarrier(m_tileLightData[frame],
                   PE_STAGE_COMPUTE_SHADER,
                   PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE);
        addBarrier(m_pointLightIndices[frame],
                   PE_STAGE_COMPUTE_SHADER,
                   PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE);
        addBarrier(m_spotLightIndices[frame],
                   PE_STAGE_COMPUTE_SHADER,
                   PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE);
        cmd->BufferBarriers(barriers);

        cmd->BeginDebugRegion("ForwardPlusLightCullingPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->Dispatch(m_tileCountX, m_tileCountY, 1);
        cmd->EndDebugRegion();
    }

    void ForwardPlusLightCullingPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;

        Init();
        UpdateDescriptorSets();
    }

    void ForwardPlusLightCullingPass::Destroy()
    {
        for (auto &uniform : m_uniforms)
            Buffer::Destroy(uniform);
        m_uniforms.clear();
        DestroyBuffers();
        m_boundLightUniforms.clear();
        m_boundLightStorage.clear();
    }

    Buffer *ForwardPlusLightCullingPass::GetTileLightData(uint32_t frame) const
    {
        return frame < m_tileLightData.size() ? m_tileLightData[frame] : nullptr;
    }

    Buffer *ForwardPlusLightCullingPass::GetPointLightIndices(uint32_t frame) const
    {
        return frame < m_pointLightIndices.size() ? m_pointLightIndices[frame] : nullptr;
    }

    Buffer *ForwardPlusLightCullingPass::GetSpotLightIndices(uint32_t frame) const
    {
        return frame < m_spotLightIndices.size() ? m_spotLightIndices[frame] : nullptr;
    }

    bool ForwardPlusLightCullingPass::HasLiveResources() const
    {
        const uint32_t frameCount = RHII.GetSwapchainImageCount();
        if (m_uniforms.size() != frameCount ||
            m_tileLightData.size() != frameCount ||
            m_pointLightIndices.size() != frameCount ||
            m_spotLightIndices.size() != frameCount)
        {
            return false;
        }

        for (uint32_t i = 0; i < frameCount; ++i)
        {
            if (!m_uniforms[i] || !m_tileLightData[i] || !m_pointLightIndices[i] || !m_spotLightIndices[i])
                return false;
        }

        return true;
    }

    void ForwardPlusLightCullingPass::EnsureBuffers()
    {
        if (!m_viewportRT)
            return;

        const uint32_t width = std::max(1u, m_viewportRT->GetWidth());
        const uint32_t height = std::max(1u, m_viewportRT->GetHeight());
        const uint32_t tileCountX = std::max(1u, DivideRoundUp(width, TileSize));
        const uint32_t tileCountY = std::max(1u, DivideRoundUp(height, TileSize));
        const bool dimensionsChanged = width != m_bufferWidth ||
                                       height != m_bufferHeight ||
                                       tileCountX != m_tileCountX ||
                                       tileCountY != m_tileCountY;

        if (!dimensionsChanged && !m_tileLightData.empty() && m_tileLightData[0])
            return;

        DestroyBuffers();

        m_bufferWidth = width;
        m_bufferHeight = height;
        m_tileCountX = tileCountX;
        m_tileCountY = tileCountY;

        const size_t tileCount = TileCount();
        const size_t tileDataSize = tileCount * sizeof(uvec4);
        const size_t lightIndexSize = tileCount * MaxLightsPerTile * sizeof(uint32_t);

        auto createBufferVec = [&](const std::string &name, size_t size)
        {
            std::vector<Buffer *> buffers(RHII.GetSwapchainImageCount());
            for (uint32_t i = 0; i < buffers.size(); ++i)
            {
                buffers[i] = Buffer::Create({
                    .size = size,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                    .name = name + std::to_string(i),
                });
            }
            return buffers;
        };

        m_tileLightData = createBufferVec("forward_plus_tile_light_data_", tileDataSize);
        m_pointLightIndices = createBufferVec("forward_plus_point_light_indices_", lightIndexSize);
        m_spotLightIndices = createBufferVec("forward_plus_spot_light_indices_", lightIndexSize);
    }

    void ForwardPlusLightCullingPass::DestroyBuffers()
    {
        auto destroyBufferVec = [](std::vector<Buffer *> &buffers)
        {
            for (auto &buffer : buffers)
                Buffer::Destroy(buffer);
            buffers.clear();
        };

        destroyBufferVec(m_tileLightData);
        destroyBufferVec(m_pointLightIndices);
        destroyBufferVec(m_spotLightIndices);
    }

    size_t ForwardPlusLightCullingPass::TileCount() const
    {
        return static_cast<size_t>(std::max(1u, m_tileCountX)) *
               static_cast<size_t>(std::max(1u, m_tileCountY));
    }
} // namespace pe
