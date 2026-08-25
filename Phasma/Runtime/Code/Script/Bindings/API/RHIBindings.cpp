#include "Script/ScriptSystem.h"
#include "API/Descriptor.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Base/EventSystem.h"
#include "Runtime/RuntimeStartup.h"

namespace pe
{
    static std::string RequestPresentModeChange(PePresentMode mode)
    {
        Surface *surface = RHII.GetSurface();
        if (!surface)
            return "unknown";

        surface->SetPresentMode(mode);
        const PePresentMode effective = surface->GetPresentMode();
        Settings::Get<SceneSettings>().preferred_present_mode = effective;
        EventSystem::PushEvent(EventType::PresentMode);
        return PresentModeToConfigToken(effective);
    }

    static struct RHIBindings
    {
        RHIBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table rhi = lua.create_named_table("rhi");

                // --- In declaration order (public, non-internal only) ---

                // IsInstanceExtensionValid
                rhi.set_function("is_instance_extension_valid", [](const std::string &name) -> bool {
                    return RHII.IsInstanceExtensionValid(name.c_str());
                });

                // IsInstanceLayerValid
                rhi.set_function("is_instance_layer_valid", [](const std::string &name) -> bool {
                    return RHII.IsInstanceLayerValid(name.c_str());
                });

                // IsDeviceExtensionValid
                rhi.set_function("is_device_extension_valid", [](const std::string &name) -> bool {
                    return RHII.IsDeviceExtensionValid(name.c_str());
                });

                // GetDepthFormat (returns neutral PeFormat enum value)
                rhi.set_function("get_depth_format", []() -> int {
                    return static_cast<int>(RHII.GetDepthFormat());
                });

                // WaitDeviceIdle
                rhi.set_function("wait_device_idle", []() { RHII.WaitDeviceIdle(); });

                // GetFrameCounter
                rhi.set_function("get_frame_counter", []() -> uint32_t { return RHII.GetFrameCounter(); });

                // GetFrameIndex
                rhi.set_function("get_frame_index", []() -> uint32_t { return RHII.GetFrameIndex(); });

                // GetMaxUniformBufferSize
                rhi.set_function("get_max_uniform_buffer_size", []() -> uint32_t { return RHII.GetMaxUniformBufferSize(); });

                // GetMaxStorageBufferSize
                rhi.set_function("get_max_storage_buffer_size", []() -> uint32_t { return RHII.GetMaxStorageBufferSize(); });

                // GetMaxDrawIndirectCount
                rhi.set_function("get_max_draw_indirect_count", []() -> uint32_t { return RHII.GetMaxDrawIndirectCount(); });

                // Align
                rhi.set_function("align", [](size_t size, size_t alignment) -> size_t {
                    return RHII.Align(size, alignment);
                });

                // AlignUniform
                rhi.set_function("align_uniform", [](size_t size) -> size_t { return RHII.AlignUniform(size); });

                // AlignStorage
                rhi.set_function("align_storage", [](size_t size) -> size_t { return RHII.AlignStorage(size); });

                // AlignStorageAs
                rhi.set_function("align_storage_as", [](size_t size, size_t alignment) -> size_t {
                    return RHII.AlignStorageAs(size, alignment);
                });

                // GetMaxPushConstantsSize
                rhi.set_function("get_max_push_constants_size", []() -> uint32_t { return RHII.GetMaxPushConstantsSize(); });

                // GetGpuName
                rhi.set_function("get_gpu_name", []() -> const std::string & { return RHII.GetGpuName(); });

                // GetDescriptorPool
                rhi.set_function("get_descriptor_pool", []() -> DescriptorPool * { return RHII.GetDescriptorPool(); });

                // GetMainQueue
                rhi.set_function("get_main_queue", []() -> Queue * { return RHII.GetMainQueue(); });

                // GetSurface
                rhi.set_function("get_surface", []() -> Surface * { return RHII.GetSurface(); });

                // GetSwapchain
                rhi.set_function("get_swapchain", []() -> Swapchain * { return RHII.GetSwapchain(); });

                // GetSwapchainImageCount
                rhi.set_function("get_swapchain_image_count", []() -> uint32_t { return RHII.GetSwapchainImageCount(); });

                // GetSystemAndProcessMemory
                rhi.set_function("get_system_memory", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto snap = RHII.GetSystemAndProcessMemory();
                    sol::table t = lua.create_table();
                    t["sys_total"] = snap.sysTotal;
                    t["sys_used"] = snap.sysUsed;
                    t["proc_working_set"] = snap.procWorkingSet;
                    t["proc_private_bytes"] = snap.procPrivateBytes;
                    t["proc_commit"] = snap.procCommit;
                    t["proc_peak_ws"] = snap.procPeakWS;
                    return t;
                });

                // GetGpuMemorySnapshot
                rhi.set_function("get_gpu_memory", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto snap = RHII.GetGpuMemorySnapshot();
                    sol::table t = lua.create_table();
                    sol::table vram = lua.create_table();
                    vram["used"] = snap.vram.used;
                    vram["budget"] = snap.vram.budget;
                    vram["size"] = snap.vram.size;
                    vram["app"] = snap.vram.app;
                    t["vram"] = vram;
                    sol::table host = lua.create_table();
                    host["used"] = snap.host.used;
                    host["budget"] = snap.host.budget;
                    host["size"] = snap.host.size;
                    host["app"] = snap.host.app;
                    t["host"] = host;
                    return t;
                });

                // ChangePresentMode
                rhi.set_function("change_present_mode", [](const std::string &mode) -> std::string {
                    const std::optional<PePresentMode> parsed = ParsePresentModeToken(mode);
                    return parsed ? RequestPresentModeChange(*parsed) : "unknown";
                });

                // SetPresentMode - deferred, player-safe alias for change_present_mode.
                rhi.set_function("set_present_mode", [](const std::string &mode) -> std::string {
                    const std::optional<PePresentMode> parsed = ParsePresentModeToken(mode);
                    return parsed ? RequestPresentModeChange(*parsed) : "unknown";
                });

                // SetRenderScale — scene renderers detect the change and rebuild only their scaled targets.
                // Returns the clamped value so callers can sync their UI.
                rhi.set_function("set_render_scale", [](double scale) -> float {
                    auto &gs = Settings::Get<SceneSettings>();
                    gs.render_scale = ClampRenderScale(static_cast<float>(scale));
                    return gs.render_scale;
                });

                // GetRenderScale
                rhi.set_function("get_render_scale", []() -> float {
                    return Settings::Get<SceneSettings>().render_scale;
                });

                // GetPresentMode
                rhi.set_function("get_present_mode", []() -> std::string {
                    return RHII.GetSurface() ? PresentModeToConfigToken(RHII.GetSurface()->GetPresentMode()) : "unknown";
                });

                // PresentModeToString
                rhi.set_function("present_mode_to_string", [](const std::string &mode) -> std::string {
                    const std::optional<PePresentMode> parsed = ParsePresentModeToken(mode);
                    return parsed ? PresentModeToConfigToken(*parsed) : "unknown";
                });

                // GetWidth
                rhi.set_function("get_width", []() -> uint32_t { return RHII.GetWidth(); });

                // GetHeight
                rhi.set_function("get_height", []() -> uint32_t { return RHII.GetHeight(); });

                // GetWidthf
                rhi.set_function("get_width_f", []() -> float { return RHII.GetWidthf(); });

                // GetHeightf
                rhi.set_function("get_height_f", []() -> float { return RHII.GetHeightf(); }); });
        }
    } s_rhiBindings;
} // namespace pe
