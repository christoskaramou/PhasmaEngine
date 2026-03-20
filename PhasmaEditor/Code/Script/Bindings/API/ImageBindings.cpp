#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Command.h"
#include "API/Image.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::Format> s_imageFormatMap = {
        {"rgba8", vk::Format::eR8G8B8A8Unorm},
        {"rgba8_srgb", vk::Format::eR8G8B8A8Srgb},
        {"bgra8", vk::Format::eB8G8R8A8Unorm},
        {"bgra8_srgb", vk::Format::eB8G8R8A8Srgb},
        {"rgba16f", vk::Format::eR16G16B16A16Sfloat},
        {"rgba32f", vk::Format::eR32G32B32A32Sfloat},
        {"rg16f", vk::Format::eR16G16Sfloat},
        {"rg32f", vk::Format::eR32G32Sfloat},
        {"r8", vk::Format::eR8Unorm},
        {"r16f", vk::Format::eR16Sfloat},
        {"r32f", vk::Format::eR32Sfloat},
        {"d32f", vk::Format::eD32Sfloat},
        {"d24_s8", vk::Format::eD24UnormS8Uint},
        {"d32f_s8", vk::Format::eD32SfloatS8Uint},
        {"s8", vk::Format::eS8Uint},
        {"rgb10a2", vk::Format::eA2B10G10R10UnormPack32},
        {"rg11b10f", vk::Format::eB10G11R11UfloatPack32},
    };

    static const std::unordered_map<std::string_view, vk::ImageUsageFlags> s_imageUsageMap = {
        {"sampled", vk::ImageUsageFlagBits::eSampled},
        {"storage", vk::ImageUsageFlagBits::eStorage},
        {"color_attachment", vk::ImageUsageFlagBits::eColorAttachment},
        {"depth_attachment", vk::ImageUsageFlagBits::eDepthStencilAttachment},
        {"transfer_src", vk::ImageUsageFlagBits::eTransferSrc},
        {"transfer_dst", vk::ImageUsageFlagBits::eTransferDst},
        {"input_attachment", vk::ImageUsageFlagBits::eInputAttachment},
    };

    static const std::unordered_map<std::string_view, vk::ImageViewType> s_imageViewTypeMap = {
        {"1d", vk::ImageViewType::e1D},
        {"2d", vk::ImageViewType::e2D},
        {"3d", vk::ImageViewType::e3D},
        {"cube", vk::ImageViewType::eCube},
        {"1d_array", vk::ImageViewType::e1DArray},
        {"2d_array", vk::ImageViewType::e2DArray},
        {"cube_array", vk::ImageViewType::eCubeArray},
    };

    static const std::unordered_map<std::string_view, vk::ImageLayout> s_imgLayoutMap = {
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

    static const std::unordered_map<std::string_view, vk::PipelineStageFlags2> s_imgStageMap = {
        {"none", vk::PipelineStageFlagBits2::eNone},
        {"vertex", vk::PipelineStageFlagBits2::eVertexShader},
        {"fragment", vk::PipelineStageFlagBits2::eFragmentShader},
        {"early_fragment", vk::PipelineStageFlagBits2::eEarlyFragmentTests},
        {"late_fragment", vk::PipelineStageFlagBits2::eLateFragmentTests},
        {"color_output", vk::PipelineStageFlagBits2::eColorAttachmentOutput},
        {"compute", vk::PipelineStageFlagBits2::eComputeShader},
        {"transfer", vk::PipelineStageFlagBits2::eTransfer},
        {"all_graphics", vk::PipelineStageFlagBits2::eAllGraphics},
        {"all_commands", vk::PipelineStageFlagBits2::eAllCommands},
    };

    static const std::unordered_map<std::string_view, vk::AccessFlags2> s_imgAccessMap = {
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

    static vk::Format ToImageFormat(const std::string &s) { return Lookup(s, s_imageFormatMap, vk::Format::eR8G8B8A8Unorm); }
    static vk::ImageUsageFlags ToImageUsage(const std::string &s) { return LookupFlags<vk::ImageUsageFlags>(s, s_imageUsageMap); }

    // Reverse lookup: vk::Format -> string
    static std::string FormatToString(vk::Format fmt)
    {
        for (auto &[k, v] : s_imageFormatMap)
            if (v == fmt)
                return std::string(k);
        return "unknown";
    }

    static struct ImageBindings
    {
        ImageBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<LuaImage> imageType = lua.new_usertype<LuaImage>("Image", sol::no_constructor);

                // --- In declaration order from Image.h ---

                // Unload
                imageType["unload"] = [](LuaImage &img) {
                    Image *p = img.Get();
                    if (p) p->Unload();
                };

                // GetWidth
                imageType["get_width"] = sol::property([](LuaImage &img) -> uint32_t {
                    Image *p = img.Get();
                    return p ? p->GetWidth() : 0;
                });

                // GetHeight
                imageType["get_height"] = sol::property([](LuaImage &img) -> uint32_t {
                    Image *p = img.Get();
                    return p ? p->GetHeight() : 0;
                });

                // GetWidth_f
                imageType["get_width_f"] = sol::property([](LuaImage &img) -> float {
                    Image *p = img.Get();
                    return p ? p->GetWidth_f() : 0.f;
                });

                // GetHeight_f
                imageType["get_height_f"] = sol::property([](LuaImage &img) -> float {
                    Image *p = img.Get();
                    return p ? p->GetHeight_f() : 0.f;
                });

                // GetSampler
                imageType["get_sampler"] = [](LuaImage &img) -> Sampler * {
                    Image *p = img.Get();
                    return p ? p->GetSampler() : nullptr;
                };

                // SetSampler
                imageType["set_sampler"] = [](LuaImage &img, Sampler *sampler) {
                    Image *p = img.Get();
                    if (p) p->SetSampler(sampler);
                };

                // GetName
                imageType["get_name"] = sol::property([](LuaImage &img) -> std::string {
                    Image *p = img.Get();
                    return p ? p->GetName() : "";
                });

                // GetFormat (returns string)
                imageType["get_format"] = sol::property([](LuaImage &img) -> std::string {
                    Image *p = img.Get();
                    return p ? FormatToString(p->GetFormat()) : "unknown";
                });

                // GetMipLevels
                imageType["get_mip_levels"] = sol::property([](LuaImage &img) -> uint32_t {
                    Image *p = img.Get();
                    return p ? p->GetMipLevels() : 0;
                });

                // GetArrayLayers
                imageType["get_array_layers"] = sol::property([](LuaImage &img) -> uint32_t {
                    Image *p = img.Get();
                    return p ? p->GetArrayLayers() : 0;
                });

                // GetSamples
                imageType["get_samples"] = sol::property([](LuaImage &img) -> int {
                    Image *p = img.Get();
                    return p ? static_cast<int>(p->GetSamples()) : 0;
                });

                // GetCurrentInfo
                imageType["get_current_info"] = [&lua](LuaImage &img, sol::optional<uint32_t> layer, sol::optional<uint32_t> mip) -> sol::table {
                    Image *p = img.Get();
                    sol::table t = lua.create_table();
                    if (!p) return t;
                    auto &info = p->GetCurrentInfo(layer.value_or(0), mip.value_or(0));
                    t["layout"] = static_cast<int>(info.layout);
                    t["stage_flags"] = static_cast<uint64_t>(static_cast<vk::PipelineStageFlags2::MaskType>(info.stageFlags));
                    t["access_mask"] = static_cast<uint64_t>(static_cast<vk::AccessFlags2::MaskType>(info.accessMask));
                    t["base_array_layer"] = info.baseArrayLayer;
                    t["array_layers"] = info.arrayLayers;
                    t["base_mip_level"] = info.baseMipLevel;
                    t["mip_levels"] = info.mipLevels;
                    t["queue_family_id"] = info.queueFamilyId;
                    return t;
                };

                // SetCurrentInfo
                imageType["set_current_info"] = [](LuaImage &img, const sol::table &t, sol::optional<uint32_t> layer, sol::optional<uint32_t> mip) {
                    Image *p = img.Get();
                    if (!p) return;
                    ImageTrackInfo info{};
                    info.image = p;
                    info.layout = Lookup(t.get_or<std::string>("layout", "undefined"), s_imgLayoutMap, vk::ImageLayout::eUndefined);
                    info.stageFlags = LookupFlags<vk::PipelineStageFlags2>(t.get_or<std::string>("stage_flags", "none"), s_imgStageMap);
                    info.accessMask = LookupFlags<vk::AccessFlags2>(t.get_or<std::string>("access_mask", "none"), s_imgAccessMap);
                    info.baseArrayLayer = t.get_or<uint32_t>("base_array_layer", 0);
                    info.arrayLayers = t.get_or<uint32_t>("array_layers", 0);
                    info.baseMipLevel = t.get_or<uint32_t>("base_mip_level", 0);
                    info.mipLevels = t.get_or<uint32_t>("mip_levels", 0);
                    info.queueFamilyId = t.get_or("queue_family_id", static_cast<uint32_t>(VK_QUEUE_FAMILY_IGNORED));
                    p->SetCurrentInfo(info, layer.value_or(0), mip.value_or(0));
                };

                // SetCurrentInfoAll
                imageType["set_current_info_all"] = [](LuaImage &img, const sol::table &t) {
                    Image *p = img.Get();
                    if (!p) return;
                    ImageTrackInfo info{};
                    info.image = p;
                    info.layout = Lookup(t.get_or<std::string>("layout", "undefined"), s_imgLayoutMap, vk::ImageLayout::eUndefined);
                    info.stageFlags = LookupFlags<vk::PipelineStageFlags2>(t.get_or<std::string>("stage_flags", "none"), s_imgStageMap);
                    info.accessMask = LookupFlags<vk::AccessFlags2>(t.get_or<std::string>("access_mask", "none"), s_imgAccessMap);
                    info.baseArrayLayer = t.get_or<uint32_t>("base_array_layer", 0);
                    info.arrayLayers = t.get_or<uint32_t>("array_layers", 0);
                    info.baseMipLevel = t.get_or<uint32_t>("base_mip_level", 0);
                    info.mipLevels = t.get_or<uint32_t>("mip_levels", 0);
                    info.queueFamilyId = t.get_or("queue_family_id", static_cast<uint32_t>(VK_QUEUE_FAMILY_IGNORED));
                    p->SetCurrentInfoAll(info);
                };

                // CreateRTV
                imageType["create_rtv"] = [](LuaImage &img) {
                    Image *p = img.Get();
                    if (p) p->CreateRTV();
                };

                // CreateSRV
                imageType["create_srv"] = sol::overload(
                    [](LuaImage &img, const std::string &viewType) {
                        Image *p = img.Get();
                        if (p) p->CreateSRV(Lookup(viewType, s_imageViewTypeMap, vk::ImageViewType::e2D));
                    },
                    [](LuaImage &img, const std::string &viewType, int mip) {
                        Image *p = img.Get();
                        if (p) p->CreateSRV(Lookup(viewType, s_imageViewTypeMap, vk::ImageViewType::e2D), mip);
                    });

                // CreateUAV
                imageType["create_uav"] = [](LuaImage &img, const std::string &viewType, uint32_t mip) {
                    Image *p = img.Get();
                    if (p) p->CreateUAV(Lookup(viewType, s_imageViewTypeMap, vk::ImageViewType::e2D), mip);
                };

                // HasRTV
                imageType["has_rtv"] = [](LuaImage &img) -> bool {
                    Image *p = img.Get();
                    return p ? p->HasRTV() : false;
                };

                // HasSRV
                imageType["has_srv"] = sol::overload(
                    [](LuaImage &img) -> bool {
                        Image *p = img.Get();
                        return p ? p->HasSRV() : false;
                    },
                    [](LuaImage &img, int mip) -> bool {
                        Image *p = img.Get();
                        return p ? p->HasSRV(mip) : false;
                    });

                // HasUAV
                imageType["has_uav"] = [](LuaImage &img, uint32_t mip) -> bool {
                    Image *p = img.Get();
                    return p ? p->HasUAV(mip) : false;
                };

                // GetRTV
                imageType["get_rtv"] = [](LuaImage &img) -> ImageView * {
                    Image *p = img.Get();
                    return p ? p->GetRTV() : nullptr;
                };

                // GetSRV
                imageType["get_srv"] = sol::overload(
                    [](LuaImage &img) -> ImageView * {
                        Image *p = img.Get();
                        return p ? p->GetSRV() : nullptr;
                    },
                    [](LuaImage &img, int mip) -> ImageView * {
                        Image *p = img.Get();
                        return p ? p->GetSRV(mip) : nullptr;
                    });

                // GetUAV
                imageType["get_uav"] = [](LuaImage &img, uint32_t mip) -> ImageView * {
                    Image *p = img.Get();
                    return p ? p->GetUAV(mip) : nullptr;
                };

                // SetRTV
                imageType["set_rtv"] = [](LuaImage &img, ImageView *view) {
                    Image *p = img.Get();
                    if (p) p->SetRTV(view);
                };

                // HasGeneratedMips
                imageType["has_generated_mips"] = sol::property([](LuaImage &img) -> bool {
                    Image *p = img.Get();
                    return p ? p->HasGeneratedMips() : false;
                });

                // GetClearColor
                imageType["get_clear_color"] = [](LuaImage &img) -> std::tuple<float, float, float, float> {
                    Image *p = img.Get();
                    if (!p) return {0.f, 0.f, 0.f, 0.f};
                    vec4 c = p->GetClearColor();
                    return {c.x, c.y, c.z, c.w};
                };

                // SetClearColor
                imageType["set_clear_color"] = [](LuaImage &img, float r, float g, float b, float a) {
                    Image *p = img.Get();
                    if (p) p->SetClearColor(vec4(r, g, b, a));
                };

                // --- Static functions ---

                // CalculateMips
                lua.set_function("calculate_mips", [](uint32_t width, uint32_t height) -> uint32_t {
                    return Image::CalculateMips(width, height);
                });

                // LoadRGBA
                lua.set_function("load_image_rgba", [](CommandBuffer *cmd, const std::string &path, const std::string &format, sol::optional<bool> isFloat) -> std::shared_ptr<LuaImage> {
                    Image *img = Image::LoadRGBA(cmd, Path::Assets + path, ToImageFormat(format), isFloat.value_or(false));
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // LoadRGBA8
                lua.set_function("load_image_rgba8", [](CommandBuffer *cmd, const std::string &path) -> std::shared_ptr<LuaImage> {
                    Image *img = Image::LoadRGBA8(cmd, Path::Assets + path);
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // LoadRGBA32F
                lua.set_function("load_image_rgba32f", [](CommandBuffer *cmd, const std::string &path) -> std::shared_ptr<LuaImage> {
                    Image *img = Image::LoadRGBA32F(cmd, Path::Assets + path);
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // LoadRaw
                lua.set_function("load_image_raw", [](CommandBuffer *cmd, const std::string &path, const sol::table &params) -> std::shared_ptr<LuaImage> {
                    Image::LoadRawParams p{};
                    p.width = params.get<uint32_t>("width");
                    p.height = params.get<uint32_t>("height");
                    p.format = ToImageFormat(params.get<std::string>("format"));
                    p.generateMips = params.get_or("generate_mips", false);
                    p.clampToEdge = params.get_or("clamp_to_edge", false);
                    p.mipLodBias = params.get_or("mip_lod_bias", 0.0f);
                    Image *img = Image::LoadRaw(cmd, Path::Assets + path, p);
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // Factory: create_image(width, height, format, usage, name [, mipLevels])
                lua.set_function("create_image", sol::overload(
                    [](uint32_t width, uint32_t height, const std::string &format, const std::string &usage, const std::string &name) -> std::shared_ptr<LuaImage> {
                        vk::ImageCreateInfo info = Image::CreateInfoInit();
                        info.format = ToImageFormat(format);
                        info.extent = vk::Extent3D{width, height, 1u};
                        info.usage = ToImageUsage(usage);
                        Image *img = Image::Create(info, name);
                        if (!img) return nullptr;
                        auto luaImg = std::make_shared<LuaImage>();
                        luaImg->ptr = img;
                        luaImg->owned = true;
                        return luaImg;
                    },
                    [](uint32_t width, uint32_t height, const std::string &format, const std::string &usage, const std::string &name, uint32_t mipLevels) -> std::shared_ptr<LuaImage> {
                        vk::ImageCreateInfo info = Image::CreateInfoInit();
                        info.format = ToImageFormat(format);
                        info.extent = vk::Extent3D{width, height, 1u};
                        info.usage = ToImageUsage(usage);
                        info.mipLevels = mipLevels;
                        Image *img = Image::Create(info, name);
                        if (!img) return nullptr;
                        auto luaImg = std::make_shared<LuaImage>();
                        luaImg->ptr = img;
                        luaImg->owned = true;
                        return luaImg;
                    }));

                // destroy_image
                lua.set_function("destroy_image", [](std::shared_ptr<LuaImage> img) {
                    if (img && img->ptr && img->owned)
                    {
                        // Lua tests create and destroy many transient images; invalidate cached
                        // framebuffers so later pointer reuse can't resurrect stale attachments.
                        CommandBuffer::ClearFramebufferCache();
                        Image::Destroy(img->ptr);
                        img->ptr = nullptr;
                    }
                }); });
        }
    } s_imageBindings;
} // namespace pe
#endif
