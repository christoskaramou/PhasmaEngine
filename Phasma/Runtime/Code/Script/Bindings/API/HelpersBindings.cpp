#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/RHITypes.h"

namespace pe
{
    static const std::unordered_map<std::string_view, ::PeFormat> s_helperFormatMap = {
        {"rgba8", PE_FORMAT_R8G8B8A8_UNORM},
        {"rgba8_srgb", PE_FORMAT_R8G8B8A8_SRGB},
        {"bgra8", PE_FORMAT_B8G8R8A8_UNORM},
        {"rgba16f", PE_FORMAT_R16G16B16A16_SFLOAT},
        {"rgba32f", PE_FORMAT_R32G32B32A32_SFLOAT},
        {"r8", PE_FORMAT_R8_UNORM},
        {"r16f", PE_FORMAT_R16_SFLOAT},
        {"r32f", PE_FORMAT_R32_SFLOAT},
        {"d32f", PE_FORMAT_D32_SFLOAT},
        {"d24_s8", PE_FORMAT_D24_UNORM_S8_UINT},
        {"d32f_s8", PE_FORMAT_D32_SFLOAT_S8_UINT},
        {"s8", PE_FORMAT_S8_UINT},
    };

    static const std::unordered_map<std::string_view, PeAccessFlags> s_helperAccessMap = {
        {"none", PE_ACCESS_NONE},
        {"shader_read", PE_ACCESS_SHADER_READ},
        {"shader_write", PE_ACCESS_SHADER_WRITE},
        {"color_read", PE_ACCESS_COLOR_ATTACHMENT_READ},
        {"color_write", PE_ACCESS_COLOR_ATTACHMENT_WRITE},
        {"depth_read", PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ},
        {"depth_write", PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE},
        {"transfer_read", PE_ACCESS_TRANSFER_READ},
        {"transfer_write", PE_ACCESS_TRANSFER_WRITE},
        {"host_write", PE_ACCESS_HOST_WRITE},
        {"memory_read", PE_ACCESS_MEMORY_READ},
        {"memory_write", PE_ACCESS_MEMORY_WRITE},
    };

    static struct HelpersBindings
    {
        HelpersBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // IsDepthAndStencil
                lua.set_function("is_depth_and_stencil", [](const std::string &fmt) -> bool {
                    return ::PeFormatIsDepthAndStencil(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // IsDepthOnly
                lua.set_function("is_depth_only", [](const std::string &fmt) -> bool {
                    return ::PeFormatIsDepthOnly(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // IsStencilOnly
                lua.set_function("is_stencil_only", [](const std::string &fmt) -> bool {
                    return ::PeFormatIsStencilOnly(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // HasDepth
                lua.set_function("has_depth", [](const std::string &fmt) -> bool {
                    return ::PeFormatHasDepth(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // HasStencil
                lua.set_function("has_stencil", [](const std::string &fmt) -> bool {
                    return ::PeFormatHasStencil(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // HasDepthOrStencil
                lua.set_function("has_depth_or_stencil", [](const std::string &fmt) -> bool {
                    return ::PeFormatHasDepthOrStencil(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                });

                // GetAspectMask
                lua.set_function("get_aspect_mask", [](const std::string &fmt) -> std::string {
                    PeImageAspectFlags flags = ::PeFormatAspectMask(Lookup(fmt, s_helperFormatMap, PE_FORMAT_UNDEFINED));
                    std::string result;
                    if (flags & PE_IMAGE_ASPECT_COLOR)
                        result += "color";
                    if (flags & PE_IMAGE_ASPECT_DEPTH)
                    {
                        if (!result.empty()) result += "|";
                        result += "depth";
                    }
                    if (flags & PE_IMAGE_ASPECT_STENCIL)
                    {
                        if (!result.empty()) result += "|";
                        result += "stencil";
                    }
                    return result;
                });

                // IsReadOnlyAccess
                lua.set_function("is_read_only_access", [](const std::string &access) -> bool {
                    return ::IsReadOnlyAccess(LookupFlags<PeAccessFlags>(access, s_helperAccessMap));
                }); });
        }
    } s_helpersBindings;
} // namespace pe
