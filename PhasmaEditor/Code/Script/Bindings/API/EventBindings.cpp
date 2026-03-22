#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Event.h"
#include "API/Command.h"
#include "API/Image.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::ImageLayout> s_eventLayoutMap = {
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

    static const std::unordered_map<std::string_view, vk::PipelineStageFlags2> s_eventStageMap = {
        {"none", vk::PipelineStageFlagBits2::eNone},
        {"vertex", vk::PipelineStageFlagBits2::eVertexShader},
        {"fragment", vk::PipelineStageFlagBits2::eFragmentShader},
        {"early_fragment", vk::PipelineStageFlagBits2::eEarlyFragmentTests},
        {"late_fragment", vk::PipelineStageFlagBits2::eLateFragmentTests},
        {"color_output", vk::PipelineStageFlagBits2::eColorAttachmentOutput},
        {"compute", vk::PipelineStageFlagBits2::eComputeShader},
        {"transfer", vk::PipelineStageFlagBits2::eTransfer},
        {"bottom_of_pipe", vk::PipelineStageFlagBits2::eBottomOfPipe},
        {"all_graphics", vk::PipelineStageFlagBits2::eAllGraphics},
        {"all_commands", vk::PipelineStageFlagBits2::eAllCommands},
    };

    static const std::unordered_map<std::string_view, vk::AccessFlags2> s_eventAccessMap = {
        {"none", vk::AccessFlagBits2::eNone},
        {"shader_read", vk::AccessFlagBits2::eShaderRead},
        {"shader_write", vk::AccessFlagBits2::eShaderWrite},
        {"color_read", vk::AccessFlagBits2::eColorAttachmentRead},
        {"color_write", vk::AccessFlagBits2::eColorAttachmentWrite},
        {"depth_read", vk::AccessFlagBits2::eDepthStencilAttachmentRead},
        {"depth_write", vk::AccessFlagBits2::eDepthStencilAttachmentWrite},
        {"transfer_read", vk::AccessFlagBits2::eTransferRead},
        {"transfer_write", vk::AccessFlagBits2::eTransferWrite},
        {"memory_read", vk::AccessFlagBits2::eMemoryRead},
        {"memory_write", vk::AccessFlagBits2::eMemoryWrite},
    };

    static struct EventBindings
    {
        EventBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Event> eventType = lua.new_usertype<Event>("Event", sol::no_constructor);

                // Factory
                lua.set_function("create_event", [](const std::string &name) -> Event * {
                    return Event::Create(name);
                });
                lua.set_function("destroy_event", [](Event *ev) {
                    if (ev) Event::Destroy(ev);
                });

                // Set
                eventType["set"] = [](Event &ev, CommandBuffer &cmd, std::shared_ptr<LuaImage> img,
                                      const std::string &srcLayout, const std::string &dstLayout,
                                      const std::string &srcStage, const std::string &dstStage,
                                      const std::string &srcAccess, const std::string &dstAccess) {
                    Image *p = img ? img->Get() : nullptr;
                    if (!p) return;
                    ev.Set(&cmd,
                           p,
                           Lookup(srcLayout, s_eventLayoutMap, vk::ImageLayout::eUndefined),
                           Lookup(dstLayout, s_eventLayoutMap, vk::ImageLayout::eUndefined),
                           Lookup(srcStage, s_eventStageMap),
                           Lookup(dstStage, s_eventStageMap),
                           Lookup(srcAccess, s_eventAccessMap),
                           Lookup(dstAccess, s_eventAccessMap));
                };

                // Wait
                eventType["wait"] = &Event::Wait;

                // Reset
                eventType["reset"] = [](Event &e, const std::string &stage) {
                    e.Reset(Lookup(stage, s_eventStageMap, vk::PipelineStageFlags2(vk::PipelineStageFlagBits2::eAllCommands)));
                };

                // IsSet
                eventType["is_set"] = sol::property(&Event::IsSet); });
        }
    } s_eventBindings;
} // namespace pe
