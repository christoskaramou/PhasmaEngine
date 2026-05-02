#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Vulkan/Helpers_Vulkan.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::Format> s_helperFormatMap = {
        {"rgba8", vk::Format::eR8G8B8A8Unorm},
        {"rgba8_srgb", vk::Format::eR8G8B8A8Srgb},
        {"bgra8", vk::Format::eB8G8R8A8Unorm},
        {"rgba16f", vk::Format::eR16G16B16A16Sfloat},
        {"rgba32f", vk::Format::eR32G32B32A32Sfloat},
        {"r8", vk::Format::eR8Unorm},
        {"r16f", vk::Format::eR16Sfloat},
        {"r32f", vk::Format::eR32Sfloat},
        {"d32f", vk::Format::eD32Sfloat},
        {"d24_s8", vk::Format::eD24UnormS8Uint},
        {"d32f_s8", vk::Format::eD32SfloatS8Uint},
        {"s8", vk::Format::eS8Uint},
    };

    static const std::unordered_map<std::string_view, vk::AccessFlags2> s_helperAccessMap = {
        {"none", vk::AccessFlagBits2::eNone},
        {"shader_read", vk::AccessFlagBits2::eShaderRead},
        {"shader_write", vk::AccessFlagBits2::eShaderWrite},
        {"color_read", vk::AccessFlagBits2::eColorAttachmentRead},
        {"color_write", vk::AccessFlagBits2::eColorAttachmentWrite},
        {"depth_read", vk::AccessFlagBits2::eDepthStencilAttachmentRead},
        {"depth_write", vk::AccessFlagBits2::eDepthStencilAttachmentWrite},
        {"transfer_read", vk::AccessFlagBits2::eTransferRead},
        {"transfer_write", vk::AccessFlagBits2::eTransferWrite},
        {"host_write", vk::AccessFlagBits2::eHostWrite},
        {"memory_read", vk::AccessFlagBits2::eMemoryRead},
        {"memory_write", vk::AccessFlagBits2::eMemoryWrite},
    };

    static struct HelpersBindings
    {
        HelpersBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // IsDepthAndStencil
                lua.set_function("is_depth_and_stencil", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::IsDepthAndStencil(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // IsDepthOnly
                lua.set_function("is_depth_only", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::IsDepthOnly(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // IsStencilOnly
                lua.set_function("is_stencil_only", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::IsStencilOnly(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // HasDepth
                lua.set_function("has_depth", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::HasDepth(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // HasStencil
                lua.set_function("has_stencil", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::HasStencil(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // HasDepthOrStencil
                lua.set_function("has_depth_or_stencil", [](const std::string &fmt) -> bool {
                    return VulkanHelpers::HasDepthOrStencil(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                });

                // GetAspectMask
                lua.set_function("get_aspect_mask", [](const std::string &fmt) -> std::string {
                    vk::ImageAspectFlags flags = VulkanHelpers::GetAspectMask(Lookup(fmt, s_helperFormatMap, vk::Format::eUndefined));
                    std::string result;
                    if (flags & vk::ImageAspectFlagBits::eColor)
                        result += "color";
                    if (flags & vk::ImageAspectFlagBits::eDepth)
                    {
                        if (!result.empty()) result += "|";
                        result += "depth";
                    }
                    if (flags & vk::ImageAspectFlagBits::eStencil)
                    {
                        if (!result.empty()) result += "|";
                        result += "stencil";
                    }
                    return result;
                });

                // IsReadOnlyAccess
                lua.set_function("is_read_only_access", [](const std::string &access) -> bool {
                    return VulkanHelpers::IsReadOnlyAccess(LookupFlags<vk::AccessFlags2>(access, s_helperAccessMap));
                }); });
        }
    } s_helpersBindings;
} // namespace pe
