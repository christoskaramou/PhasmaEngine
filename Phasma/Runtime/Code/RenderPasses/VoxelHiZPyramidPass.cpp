#include "VoxelHiZPyramidPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Scene.h"

namespace pe
{
    namespace
    {
        struct DepthPyramidPushConstants
        {
            uint32_t dstWidth;
            uint32_t dstHeight;
            uint32_t srcWidth;
            uint32_t srcHeight;
        };

        constexpr uint32_t kGroupSize = 8;

        inline uint32_t MipDim(uint32_t base, uint32_t mip)
        {
            return std::max(1u, base >> mip);
        }
    } // namespace

    void VoxelHiZPyramidPass::Init()
    {
        if (!m_copyPassInfo)
            m_copyPassInfo = std::make_shared<PassInfo>();
        AcquireTargets();
    }

    void VoxelHiZPyramidPass::AcquireTargets()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        m_pyramid = rs->GetRenderTarget("voxelHiZ");
        if (!m_pyramid)
            m_pyramid = rs->CreateRenderTarget("voxelHiZ",
                                               PE_FORMAT_R32_SFLOAT,
                                               PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_SAMPLED,
                                               true, // useRenderTargetScale (match depth)
                                               true, // useMips (full Hi-Z chain)
                                               Color::Transparent);

        const uint32_t mips = m_pyramid->GetMipLevels();
        for (uint32_t i = 0; i < mips; ++i)
            if (!m_pyramid->HasUAV(i))
                m_pyramid->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, i);
    }

    void VoxelHiZPyramidPass::UpdatePassInfo()
    {
        // Reduce pipeline: previous pyramid level read as a storage image -> base m_passInfo.
        m_passInfo->name = "VoxelHiZReduce_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/DepthPyramidCS.hlsl",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->Update();

        // Copy pipeline: scene depth sampled into pyramid mip 0.
        m_copyPassInfo->name = "VoxelHiZCopy_pipeline";
        m_copyPassInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/DepthPyramidCS.hlsl",
                                                      .entryPoint = "mainCS",
                                                      .stage = PE_SHADER_STAGE_COMPUTE,
                                                      .defines = std::vector<Define>{Define{"PYRAMID_FROM_DEPTH", "1"}}});
        m_copyPassInfo->Update();
    }

    void VoxelHiZPyramidPass::CreateUniforms(CommandBuffer *cmd)
    {
        (void)cmd;
        UpdateDescriptorSets();
    }

    void VoxelHiZPyramidPass::DestroyDescriptors()
    {
        Descriptor::Destroy(m_copySet); // also nulls the pointer
        for (Descriptor *set : m_reduceSets)
            Descriptor::Destroy(set);
        m_reduceSets.clear();
    }

    void VoxelHiZPyramidPass::UpdateDescriptorSets()
    {
        if (!m_pyramid || !m_depth)
            return;

        const uint32_t mips = m_pyramid->GetMipLevels();
        const uint32_t reduceCount = mips > 0 ? mips - 1 : 0;

        std::vector<DescriptorBindingInfo> copyBindings(2);
        copyBindings[0].binding = 0;
        copyBindings[0].type = PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        copyBindings[0].imageLayout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        copyBindings[1].binding = 1;
        copyBindings[1].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        copyBindings[1].imageLayout = PE_IMAGE_LAYOUT_GENERAL;

        std::vector<DescriptorBindingInfo> reduceBindings(2);
        reduceBindings[0].binding = 0;
        reduceBindings[0].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        reduceBindings[0].imageLayout = PE_IMAGE_LAYOUT_GENERAL;
        reduceBindings[1].binding = 1;
        reduceBindings[1].type = PE_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        reduceBindings[1].imageLayout = PE_IMAGE_LAYOUT_GENERAL;

        if (!m_copySet || m_reduceSets.size() != reduceCount)
        {
            DestroyDescriptors();
            m_copySet = Descriptor::Create(copyBindings, PE_SHADER_STAGE_COMPUTE, false, "VoxelHiZCopy_descriptor");
            m_reduceSets.resize(reduceCount);
            for (uint32_t i = 0; i < reduceCount; ++i)
                m_reduceSets[i] = Descriptor::Create(reduceBindings, PE_SHADER_STAGE_COMPUTE, false,
                                                     "VoxelHiZReduce_descriptor_" + std::to_string(i));
        }

        m_copySet->SetImageView(0, m_depth->GetSRV());
        m_copySet->SetImageView(1, m_pyramid->GetUAV(0));
        m_copySet->Update();

        for (uint32_t i = 0; i < reduceCount; ++i)
        {
            m_reduceSets[i]->SetImageView(0, m_pyramid->GetUAV(i));     // source = mip i
            m_reduceSets[i]->SetImageView(1, m_pyramid->GetUAV(i + 1)); // dest   = mip i + 1
            m_reduceSets[i]->Update();
        }
    }

    void VoxelHiZPyramidPass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_depth);
        builder.WriteCompute(m_pyramid);
    }

    void VoxelHiZPyramidPass::DeclareOutputs(RGBuilder &builder)
    {
        (void)builder;
        // Same rationale as DepthPyramidPass: leave the pyramid in GENERAL/WRITE so next frame's
        // CullingPass ReadCompute emits the real WRITE->READ barrier itself (no pre-marking here).
    }

    void VoxelHiZPyramidPass::ExecutePass(CommandBuffer *cmd)
    {
        // Only voxels need this pyramid; skip the build (and its bandwidth) when none are live.
        if (!m_pyramid || !m_depth || !m_copySet || !m_scene || !m_scene->HasArenaVoxels())
            return;

        const uint32_t mips = m_pyramid->GetMipLevels();
        const uint32_t baseW = m_pyramid->GetWidth();
        const uint32_t baseH = m_pyramid->GetHeight();

        auto dispatch = [&](uint32_t dstW, uint32_t dstH, uint32_t srcW, uint32_t srcH)
        {
            DepthPyramidPushConstants pc{dstW, dstH, srcW, srcH};
            cmd->SetConstants(pc);
            cmd->PushConstants();
            cmd->Dispatch((dstW + kGroupSize - 1) / kGroupSize, (dstH + kGroupSize - 1) / kGroupSize, 1);
        };

        MemoryBarrierInfo uavBarrier{};
        uavBarrier.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        uavBarrier.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        uavBarrier.dstStageMask = PE_STAGE_COMPUTE_SHADER;
        uavBarrier.dstAccessMask = PE_ACCESS_SHADER_READ;

        cmd->BeginDebugRegion("VoxelHiZPyramidPass");

        cmd->BindPipeline(*m_copyPassInfo, false);
        cmd->BindDescriptors(1, &m_copySet);
        dispatch(MipDim(baseW, 0), MipDim(baseH, 0), m_depth->GetWidth(), m_depth->GetHeight());

        if (mips > 1)
            cmd->BindPipeline(*m_passInfo, false);
        for (uint32_t i = 1; i < mips; ++i)
        {
            cmd->MemoryBarrier(uavBarrier);
            cmd->BindDescriptors(1, &m_reduceSets[i - 1]);
            dispatch(MipDim(baseW, i), MipDim(baseH, i), MipDim(baseW, i - 1), MipDim(baseH, i - 1));
        }

        cmd->EndDebugRegion();
    }

    void VoxelHiZPyramidPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        AcquireTargets();
        UpdateDescriptorSets();
    }

    void VoxelHiZPyramidPass::Destroy()
    {
        DestroyDescriptors();
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
        if (m_copyPassInfo)
        {
            Shader::Destroy(m_copyPassInfo->pCompShader);
            m_copyPassInfo.reset();
        }
        if (SceneRendererHost *rs = GetActiveSceneRendererHost())
            rs->DestroyRenderTarget("voxelHiZ");
        m_pyramid = nullptr;
        m_depth = nullptr;
        m_scene = nullptr;
    }

    std::vector<PassInfo *> VoxelHiZPyramidPass::GetPassInfos() noexcept
    {
        return {m_passInfo.get(), m_copyPassInfo.get()};
    }
} // namespace pe
