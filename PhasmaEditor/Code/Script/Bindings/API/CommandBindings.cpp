#if defined(PE_SCRIPTS)
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
    static const std::unordered_map<std::string_view, vk::ImageLayout> s_imageLayoutMap = {
        {"undefined", vk::ImageLayout::eUndefined},
        {"general", vk::ImageLayout::eGeneral},
        {"color_attachment", vk::ImageLayout::eColorAttachmentOptimal},
        {"depth_attachment", vk::ImageLayout::eDepthStencilAttachmentOptimal},
        {"shader_read", vk::ImageLayout::eShaderReadOnlyOptimal},
        {"transfer_src", vk::ImageLayout::eTransferSrcOptimal},
        {"transfer_dst", vk::ImageLayout::eTransferDstOptimal},
        {"present", vk::ImageLayout::ePresentSrcKHR},
        {"attachment", vk::ImageLayout::eAttachmentOptimal},
    };

    static const std::unordered_map<std::string_view, vk::PipelineStageFlags2> s_pipelineStageMap = {
        {"none", vk::PipelineStageFlagBits2::eNone},
        {"vertex_input", vk::PipelineStageFlagBits2::eVertexInput},
        {"vertex", vk::PipelineStageFlagBits2::eVertexShader},
        {"fragment", vk::PipelineStageFlagBits2::eFragmentShader},
        {"early_fragment", vk::PipelineStageFlagBits2::eEarlyFragmentTests},
        {"late_fragment", vk::PipelineStageFlagBits2::eLateFragmentTests},
        {"color_output", vk::PipelineStageFlagBits2::eColorAttachmentOutput},
        {"compute", vk::PipelineStageFlagBits2::eComputeShader},
        {"transfer", vk::PipelineStageFlagBits2::eTransfer},
        {"draw_indirect", vk::PipelineStageFlagBits2::eDrawIndirect},
        {"ray_tracing", vk::PipelineStageFlagBits2::eRayTracingShaderKHR},
        {"acceleration_structure_build", vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR},
        {"bottom_of_pipe", vk::PipelineStageFlagBits2::eBottomOfPipe},
        {"all_graphics", vk::PipelineStageFlagBits2::eAllGraphics},
        {"all_commands", vk::PipelineStageFlagBits2::eAllCommands},
    };

    static const std::unordered_map<std::string_view, vk::AccessFlags2> s_accessFlagsMap = {
        {"none", vk::AccessFlagBits2::eNone},
        {"shader_read", vk::AccessFlagBits2::eShaderRead},
        {"shader_write", vk::AccessFlagBits2::eShaderWrite},
        {"shader_read_write", vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite},
        {"color_read", vk::AccessFlagBits2::eColorAttachmentRead},
        {"color_write", vk::AccessFlagBits2::eColorAttachmentWrite},
        {"depth_read", vk::AccessFlagBits2::eDepthStencilAttachmentRead},
        {"depth_write", vk::AccessFlagBits2::eDepthStencilAttachmentWrite},
        {"transfer_read", vk::AccessFlagBits2::eTransferRead},
        {"transfer_write", vk::AccessFlagBits2::eTransferWrite},
        {"host_write", vk::AccessFlagBits2::eHostWrite},
        {"memory_read", vk::AccessFlagBits2::eMemoryRead},
        {"memory_write", vk::AccessFlagBits2::eMemoryWrite},
        {"index_read", vk::AccessFlagBits2::eIndexRead},
        {"vertex_read", vk::AccessFlagBits2::eVertexAttributeRead},
        {"indirect_read", vk::AccessFlagBits2::eIndirectCommandRead},
        {"acceleration_structure_read", vk::AccessFlagBits2::eAccelerationStructureReadKHR},
        {"acceleration_structure_write", vk::AccessFlagBits2::eAccelerationStructureWriteKHR},
    };

    static const std::unordered_map<std::string_view, vk::AttachmentLoadOp> s_loadOpMap = {
        {"load", vk::AttachmentLoadOp::eLoad},
        {"clear", vk::AttachmentLoadOp::eClear},
        {"dont_care", vk::AttachmentLoadOp::eDontCare},
    };

    static const std::unordered_map<std::string_view, vk::AttachmentStoreOp> s_storeOpMap = {
        {"store", vk::AttachmentStoreOp::eStore},
        {"dont_care", vk::AttachmentStoreOp::eDontCare},
    };

    static const std::unordered_map<std::string_view, vk::ImageAspectFlags> s_aspectMaskMap = {
        {"color", vk::ImageAspectFlags(vk::ImageAspectFlagBits::eColor)},
        {"depth", vk::ImageAspectFlags(vk::ImageAspectFlagBits::eDepth)},
        {"stencil", vk::ImageAspectFlags(vk::ImageAspectFlagBits::eStencil)},
        {"depth_stencil", vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil},
    };

    static vk::ImageLayout ToImageLayout(const std::string &s) { return Lookup(s, s_imageLayoutMap, vk::ImageLayout::eUndefined); }
    static vk::PipelineStageFlags2 ToPipelineStage(const std::string &s) { return Lookup(s, s_pipelineStageMap); }
    static vk::AccessFlags2 ToAccessFlags(const std::string &s) { return Lookup(s, s_accessFlagsMap); }
    static vk::ImageAspectFlags ToAspectMask(const std::string &s) { return Lookup(s, s_aspectMaskMap, vk::ImageAspectFlags(vk::ImageAspectFlagBits::eColor)); }

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
                    vk::ImageAspectFlags aspectMask = aspect ? ToAspectMask(*aspect) : vk::ImageAspectFlagBits::eColor;
                    vk::ImageBlit region{};
                    region.srcOffsets[0] = vk::Offset3D{0, 0, 0};
                    region.srcOffsets[1] = vk::Offset3D{static_cast<int32_t>(s->GetWidth()), static_cast<int32_t>(s->GetHeight()), 1};
                    region.srcSubresource.aspectMask = aspectMask;
                    region.srcSubresource.layerCount = 1;
                    region.dstOffsets[0] = vk::Offset3D{0, 0, 0};
                    region.dstOffsets[1] = vk::Offset3D{static_cast<int32_t>(d->GetWidth()), static_cast<int32_t>(d->GetHeight()), 1};
                    region.dstSubresource.aspectMask = aspectMask;
                    region.dstSubresource.layerCount = 1;
                    cmd.BlitImage(s, d, region, filter == "nearest" ? vk::Filter::eNearest : vk::Filter::eLinear);
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
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, vk::AttachmentLoadOp::eClear);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, vk::AttachmentStoreOp::eStore);
                        if (entry.size() >= 4)
                            att.stencilLoadOp = Lookup(entry.get<std::string>(4), s_loadOpMap, vk::AttachmentLoadOp::eDontCare);
                        if (entry.size() >= 5)
                            att.stencilStoreOp = Lookup(entry.get<std::string>(5), s_storeOpMap, vk::AttachmentStoreOp::eDontCare);
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
                    vk::MemoryBarrier2 barrier{};
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
                    std::vector<vk::MemoryBarrier2> barriers;
                    barriers.reserve(entries.size());
                    for (auto &[k, v] : entries)
                    {
                        if (!v.is<sol::table>()) continue;
                        sol::table entry = v.as<sol::table>();
                        vk::MemoryBarrier2 b{};
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
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, vk::AttachmentLoadOp::eClear);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, vk::AttachmentStoreOp::eStore);
                        if (entry.size() >= 4)
                            att.stencilLoadOp = Lookup(entry.get<std::string>(4), s_loadOpMap, vk::AttachmentLoadOp::eDontCare);
                        if (entry.size() >= 5)
                            att.stencilStoreOp = Lookup(entry.get<std::string>(5), s_storeOpMap, vk::AttachmentStoreOp::eDontCare);
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
                            att.loadOp = Lookup(entry.get<std::string>(2), s_loadOpMap, vk::AttachmentLoadOp::eClear);
                        if (entry.size() >= 3)
                            att.storeOp = Lookup(entry.get<std::string>(3), s_storeOpMap, vk::AttachmentStoreOp::eStore);
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
#endif
