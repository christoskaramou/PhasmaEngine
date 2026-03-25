#include "Script/ScriptSystem.h"
#include "API/Debug.h"
#include "API/Buffer.h"
#include "API/Image.h"
#include "API/Command.h"
#include "API/Pipeline.h"
#include "API/RenderPass.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"
#include "API/Event.h"
#include "API/Framebuffer.h"
#include "API/Descriptor.h"
#include "API/Queue.h"

namespace pe
{
    static struct DebugBindings
    {
        DebugBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table debug = lua.create_named_table("debug_utils");

                // Init / Messenger
                debug.set_function("create_debug_messenger", []() { Debug::CreateDebugMessenger(); });
                debug.set_function("destroy_debug_messenger", []() { Debug::DestroyDebugMessenger(); });

                // Object naming
                debug.set_function("set_buffer_name", [](Buffer &buf, const std::string &name) {
                    Debug::SetObjectName(buf.ApiHandle(), name);
                });
                debug.set_function("set_image_name", [](std::shared_ptr<LuaImage> img, const std::string &name) {
                    Image *p = img ? img->Get() : nullptr;
                    if (p) Debug::SetObjectName(p->ApiHandle(), name);
                });
                debug.set_function("set_command_buffer_name", [](CommandBuffer &cmd, const std::string &name) {
                    Debug::SetObjectName(cmd.ApiHandle(), name);
                });
                debug.set_function("set_pipeline_name", [](Pipeline &pipe, const std::string &name) {
                    Debug::SetObjectName(pipe.ApiHandle(), name);
                });
                debug.set_function("set_render_pass_name", [](RenderPass &rp, const std::string &name) {
                    Debug::SetObjectName(rp.ApiHandle(), name);
                });
                debug.set_function("set_semaphore_name", [](Semaphore &sem, const std::string &name) {
                    Debug::SetObjectName(sem.ApiHandle(), name);
                });
                debug.set_function("set_event_name", [](Event &ev, const std::string &name) {
                    Debug::SetObjectName(ev.ApiHandle(), name);
                });
                debug.set_function("set_framebuffer_name", [](Framebuffer &fb, const std::string &name) {
                    Debug::SetObjectName(fb.ApiHandle(), name);
                });
                debug.set_function("set_descriptor_name", [](Descriptor &desc, const std::string &name) {
                    Debug::SetObjectName(desc.ApiHandle(), name);
                });
                debug.set_function("set_queue_name", [](Queue &q, const std::string &name) {
                    Debug::SetObjectName(q.ApiHandle(), name);
                });
                debug.set_function("set_swapchain_name", [](Swapchain &sw, const std::string &name) {
                    Debug::SetObjectName(sw.ApiHandle(), name);
                });
                debug.set_function("set_command_pool_name", [](CommandPool &cp, const std::string &name) {
                    Debug::SetObjectName(cp.ApiHandle(), name);
                });

                // Frame capture (RenderDoc / GPU debugger)
                debug.set_function("init_capture_api", []() { Debug::InitCaptureApi(); });
                debug.set_function("destroy_capture_api", []() { Debug::DestroyCaptureApi(); });
                debug.set_function("trigger_capture", [](sol::optional<uint32_t> n) {
                    Debug::TriggerMultiFrameCapture(n.value_or(1));
                }); });
        }
    } s_debugBindings;
} // namespace pe
