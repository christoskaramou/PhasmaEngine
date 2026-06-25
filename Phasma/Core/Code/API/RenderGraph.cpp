#include "RenderGraph.h"
#include "API/Command.h"

namespace pe
{
    RGBuilder::RGBuilder()
    {
        m_inputs.reserve(16);
        m_outputs.reserve(8);
    }

    void RGBuilder::Barrier(Image *image,
                            PeImageLayout layout,
                            PeBarrierSync stageFlags,
                            PeBarrierAccess accessMask)
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
        Barrier(image, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_FRAGMENT_SHADER, PE_ACCESS_SHADER_SAMPLED_READ);
    }

    void RGBuilder::ReadCompute(Image *image)
    {
        Barrier(image, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_SAMPLED_READ);
    }

    void RGBuilder::WriteCompute(Image *image)
    {
        Barrier(image, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_STORAGE_WRITE);
    }

    void RGBuilder::ReadRayTracing(Image *image)
    {
        Barrier(image, PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, PE_STAGE_RAY_TRACING_SHADER_KHR, PE_ACCESS_SHADER_SAMPLED_READ);
    }

    void RGBuilder::WriteRayTracing(Image *image)
    {
        Barrier(image, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_RAY_TRACING_SHADER_KHR, PE_ACCESS_SHADER_STORAGE_WRITE);
    }

    void RGBuilder::OutputColor(Image *image)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
        info.accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
        m_outputs.push_back(info);
    }

    void RGBuilder::OutputDepth(Image *image)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
        info.accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
        m_outputs.push_back(info);
    }

    void RGBuilder::OutputCustom(Image *image, PeImageLayout layout, PeBarrierSync stage, PeBarrierAccess access)
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

    void RenderGraph::AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, IRenderPassComponent *component)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passes.push_back({id, order, std::move(name), std::move(condition), component, nullptr});
    }

    void RenderGraph::AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, PassCallback callback)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passes.push_back({id, order, std::move(name), std::move(condition), nullptr, std::move(callback)});
    }

    void RenderGraph::Compile()
    {
        // Sort passes by order and rebuild index
        std::sort(m_passes.begin(), m_passes.end(),
                  [](const Pass &a, const Pass &b)
                  { return a.order < b.order; });
        m_passIndex.clear();
        for (size_t i = 0; i < m_passes.size(); i++)
            m_passIndex[m_passes[i].id] = i;

        m_passIO.assign(m_passes.size(), {});
        m_dependencies.assign(m_passes.size(), {});

        std::unordered_map<Image *, size_t> lastWriter;
        std::unordered_set<Image *> seen;

        for (size_t i = 0; i < m_passes.size(); i++)
        {
            auto &pass = m_passes[i];
            if (!pass.component || !pass.condition())
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
                if (input.image && !IsReadOnlyAccess(input.accessMask))
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
                if (input.image && IsReadOnlyAccess(input.accessMask))
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
                    PE_WARN("[RenderGraph] pass '%s' reads image '%s' without a prior producer pass", pass.name.c_str(), img->GetName().c_str());
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

        // RAII scopes: both our profiler and Tracy end automatically on early returns
        CpuProfileScope peScope(pass.name.c_str());
#ifdef PE_TRACY
        ZoneScoped;
        ZoneName(pass.name.c_str(), pass.name.size());
#endif

        if (pass.callback)
        {
            PE_PROFILE_COUNTER("RG Callback Passes", 1);
            PE_PROFILE_SCOPE("RG Callback");
            pass.callback(cmd);
            return;
        }

        if (!pass.component)
            return;

        {
            PE_PROFILE_SCOPE("RG Builder Reset");
            builder.Reset();
        }
        {
            PE_PROFILE_SCOPE("RG DeclareInputs");
            pass.component->DeclareInputs(builder);
        }
        {
            PE_PROFILE_SCOPE("RG DeclareOutputs");
            pass.component->DeclareOutputs(builder);
        }
        PE_PROFILE_COUNTER("RG Component Passes", 1);
        PE_PROFILE_COUNTER("RG Input Image Count", builder.m_inputs.size());
        PE_PROFILE_COUNTER("RG Output Image Count", builder.m_outputs.size());

        if (!builder.m_inputs.empty())
        {
            PE_PROFILE_SCOPE("RG Input ImageBarriers");
            cmd->ImageBarriers(builder.m_inputs);
        }

        {
            PE_PROFILE_SCOPE("RG ExecutePass");
            pass.component->ExecutePass(cmd);
        }

        // Apply tracked-state fixup for custom outputs (non-attachment).
        // Attachment barriers and tracking are handled by BeginPass/EndPass.
        {
            PE_PROFILE_SCOPE("RG Output Fixup");
            for (auto &output : builder.m_outputs)
            {
                if (!output.image || output.finalLayout == PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL)
                    continue;

                ImageTrackInfo fixup{};
                fixup.image = output.image;
                fixup.layout = output.finalLayout;
                fixup.stageFlags = output.stageFlags;
                fixup.accessMask = output.accessMask;
                output.image->SetCurrentInfoAll(fixup);
            }
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
