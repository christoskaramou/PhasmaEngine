#include "RenderGraph.h"
#include "API/Buffer.h"
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

    void RGBuilder::OutputColor(Image *image, bool loads)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
        info.accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
        info.loads = loads;
        m_outputs.push_back(info);
    }

    void RGBuilder::OutputDepth(Image *image, bool loads)
    {
        if (!image)
            return;

        OutputInfo info{};
        info.image = image;
        info.finalLayout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        info.stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
        info.accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
        info.loads = loads;
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

    void RGBuilder::ReadBuffer(Buffer *buffer)
    {
        if (buffer)
            m_bufferReads.push_back(buffer);
    }

    void RGBuilder::WriteBuffer(Buffer *buffer)
    {
        if (buffer)
            m_bufferWrites.push_back(buffer);
    }

    void RGBuilder::Reset()
    {
        m_inputs.clear();
        m_outputs.clear();
        m_bufferReads.clear();
        m_bufferWrites.clear();
    }

    void RenderGraph::AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, IRenderPassComponent *component)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passes.push_back({id, order, std::move(name), std::move(condition), component, nullptr, nullptr});
    }

    void RenderGraph::AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, PassCallback callback,
                              std::function<void(RGBuilder &)> declare)
    {
        PE_ERROR_IF(m_passIndex.find(id) != m_passIndex.end(), "RenderGraph::AddPass duplicate pass id: %u", static_cast<unsigned>(id));
        m_passes.push_back({id, order, std::move(name), std::move(condition), nullptr, std::move(callback), std::move(declare)});
    }

    void RenderGraph::Compile(bool logProblems)
    {
        // Sort passes by order and rebuild index
        std::sort(m_passes.begin(), m_passes.end(),
                  [](const Pass &a, const Pass &b)
                  { return a.order != b.order ? a.order < b.order : a.id < b.id; });
        m_passIndex.clear();
        for (size_t i = 0; i < m_passes.size(); i++)
            m_passIndex[m_passes[i].id] = i;

        m_passIO.assign(m_passes.size(), {});
        m_dependencies.assign(m_passes.size(), {});
        m_problems.clear();

        std::unordered_map<const void *, size_t> lastWriter;
        std::vector<std::pair<size_t, Image *>> loadsBeforeWriter; // attachment loads with no earlier writer
        std::unordered_set<Image *> seen;
        auto problem = [&](const Pass &pass, const void *resource, std::string text)
        {
            if (logProblems)
                PE_WARN("[RenderGraph] %s", text.c_str());
            m_problems.push_back({pass.id, resource, std::move(text)});
        };
        // Resources the current column has touched: the pass that used one last and whether any pass wrote it.
        struct ColumnUse
        {
            size_t pass;
            bool written;
        };
        std::unordered_map<const void *, ColumnUse> columnUse;
        uint32_t columnOrder = 0;
        bool columnStarted = false;

        for (size_t i = 0; i < m_passes.size(); i++)
        {
            auto &pass = m_passes[i];
            if ((!pass.component && !pass.declare) || !pass.condition())
                continue;

            m_builderScratch.Reset();
            if (pass.component)
            {
                pass.component->DeclareInputs(m_builderScratch);
                pass.component->DeclareOutputs(m_builderScratch);
            }
            else
            {
                pass.declare(m_builderScratch);
            }

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
                    problem(pass, img, pass.name + " reads image '" + img->GetName() + "' before any pass writes it");
                    continue;
                }

                deps.push_back(it->second);
            }

            // A loaded attachment keeps what an earlier pass wrote: ordering only, no barrier. With no
            // earlier writer it holds last frame's contents, which is wrong only if a later pass writes it.
            for (const OutputInfo &output : m_builderScratch.m_outputs)
            {
                if (!output.loads || !output.image)
                    continue;
                if (const auto it = lastWriter.find(output.image); it != lastWriter.end())
                    deps.push_back(it->second);
                else
                    loadsBeforeWriter.emplace_back(i, output.image);
                if (std::find(io.inputs.begin(), io.inputs.end(), output.image) == io.inputs.end())
                    io.inputs.push_back(output.image);
            }

            for (Buffer *buffer : m_builderScratch.m_bufferReads)
            {
                if (!lastWriter.contains(buffer))
                    problem(pass, buffer, pass.name + " reads buffer '" + buffer->GetName() + "' before any pass writes it");
            }

            for (const OutputInfo &output : m_builderScratch.m_outputs)
            {
                const auto sampled = [&](const InputInfo &input)
                { return input.image == output.image && input.layout == PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; };
                if (output.finalLayout == PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL &&
                    std::any_of(m_builderScratch.m_inputs.begin(), m_builderScratch.m_inputs.end(), sampled))
                    problem(pass, output.image, pass.name + " samples image '" + output.image->GetName() + "' while rendering into it");
            }

            if (!columnStarted || pass.order != columnOrder)
            {
                columnUse.clear();
                columnOrder = pass.order;
                columnStarted = true;
            }
            auto useInColumn = [&](const void *resource, const std::string &name, bool writes)
            {
                auto [it, inserted] = columnUse.try_emplace(resource, ColumnUse{i, writes});
                if (inserted)
                    return;
                if (it->second.pass != i && (writes || it->second.written))
                    problem(pass, resource, pass.name + " and " + m_passes[it->second.pass].name + " share '" + name + "' in one column, and one of them writes it");
                it->second.pass = i;
                it->second.written = it->second.written || writes;
            };
            for (Image *img : io.inputs)
                useInColumn(img, img->GetName(), false);
            for (Image *img : io.outputs)
                useInColumn(img, img->GetName(), true);
            for (Buffer *buffer : m_builderScratch.m_bufferReads)
                useInColumn(buffer, buffer->GetName(), false);
            for (Buffer *buffer : m_builderScratch.m_bufferWrites)
                useInColumn(buffer, buffer->GetName(), true);

            io.bufferReads = m_builderScratch.m_bufferReads;
            io.bufferWrites = m_builderScratch.m_bufferWrites;
            for (auto *img : io.outputs)
                lastWriter[img] = i;
            for (Buffer *buffer : m_builderScratch.m_bufferWrites)
                lastWriter[buffer] = i;
        }
        for (const auto &[index, image] : loadsBeforeWriter)
            for (size_t later = index + 1; later < m_passes.size(); ++later)
            {
                const std::vector<Image *> &outputs = m_passIO[later].outputs;
                if (std::find(outputs.begin(), outputs.end(), image) == outputs.end())
                    continue;
                problem(m_passes[index], image, m_passes[index].name + " loads image '" + image->GetName() + "' before " + m_passes[later].name + " writes it");
                break;
            }

        // A script (callback) pass that declares nothing records whatever it touches out of the graph's sight, so it runs alone.
        for (size_t begin = 0; begin < m_passes.size();)
        {
            size_t end = begin + 1;
            while (end < m_passes.size() && m_passes[end].order == m_passes[begin].order)
                ++end;
            const Pass *script = nullptr;
            const Pass *other = nullptr;
            for (size_t i = begin; i < end; ++i)
            {
                const Pass &candidate = m_passes[i];
                if (!candidate.condition || !candidate.condition())
                    continue;
                if (candidate.callback && !candidate.declare && !script)
                    script = &candidate;
                else if (!other)
                    other = &candidate;
            }
            if (script && other)
                problem(*script, nullptr, script->name + " shares a column with " + other->name + "; script passes run alone");
            begin = end;
        }
    }

    void RenderGraph::ExecuteSinglePass(CommandBuffer *cmd, Pass &pass, RGBuilder &builder, bool issueInputBarriers)
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
            if (pass.declare && issueInputBarriers)
            {
                builder.Reset();
                pass.declare(builder);
                if (!builder.m_inputs.empty())
                    cmd->ImageBarriers(builder.m_inputs);
            }
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
        if (issueInputBarriers)
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

    void RenderGraph::ExecuteColumn(CommandBuffer *cmd, size_t begin, size_t end)
    {
        // The column's passes do not depend on each other (Compile reports any that do), so one barrier
        // batch covers all their inputs and the GPU can overlap them.
        m_columnBarriers.clear();
        for (size_t i = begin; i < end; ++i)
        {
            Pass &pass = m_passes[i];
            if ((!pass.component && !pass.declare) || !pass.condition())
                continue;
            m_builderScratch.Reset();
            if (pass.component)
                pass.component->DeclareInputs(m_builderScratch);
            else
                pass.declare(m_builderScratch);
            for (const InputInfo &input : m_builderScratch.m_inputs)
            {
                auto same = std::find_if(m_columnBarriers.begin(), m_columnBarriers.end(), [&](const InputInfo &b)
                                         { return b.image == input.image && b.layout == input.layout && b.baseArrayLayer == input.baseArrayLayer &&
                                                  b.arrayLayers == input.arrayLayers && b.baseMipLevel == input.baseMipLevel && b.mipLevels == input.mipLevels; });
                if (same == m_columnBarriers.end())
                {
                    m_columnBarriers.push_back(input);
                }
                else
                {
                    same->stageFlags = same->stageFlags | input.stageFlags;
                    same->accessMask = same->accessMask | input.accessMask;
                }
            }
        }
        if (!m_columnBarriers.empty())
        {
            PE_PROFILE_SCOPE("RG Column ImageBarriers");
            cmd->ImageBarriers(m_columnBarriers);
        }
        for (size_t i = begin; i < end; ++i)
            ExecuteSinglePass(cmd, m_passes[i], m_builderScratch, false);
    }

    void RenderGraph::Execute(CommandBuffer *cmd)
    {
        for (size_t begin = 0; begin < m_passes.size();)
        {
            size_t end = begin + 1;
            while (end < m_passes.size() && m_passes[end].order == m_passes[begin].order)
                ++end;
            if (end - begin == 1)
                ExecuteSinglePass(cmd, m_passes[begin], m_builderScratch);
            else
                ExecuteColumn(cmd, begin, end);
            begin = end;
        }
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

    void RenderGraph::SetPassOrders(const std::unordered_map<PassID, uint32_t> &orders)
    {
        for (Pass &pass : m_passes)
            if (const auto it = orders.find(pass.id); it != orders.end())
                pass.order = it->second;
    }

    std::vector<RenderGraph::Problem> RenderGraph::NewProblems(const std::unordered_map<PassID, uint32_t> &orders) const
    {
        // Compile both orders fresh: pass conditions may have changed since this graph compiled.
        RenderGraph current;
        RenderGraph moved;
        current.m_passes = moved.m_passes = m_passes;
        moved.SetPassOrders(orders);
        current.Compile(false);
        moved.Compile(false);
        std::vector<Problem> added;
        for (const Problem &found : moved.m_problems)
        {
            const bool alreadyThere = std::any_of(current.m_problems.begin(), current.m_problems.end(), [&](const Problem &p)
                                                  { return p.pass == found.pass && p.resource == found.resource; });
            if (!alreadyThere)
                added.push_back(found);
        }
        return added;
    }

    void RenderGraph::Clear()
    {
        m_passes.clear();
        m_passIndex.clear();
        m_passIO.clear();
        m_dependencies.clear();
    }
} // namespace pe
