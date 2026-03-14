#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "API/Descriptor.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "API/Swapchain.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::PresentModeKHR> s_presentModeMap = {
        {"immediate", vk::PresentModeKHR::eImmediate},
        {"mailbox", vk::PresentModeKHR::eMailbox},
        {"fifo", vk::PresentModeKHR::eFifo},
        {"fifo_relaxed", vk::PresentModeKHR::eFifoRelaxed},
    };

    static std::string PresentModeToString(vk::PresentModeKHR mode)
    {
        for (auto &[k, v] : s_presentModeMap)
            if (v == mode)
                return std::string(k);
        return "unknown";
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

                // GetDepthFormat
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
                rhi.set_function("get_system_memory", [&lua]() -> sol::table {
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
                rhi.set_function("get_gpu_memory", [&lua]() -> sol::table {
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
                rhi.set_function("change_present_mode", [](const std::string &mode) {
                    auto it = s_presentModeMap.find(std::string_view(mode));
                    if (it != s_presentModeMap.end())
                        RHII.ChangePresentMode(it->second);
                });

                // PresentModeToString
                rhi.set_function("present_mode_to_string", [](const std::string &mode) -> std::string {
                    auto it = s_presentModeMap.find(std::string_view(mode));
                    if (it != s_presentModeMap.end())
                        return PresentModeToString(it->second);
                    return "unknown";
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
#endif
