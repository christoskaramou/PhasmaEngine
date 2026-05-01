#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Event.h"
#include "API/Command.h"
#include "API/Image.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeImageLayout> s_eventLayoutMap = {
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

    static const std::unordered_map<std::string_view, PeBarrierSync> s_eventStageMap = {
        {"none", PE_STAGE_NONE},
        {"top_of_pipe", PE_STAGE_TOP_OF_PIPE},
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
        {"bottom_of_pipe", PE_STAGE_BOTTOM_OF_PIPE},
        {"all_graphics", PE_STAGE_ALL_GRAPHICS},
        {"all_commands", PE_STAGE_ALL_COMMANDS},
    };

    static const std::unordered_map<std::string_view, PeBarrierAccess> s_eventAccessMap = {
        {"none", PE_ACCESS_NONE},
        {"shader_read", PE_ACCESS_SHADER_READ},
        {"shader_write", PE_ACCESS_SHADER_WRITE},
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
        {"memory_read", PE_ACCESS_MEMORY_READ},
        {"memory_write", PE_ACCESS_MEMORY_WRITE},
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
                           Lookup(srcLayout, s_eventLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED),
                           Lookup(dstLayout, s_eventLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED),
                           Lookup(srcStage, s_eventStageMap),
                           Lookup(dstStage, s_eventStageMap),
                           Lookup(srcAccess, s_eventAccessMap),
                           Lookup(dstAccess, s_eventAccessMap));
                };

                // Wait
                eventType["wait"] = &Event::Wait;

                // Reset
                eventType["reset"] = [](Event &e, const std::string &stage) {
                    e.Reset(Lookup(stage, s_eventStageMap, PE_STAGE_ALL_COMMANDS));
                };

                // IsSet
                eventType["is_set"] = sol::property(&Event::IsSet); });
        }
    } s_eventBindings;
} // namespace pe
