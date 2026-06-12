#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Buffer.h"
#include "API/Queue.h"
#include "API/RenderPass.h"
#include "API/Framebuffer.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeImageLayout> s_imageLayoutMap = {
        {"undefined", PE_IMAGE_LAYOUT_UNDEFINED},
        {"general", PE_IMAGE_LAYOUT_GENERAL},
        {"color_attachment", PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {"depth_attachment", PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        {"shader_read", PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {"transfer_src", PE_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
        {"transfer_dst", PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
        {"present", PE_IMAGE_LAYOUT_PRESENT_SRC},
        {"attachment", PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL},
        {"depth_stencil_read_only", PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL},
        {"depth_attachment_stencil_read_only", PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL},
        {"depth_read_only_stencil_attachment", PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL},
        {"depth_read_only", PE_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL},
        {"depth_attachment_only", PE_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL},
        {"stencil_read_only", PE_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL},
        {"stencil_attachment", PE_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL},
    };

    static const std::unordered_map<std::string_view, PeBarrierSync> s_pipelineStageMap = {
        {"none", PE_STAGE_NONE},
        {"top_of_pipe", PE_STAGE_TOP_OF_PIPE},
        {"vertex_input", PE_STAGE_VERTEX_INPUT},
        {"vertex", PE_STAGE_VERTEX_SHADER},
        {"fragment", PE_STAGE_FRAGMENT_SHADER},
        {"early_fragment", PE_STAGE_EARLY_FRAGMENT_TESTS},
        {"late_fragment", PE_STAGE_LATE_FRAGMENT_TESTS},
        {"color_output", PE_STAGE_COLOR_ATTACHMENT_OUTPUT},
        {"compute", PE_STAGE_COMPUTE_SHADER},
        {"transfer", PE_STAGE_TRANSFER},
        {"clear", PE_STAGE_CLEAR},
        {"copy", PE_STAGE_COPY},
        {"host", PE_STAGE_HOST},
        {"draw_indirect", PE_STAGE_DRAW_INDIRECT},
        {"index_input", PE_STAGE_INDEX_INPUT},
        {"vertex_attribute_input", PE_STAGE_VERTEX_ATTRIBUTE_INPUT},
        {"ray_tracing", PE_STAGE_RAY_TRACING_SHADER_KHR},
        {"acceleration_structure_build", PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR},
        {"bottom_of_pipe", PE_STAGE_BOTTOM_OF_PIPE},
        {"all_graphics", PE_STAGE_ALL_GRAPHICS},
        {"all_commands", PE_STAGE_ALL_COMMANDS},
    };

    static const std::unordered_map<std::string_view, PeBarrierAccess> s_accessFlagsMap = {
        {"none", PE_ACCESS_NONE},
        {"shader_read", PE_ACCESS_SHADER_READ},
        {"shader_write", PE_ACCESS_SHADER_WRITE},
        {"shader_read_write", PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE},
        {"sampled_read", PE_ACCESS_SHADER_SAMPLED_READ},
        {"uniform_read", PE_ACCESS_UNIFORM_READ},
        {"storage_read", PE_ACCESS_SHADER_STORAGE_READ},
        {"storage_write", PE_ACCESS_SHADER_STORAGE_WRITE},
        {"color_read", PE_ACCESS_COLOR_ATTACHMENT_READ},
        {"color_write", PE_ACCESS_COLOR_ATTACHMENT_WRITE},
        {"depth_read", PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ},
        {"depth_write", PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE},
        {"transfer_read", PE_ACCESS_TRANSFER_READ},
        {"transfer_write", PE_ACCESS_TRANSFER_WRITE},
        {"host_write", PE_ACCESS_HOST_WRITE},
        {"memory_read", PE_ACCESS_MEMORY_READ},
        {"memory_write", PE_ACCESS_MEMORY_WRITE},
        {"index_read", PE_ACCESS_INDEX_READ},
        {"vertex_read", PE_ACCESS_VERTEX_ATTRIBUTE_READ},
        {"indirect_read", PE_ACCESS_INDIRECT_COMMAND_READ},
        {"acceleration_structure_read", PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR},
        {"acceleration_structure_write", PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR},
    };

    static const std::unordered_map<std::string_view, PeLoadOp> s_loadOpMap = {
        {"load", PE_LOAD_OP_LOAD},
        {"clear", PE_LOAD_OP_CLEAR},
        {"dont_care", PE_LOAD_OP_DONT_CARE},
    };

    static const std::unordered_map<std::string_view, PeStoreOp> s_storeOpMap = {
        {"store", PE_STORE_OP_STORE},
        {"dont_care", PE_STORE_OP_DONT_CARE},
    };

    static const std::unordered_map<std::string_view, PeImageAspectFlags> s_aspectMaskMap = {
        {"color", PE_IMAGE_ASPECT_COLOR},
        {"depth", PE_IMAGE_ASPECT_DEPTH},
        {"stencil", PE_IMAGE_ASPECT_STENCIL},
        {"depth_stencil", PE_IMAGE_ASPECT_DEPTH | PE_IMAGE_ASPECT_STENCIL},
    };

    static PeImageLayout ToImageLayout(const std::string &s)
    {
        return Lookup(s, s_imageLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED);
    }
    static PeBarrierSync ToPipelineStage(const std::string &s)
    {
        return Lookup(s, s_pipelineStageMap);
    }
    static PeBarrierAccess ToAccessFlags(const std::string &s)
    {
        return Lookup(s, s_accessFlagsMap);
    }
    static PeImageAspectFlags ToAspectMask(const std::string &s)
    {
        return Lookup(s, s_aspectMaskMap, PE_IMAGE_ASPECT_COLOR);
    }

    static struct CommandBindings
    {
        CommandBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<CommandBuffer> cmdType = lua.new_usertype<CommandBuffer>("CommandBuffer", sol::no_constructor);

                // Lifecycle
                cmdType["begin"] = &CommandBuffer::Begin;
                cmdType["end_cmd"] = &CommandBuffer::End;
                cmdType["reset"] = &CommandBuffer::Reset;

                // Blit: blit_image(src, dst, filter, aspect)
                // aspect: "color" (default), "depth", "stencil", "depth_stencil"
                cmdType["blit_image"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> src, std::shared_ptr<LuaImage> dst,
                                           const std::string &filter, sol::optional<std::string> aspect) {
                    Image *s = src->Get();
                    Image *d = dst->Get();
                    if (!s || !d) return;
                    PeImageAspectFlags aspectMask = aspect ? ToAspectMask(*aspect) : PE_IMAGE_ASPECT_COLOR;
                    ImageBlit region{};
                    region.srcOffsets[1] = {static_cast<int32_t>(s->GetWidth()), static_cast<int32_t>(s->GetHeight()), 1};
                    region.srcSubresource.aspectMask = aspectMask;
                    region.srcSubresource.layerCount = 1;
                    region.dstOffsets[1] = {static_cast<int32_t>(d->GetWidth()), static_cast<int32_t>(d->GetHeight()), 1};
                    region.dstSubresource.aspectMask = aspectMask;
                    region.dstSubresource.layerCount = 1;
                    cmd.BlitImage(s, d, region, filter == "nearest" ? PE_FILTER_NEAREST : PE_FILTER_LINEAR);
                };

                // Clear
                cmdType["clear_color"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img) {
                    Image *p = img->Get();
                    if (p) cmd.ClearColors({p});
                };
                cmdType["clear_colors"] = [](CommandBuffer &cmd, sol::table images) {
                    std::vector<Image *> imgs;
                    imgs.reserve(images.size());
                    for (auto &[k, v] : images)
                    {
                        if (!v.is<std::shared_ptr<LuaImage>>()) continue;
                        Image *p = v.as<std::shared_ptr<LuaImage>>()->Get();
                        if (p) imgs.push_back(p);
                    }
                    if (!imgs.empty()) cmd.ClearColors(imgs);
                };
                cmdType["clear_depth_stencil"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img) {
                    Image *p = img->Get();
                    if (p) cmd.ClearDepthStencils({p});
                };
                cmdType["clear_depth_stencils"] = [](CommandBuffer &cmd, sol::table images) {
                    std::vector<Image *> imgs;
                    imgs.reserve(images.size());
                    for (auto &[k, v] : images)
                    {
                        if (!v.is<std::shared_ptr<LuaImage>>()) continue;
                        Image *p = v.as<std::shared_ptr<LuaImage>>()->Get();
                        if (p) imgs.push_back(p);
                    }
                    if (!imgs.empty()) cmd.ClearDepthStencils(imgs);
                };

                // Render pass: {{image, load_op, store_op, stencil_load_op, stencil_store_op}, ...}
                // Minimal: {{image}} uses defaults (clear/store/dont_care/dont_care)
                cmdType["begin_pass"] = [](CommandBuffer &cmd, sol::table attachments, const std::string &name, sol::optional<bool> skipDynamic) {
                    static thread_local std::vector<Attachment> atts;
                    atts.clear();
                    atts.reserve(attachments.size());
                    for (size_t i = 1; i <= attachments.size(); i++)
                    {
                        sol::table entry = attachments.get<sol::table>(i);
                        Attachment att{};
                        auto img = entry.get<std::shared_ptr<LuaImage>>(1);
                        att.image = img ? img->Get() : nullptr;
                        if (!att.image) continue;
                        if (entry.size() >= 2)
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, PE_LOAD_OP_CLEAR);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, PE_STORE_OP_STORE);
                        if (entry.size() >= 4)
                            att.stencilLoadOp = Lookup(entry.get<std::string>(4), s_loadOpMap, PE_LOAD_OP_DONT_CARE);
                        if (entry.size() >= 5)
                            att.stencilStoreOp = Lookup(entry.get<std::string>(5), s_storeOpMap, PE_STORE_OP_DONT_CARE);
                        atts.push_back(att);
                    }
                    if (!atts.empty())
                        cmd.BeginPass(static_cast<uint32_t>(atts.size()), atts.data(), name, skipDynamic.value_or(false));
                };
                cmdType["end_pass"] = &CommandBuffer::EndPass;

                // Pipeline
                cmdType["bind_pipeline"] = sol::overload(
                    [](CommandBuffer &cmd, PassInfo &pi) { cmd.BindPipeline(pi); },
                    [](CommandBuffer &cmd, PassInfo &pi, bool bindDesc) { cmd.BindPipeline(pi, bindDesc); });

                // Vertex / Index buffers
                cmdType["bind_vertex_buffer"] = sol::overload(
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset) { cmd.BindVertexBuffer(&buf, offset); },
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t firstBinding) { cmd.BindVertexBuffer(&buf, offset, firstBinding); },
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t firstBinding, uint32_t bindingCount) { cmd.BindVertexBuffer(&buf, offset, firstBinding, bindingCount); });
                cmdType["bind_index_buffer"] = [](CommandBuffer &cmd, Buffer &buf, size_t offset) {
                    cmd.BindIndexBuffer(&buf, offset);
                };

                // Descriptors
                cmdType["bind_descriptors"] = [](CommandBuffer &cmd, sol::variadic_args va) {
                    std::vector<Descriptor *> descs;
                    descs.reserve(va.size());
                    for (auto v : va)
                    {
                        if (v.is<Descriptor *>())
                            descs.push_back(v.as<Descriptor *>());
                    }
                    if (!descs.empty())
                        cmd.BindDescriptors(static_cast<uint32_t>(descs.size()), descs.data());
                };

                // Viewport / Scissor
                cmdType["set_viewport"] = &CommandBuffer::SetViewport;
                cmdType["set_scissor"] = &CommandBuffer::SetScissor;

                // Dynamic state
                cmdType["set_line_width"] = &CommandBuffer::SetLineWidth;
                cmdType["set_depth_bias"] = &CommandBuffer::SetDepthBias;
                cmdType["set_depth_test_enable"] = &CommandBuffer::SetDepthTestEnable;
                cmdType["set_depth_write_enable"] = &CommandBuffer::SetDepthWriteEnable;

                // Dispatch / Compute
                cmdType["dispatch"] = &CommandBuffer::Dispatch;

                // Push constants
                cmdType["set_constant_float"] = [](CommandBuffer &cmd, uint16_t offset, float v) { cmd.SetConstantAt(offset, v); };
                cmdType["set_constant_int"] = [](CommandBuffer &cmd, uint16_t offset, int32_t v) { cmd.SetConstantAt(offset, v); };
                cmdType["set_constant_uint"] = [](CommandBuffer &cmd, uint16_t offset, uint32_t v) { cmd.SetConstantAt(offset, v); };
                cmdType["set_constant_vec2"] = [](CommandBuffer &cmd, uint16_t offset, float x, float y) {
                    cmd.SetConstantAt(offset, vec2(x, y));
                };
                cmdType["set_constant_vec3"] = [](CommandBuffer &cmd, uint16_t offset, float x, float y, float z) {
                    cmd.SetConstantAt(offset, vec3(x, y, z));
                };
                cmdType["set_constant_vec4"] = [](CommandBuffer &cmd, uint16_t offset, float x, float y, float z, float w) {
                    cmd.SetConstantAt(offset, vec4(x, y, z, w));
                };
                cmdType["set_constant_mat4"] = [](CommandBuffer &cmd, uint16_t offset, const mat4 &m) {
                    cmd.SetConstantAt(offset, m);
                };
                cmdType["push_constants"] = &CommandBuffer::PushConstants;

                // Draw
                cmdType["draw"] = &CommandBuffer::Draw;
                cmdType["draw_indexed"] = &CommandBuffer::DrawIndexed;
                cmdType["draw_indirect"] = sol::overload(
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t drawCount) {
                        cmd.DrawIndirect(&buf, offset, drawCount);
                    },
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t drawCount, uint32_t stride) {
                        cmd.DrawIndirect(&buf, offset, drawCount, stride);
                    });
                cmdType["draw_indexed_indirect"] = sol::overload(
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t drawCount) {
                        cmd.DrawIndexedIndirect(&buf, offset, drawCount);
                    },
                    [](CommandBuffer &cmd, Buffer &buf, size_t offset, uint32_t drawCount, uint32_t stride) {
                        cmd.DrawIndexedIndirect(&buf, offset, drawCount, stride);
                    });

                // Ray tracing
                cmdType["trace_rays"] = &CommandBuffer::TraceRays;

                // Barriers (single)
                cmdType["buffer_barrier"] = [](CommandBuffer &cmd, Buffer &buf, const std::string &stage, const std::string &access) {
                    BufferBarrierInfo info{};
                    info.buffer = &buf;
                    info.stageMask = ToPipelineStage(stage);
                    info.accessMask = ToAccessFlags(access);
                    cmd.BufferBarrier(info);
                };
                cmdType["image_barrier"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img,
                                               const std::string &layout, const std::string &stage, const std::string &access) {
                    Image *p = img->Get();
                    if (!p) return;
                    ImageBarrierInfo barrier{};
                    barrier.image = p;
                    barrier.layout = ToImageLayout(layout);
                    barrier.stageFlags = ToPipelineStage(stage);
                    barrier.accessMask = ToAccessFlags(access);
                    cmd.ImageBarrier(barrier);
                };
                cmdType["memory_barrier"] = [](CommandBuffer &cmd,
                                                const std::string &srcStage, const std::string &srcAccess,
                                                const std::string &dstStage, const std::string &dstAccess) {
                    MemoryBarrierInfo barrier{};
                    barrier.srcStageMask = ToPipelineStage(srcStage);
                    barrier.srcAccessMask = ToAccessFlags(srcAccess);
                    barrier.dstStageMask = ToPipelineStage(dstStage);
                    barrier.dstAccessMask = ToAccessFlags(dstAccess);
                    cmd.MemoryBarrier(barrier);
                };

                // Barriers (batch)
                cmdType["buffer_barriers"] = [](CommandBuffer &cmd, sol::table entries) {
                    std::vector<BufferBarrierInfo> barriers;
                    barriers.reserve(entries.size());
                    for (auto &[k, v] : entries)
                    {
                        if (!v.is<sol::table>()) continue;
                        sol::table entry = v.as<sol::table>();
                        auto *buf = entry.get<Buffer *>(1);
                        if (!buf) continue;
                        BufferBarrierInfo b{};
                        b.buffer = buf;
                        b.stageMask = ToPipelineStage(entry.get<std::string>(2));
                        b.accessMask = ToAccessFlags(entry.get<std::string>(3));
                        barriers.push_back(b);
                    }
                    if (!barriers.empty())
                        cmd.BufferBarriers(barriers);
                };
                cmdType["image_barriers"] = [](CommandBuffer &cmd, sol::table entries) {
                    std::vector<ImageBarrierInfo> barriers;
                    barriers.reserve(entries.size());
                    for (auto &[k, v] : entries)
                    {
                        if (!v.is<sol::table>()) continue;
                        sol::table entry = v.as<sol::table>();
                        auto img = entry.get<std::shared_ptr<LuaImage>>(1);
                        Image *p = img ? img->Get() : nullptr;
                        if (!p) continue;
                        ImageBarrierInfo b{};
                        b.image = p;
                        b.layout = ToImageLayout(entry.get<std::string>(2));
                        b.stageFlags = ToPipelineStage(entry.get<std::string>(3));
                        b.accessMask = ToAccessFlags(entry.get<std::string>(4));
                        barriers.push_back(b);
                    }
                    if (!barriers.empty())
                        cmd.ImageBarriers(barriers);
                };
                cmdType["memory_barriers"] = [](CommandBuffer &cmd, sol::table entries) {
                    std::vector<MemoryBarrierInfo> barriers;
                    barriers.reserve(entries.size());
                    for (auto &[k, v] : entries)
                    {
                        if (!v.is<sol::table>()) continue;
                        sol::table entry = v.as<sol::table>();
                        MemoryBarrierInfo b{};
                        b.srcStageMask = ToPipelineStage(entry.get<std::string>(1));
                        b.srcAccessMask = ToAccessFlags(entry.get<std::string>(2));
                        b.dstStageMask = ToPipelineStage(entry.get<std::string>(3));
                        b.dstAccessMask = ToAccessFlags(entry.get<std::string>(4));
                        barriers.push_back(b);
                    }
                    if (!barriers.empty())
                        cmd.MemoryBarriers(barriers);
                };

                // Copy operations
                cmdType["copy_buffer"] = [](CommandBuffer &cmd, Buffer &src, Buffer &dst, size_t size, size_t srcOffset, size_t dstOffset) {
                    cmd.CopyBuffer(&src, &dst, size, srcOffset, dstOffset);
                };
                cmdType["copy_buffer_staged"] = sol::overload(
                    // From Lua data table: copy_buffer_staged(buf, data, "type", dst_offset)
                    [](CommandBuffer &cmd, Buffer &buf, sol::table data, const std::string &type, sol::optional<size_t> dstOffset) {
                        auto it = s_packTypeMap.find(std::string_view(type));
                        if (it == s_packTypeMap.end()) return;
                        std::vector<uint8_t> bytes;
                        for (size_t i = 1; i <= data.size(); i++)
                            PackValue(bytes, data, i, it->second);
                        if (!bytes.empty())
                            cmd.CopyBufferStaged(&buf, bytes.data(), bytes.size(), dstOffset.value_or(0));
                    },
                    // Zero-fill: copy_buffer_staged(buf, size, dst_offset)
                    [](CommandBuffer &cmd, Buffer &buf, size_t size, size_t dstOffset) {
                        std::vector<uint8_t> zeros(size, 0);
                        cmd.CopyBufferStaged(&buf, zeros.data(), size, dstOffset);
                    });
                cmdType["copy_data_to_image_staged"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img,
                                                           sol::table data, const std::string &type,
                                                           sol::optional<uint32_t> baseArrayLayer,
                                                           sol::optional<uint32_t> layerCount,
                                                           sol::optional<uint32_t> mipLevel) {
                    Image *p = img ? img->Get() : nullptr;
                    if (!p) return;
                    auto it = s_packTypeMap.find(std::string_view(type));
                    if (it == s_packTypeMap.end()) return;
                    std::vector<uint8_t> bytes;
                    for (size_t i = 1; i <= data.size(); i++)
                        PackValue(bytes, data, i, it->second);
                    if (!bytes.empty())
                        cmd.CopyDataToImageStaged(p, bytes.data(), bytes.size(),
                            baseArrayLayer.value_or(0), layerCount.value_or(0), mipLevel.value_or(0));
                };
                cmdType["copy_image"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> src, std::shared_ptr<LuaImage> dst) {
                    Image *s = src->Get();
                    Image *d = dst->Get();
                    if (s && d) cmd.CopyImage(s, d);
                };

                // Mip maps
                cmdType["generate_mip_maps"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img) {
                    Image *p = img->Get();
                    if (p) cmd.GenerateMipMaps(p);
                };

                // Events
                cmdType["set_event"] = [](CommandBuffer &cmd, std::shared_ptr<LuaImage> img,
                                          const std::string &srcLayout, const std::string &dstLayout,
                                          const std::string &srcStage, const std::string &dstStage,
                                          const std::string &srcAccess, const std::string &dstAccess) {
                    Image *p = img->Get();
                    if (!p) return;
                    cmd.SetEvent(p, ToImageLayout(srcLayout), ToImageLayout(dstLayout),
                                 ToPipelineStage(srcStage), ToPipelineStage(dstStage),
                                 ToAccessFlags(srcAccess), ToAccessFlags(dstAccess));
                };
                cmdType["wait_event"] = &CommandBuffer::WaitEvent;
                cmdType["reset_event"] = [](CommandBuffer &cmd, const std::string &stage) {
                    cmd.ResetEvent(ToPipelineStage(stage));
                };

                // State / Properties
                cmdType["is_recording"] = sol::property(&CommandBuffer::IsRecording);
                cmdType["get_family_id"] = sol::property(&CommandBuffer::GetFamilyId);
                cmdType["get_queue"] = &CommandBuffer::GetQueue;
                cmdType["set_submission"] = &CommandBuffer::SetSubmission;
                cmdType["wait"] = &CommandBuffer::Wait;
                cmdType["return_cmd"] = &CommandBuffer::Return;
                cmdType["get_command_pool"] = &CommandBuffer::GetCommandPool;
                cmdType["add_after_wait_callback"] = [](CommandBuffer &cmd, sol::function func) {
                    cmd.AddAfterWaitCallback([func]() { func(); });
                };

                // Debug
                cmdType["begin_debug_region"] = &CommandBuffer::BeginDebugRegion;
                cmdType["insert_debug_label"] = &CommandBuffer::InsertDebugLabel;
                cmdType["end_debug_region"] = &CommandBuffer::EndDebugRegion;

                // Static resource cache
                lua.set_function("cmd_get_render_pass", [](sol::table attachments) -> RenderPass * {
                    std::vector<Attachment> atts;
                    atts.reserve(attachments.size());
                    for (size_t i = 1; i <= attachments.size(); i++)
                    {
                        sol::table entry = attachments.get<sol::table>(i);
                        Attachment att{};
                        auto img = entry.get<std::shared_ptr<LuaImage>>(1);
                        att.image = img ? img->Get() : nullptr;
                        if (!att.image) continue;
                        if (entry.size() >= 2)
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, PE_LOAD_OP_CLEAR);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, PE_STORE_OP_STORE);
                        if (entry.size() >= 4)
                            att.stencilLoadOp = Lookup(entry.get<std::string>(4), s_loadOpMap, PE_LOAD_OP_DONT_CARE);
                        if (entry.size() >= 5)
                            att.stencilStoreOp = Lookup(entry.get<std::string>(5), s_storeOpMap, PE_STORE_OP_DONT_CARE);
                        atts.push_back(att);
                    }
                    if (atts.empty()) return nullptr;
                    return CommandBuffer::GetRenderPass(static_cast<uint32_t>(atts.size()), atts.data());
                });
                lua.set_function("cmd_get_framebuffer", [](RenderPass *rp, sol::table attachments) -> Framebuffer * {
                    if (!rp) return nullptr;
                    std::vector<Attachment> atts;
                    atts.reserve(attachments.size());
                    for (size_t i = 1; i <= attachments.size(); i++)
                    {
                        sol::table entry = attachments.get<sol::table>(i);
                        Attachment att{};
                        auto img = entry.get<std::shared_ptr<LuaImage>>(1);
                        att.image = img ? img->Get() : nullptr;
                        if (!att.image) continue;
                        if (entry.size() >= 2)
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, PE_LOAD_OP_CLEAR);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, PE_STORE_OP_STORE);
                        atts.push_back(att);
                    }
                    if (atts.empty()) return nullptr;
                    return CommandBuffer::GetFramebuffer(rp, static_cast<uint32_t>(atts.size()), atts.data());
                });
                lua.set_function("cmd_get_pipeline", [](RenderPass *rp, PassInfo &info) -> Pipeline * {
                    if (!rp) return nullptr;
                    return CommandBuffer::GetPipeline(rp, info);
                });
                lua.set_function("cmd_clear_cache", []() { CommandBuffer::ClearCache(); }); });
        }
    } s_commandBindings;
} // namespace pe
