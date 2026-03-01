#include "RenderGraph.h"
#include "API/Command.h"
#include "API/Helpers.h"

namespace pe
{
    RGBuilder::RGBuilder()
    {
        m_inputs.reserve(16);
        m_outputs.reserve(8);
    }

    void RGBuilder::Barrier(Image *image,
                            vk::ImageLayout layout,
                            vk::PipelineStageFlags2 stageFlags,
                            vk::AccessFlags2 accessMask)
    {
        if (!image)
            return;

        InputInfo info{};
        info.image = image;
        info.layout = layout;
        info.stageFlags = stageFlags;
        info.accessMask = accessMask;
        m_inputs.push_back(info);
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

    void RGBuilder::OutputColor(Image *image)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = vk::ImageLayout::eAttachmentOptimal;
        info.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        info.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        m_outputs.push_back(info);
    }

    void RGBuilder::OutputDepth(Image *image)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = vk::ImageLayout::eAttachmentOptimal;
        info.stageFlags = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
        info.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        m_outputs.push_back(info);
    }

    void RGBuilder::OutputCustom(Image *image, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = layout;
        info.stageFlags = stage;
        info.accessMask = access;
        m_outputs.push_back(info);
    }

    void RGBuilder::Reset()
    {
        m_inputs.clear();
        m_outputs.clear();
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

    void RenderGraph::Compile()
    {
        m_passIO.assign(m_passes.size(), {});
        m_dependencies.assign(m_passes.size(), {});

        std::unordered_map<Image *, size_t> lastWriter;
        std::unordered_set<Image *> seen;

        for (size_t i = 0; i < m_passes.size(); i++)
        {
            auto &pass = m_passes[i];
            if (!pass.component)
                continue;

            m_builderScratch.Reset();
            pass.component->DeclareInputs(m_builderScratch);
            pass.component->DeclareOutputs(m_builderScratch);

            auto &io = m_passIO[i];
            auto &deps = m_dependencies[i];

            // Collect outputs (deduplicated) from explicit outputs and write barriers
            seen.clear();
            for (auto &output : m_builderScratch.m_outputs)
            {
                if (output.image && seen.insert(output.image).second)
                    io.outputs.push_back(output.image);
            }

            for (auto &input : m_builderScratch.m_inputs)
            {
                if (input.image && !VulkanHelpers::IsReadOnlyAccess(input.accessMask))
                {
                    if (seen.insert(input.image).second)
                        io.outputs.push_back(input.image);
                }
            }

            // Collect inputs (deduplicated) from read-only barriers.
            // An image can be both input and output (read then write).
            seen.clear();
            for (auto &input : m_builderScratch.m_inputs)
            {
                if (input.image && VulkanHelpers::IsReadOnlyAccess(input.accessMask))
                {
                    if (seen.insert(input.image).second)
                        io.inputs.push_back(input.image);
                }
            }

            for (auto *img : io.inputs)
            {
                auto it = lastWriter.find(img);
                if (it == lastWriter.end())
                {
                    PE_WARN("RenderGraph: pass '%s' reads image '%s' without a prior producer pass", pass.name.c_str(), img->GetName().c_str());
                    continue;
                }

                deps.push_back(it->second);
            }

            for (auto *img : io.outputs)
                lastWriter[img] = i;
        }
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
        pass.component->DeclareOutputs(builder);

        if (!builder.m_inputs.empty())
            cmd->ImageBarriers(builder.m_inputs);

        pass.component->ExecutePass(cmd);

        // Apply tracked-state fixup for custom outputs (non-attachment).
        // Attachment barriers and tracking are handled by BeginPass/EndPass.
        for (auto &output : builder.m_outputs)
        {
            if (!output.image || output.finalLayout == vk::ImageLayout::eAttachmentOptimal)
                continue;

            ImageTrackInfo fixup{};
            fixup.image = output.image;
            fixup.layout = output.finalLayout;
            fixup.stageFlags = output.stageFlags;
            fixup.accessMask = output.accessMask;
            output.image->SetCurrentInfoAll(fixup);
        }
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
        m_passIO.clear();
        m_dependencies.clear();
    }
} // namespace pe
