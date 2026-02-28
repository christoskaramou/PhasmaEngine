#include "RenderGraph.h"
#include "API/Command.h"

namespace pe
{
    void RGBuilder::Barrier(Image *image,
                            vk::ImageLayout layout,
                            vk::PipelineStageFlags2 stageFlags,
                            vk::AccessFlags2 accessMask)
    {
        if (!image)
            return;

        ImageBarrierInfo info{};
        info.image = image;
        info.layout = layout;
        info.stageFlags = stageFlags;
        info.accessMask = accessMask;
        m_barriers.push_back(info);
    }

    void RGBuilder::Read(Image *image)
    {
        Barrier(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderRead);
    }

    void RGBuilder::ReadCompute(Image *image)
    {
        Barrier(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
    }

    void RGBuilder::WriteCompute(Image *image)
    {
        Barrier(image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite);
    }

    void RGBuilder::ReadRayTracing(Image *image)
    {
        Barrier(image, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderRead);
    }

    void RGBuilder::WriteRayTracing(Image *image)
    {
        Barrier(image, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderWrite);
    }

    void RenderGraph::AddPass(PassID id, std::string name, std::function<bool()> condition, IRenderPassComponent *component)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passIndex[id] = m_passes.size();
        m_passes.push_back({id, std::move(name), std::move(condition), component, nullptr});
    }

    void RenderGraph::AddPass(PassID id, std::string name, std::function<bool()> condition, PassCallback callback)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passIndex[id] = m_passes.size();
        m_passes.push_back({id, std::move(name), std::move(condition), nullptr, std::move(callback)});
    }

    void RenderGraph::ExecuteSinglePass(CommandBuffer *cmd, Pass &pass, RGBuilder &builder)
    {
        if (!pass.condition())
            return;

        if (pass.callback)
        {
            pass.callback(cmd);
            return;
        }

        if (!pass.component)
            return;

        builder.Reset();
        pass.component->DeclareInputs(builder);

        if (!builder.m_barriers.empty())
            cmd->ImageBarriers(builder.m_barriers);

        pass.component->ExecutePass(cmd);
    }

    void RenderGraph::Execute(CommandBuffer *cmd)
    {
        for (auto &pass : m_passes)
            ExecuteSinglePass(cmd, pass, m_builderScratch);
    }

    size_t RenderGraph::FindPassIndex(PassID passID) const
    {
        auto it = m_passIndex.find(passID);
        return it != m_passIndex.end() ? it->second : m_passes.size();
    }

    bool RenderGraph::ContainsPass(PassID passID) const
    {
        return FindPassIndex(passID) != m_passes.size();
    }

    void RenderGraph::Clear()
    {
        m_passes.clear();
        m_passIndex.clear();
    }
} // namespace pe
