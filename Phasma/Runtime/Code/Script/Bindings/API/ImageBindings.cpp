#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"

namespace pe
{
    static const std::unordered_map<std::string_view, ::PeFormat> s_imageFormatMap = {
        {"rgba8", PE_FORMAT_R8G8B8A8_UNORM},
        {"rgba8_srgb", PE_FORMAT_R8G8B8A8_SRGB},
        {"bgra8", PE_FORMAT_B8G8R8A8_UNORM},
        {"bgra8_srgb", PE_FORMAT_B8G8R8A8_SRGB},
        {"rgba16f", PE_FORMAT_R16G16B16A16_SFLOAT},
        {"rgba32f", PE_FORMAT_R32G32B32A32_SFLOAT},
        {"rg16f", PE_FORMAT_R16G16_SFLOAT},
        {"rg32f", PE_FORMAT_R32G32_SFLOAT},
        {"r8", PE_FORMAT_R8_UNORM},
        {"r16f", PE_FORMAT_R16_SFLOAT},
        {"r32f", PE_FORMAT_R32_SFLOAT},
        {"d32f", PE_FORMAT_D32_SFLOAT},
        {"d24_s8", PE_FORMAT_D24_UNORM_S8_UINT},
        {"d32f_s8", PE_FORMAT_D32_SFLOAT_S8_UINT},
        {"s8", PE_FORMAT_S8_UINT},
        {"rgb10a2", PE_FORMAT_A2B10G10R10_UNORM_PACK32},
        {"rg11b10f", PE_FORMAT_B10G11R11_UFLOAT_PACK32},
    };

    static const std::unordered_map<std::string_view, PeImageUsageFlags> s_imageUsageMap = {
        {"sampled", PE_IMAGE_USAGE_SAMPLED},
        {"storage", PE_IMAGE_USAGE_STORAGE},
        {"color_attachment", PE_IMAGE_USAGE_COLOR_ATTACHMENT},
        {"depth_attachment", PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT},
        {"transfer_src", PE_IMAGE_USAGE_TRANSFER_SRC},
        {"transfer_dst", PE_IMAGE_USAGE_TRANSFER_DST},
        {"input_attachment", PE_IMAGE_USAGE_INPUT_ATTACHMENT},
    };

    static const std::unordered_map<std::string_view, PeImageViewType> s_imageViewTypeMap = {
        {"1d", PE_IMAGE_VIEW_TYPE_1D},
        {"2d", PE_IMAGE_VIEW_TYPE_2D},
        {"3d", PE_IMAGE_VIEW_TYPE_3D},
        {"cube", PE_IMAGE_VIEW_TYPE_CUBE},
        {"1d_array", PE_IMAGE_VIEW_TYPE_1D_ARRAY},
        {"2d_array", PE_IMAGE_VIEW_TYPE_2D_ARRAY},
        {"cube_array", PE_IMAGE_VIEW_TYPE_CUBE_ARRAY},
    };

    static const std::unordered_map<std::string_view, PeImageLayout> s_imgLayoutMap = {
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

    static const std::unordered_map<std::string_view, PeBarrierSync> s_imgStageMap = {
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
        {"all_graphics", PE_STAGE_ALL_GRAPHICS},
        {"all_commands", PE_STAGE_ALL_COMMANDS},
    };

    static const std::unordered_map<std::string_view, PeBarrierAccess> s_imgAccessMap = {
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

    static ::PeFormat ToImageFormat(const std::string &s)
    {
        return Lookup(s, s_imageFormatMap, PE_FORMAT_R8G8B8A8_UNORM);
    }
    static PeImageUsageFlags ToImageUsage(const std::string &s)
    {
        return LookupFlags<PeImageUsageFlags>(s, s_imageUsageMap);
    }

    // Reverse lookup: ::PeFormat -> string
    static std::string FormatToString(::PeFormat fmt)
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
                imageType["get_current_info"] = [](LuaImage &img, sol::optional<uint32_t> layer, sol::optional<uint32_t> mip, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    Image *p = img.Get();
                    sol::table t = lua.create_table();
                    if (!p) return t;
                    auto &info = p->GetCurrentInfo(layer.value_or(0), mip.value_or(0));
                    t["layout"] = static_cast<int>(info.layout);
                    t["stage_flags"] = static_cast<uint64_t>(info.stageFlags);
                    t["access_mask"] = static_cast<uint64_t>(info.accessMask);
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
                    info.layout = Lookup(t.get_or<std::string>("layout", "undefined"), s_imgLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED);
                    info.stageFlags = LookupFlags<PeBarrierSync>(t.get_or<std::string>("stage_flags", "none"), s_imgStageMap);
                    info.accessMask = LookupFlags<PeBarrierAccess>(t.get_or<std::string>("access_mask", "none"), s_imgAccessMap);
                    info.baseArrayLayer = t.get_or<uint32_t>("base_array_layer", 0);
                    info.arrayLayers = t.get_or<uint32_t>("array_layers", 0);
                    info.baseMipLevel = t.get_or<uint32_t>("base_mip_level", 0);
                    info.mipLevels = t.get_or<uint32_t>("mip_levels", 0);
                    info.queueFamilyId = t.get_or("queue_family_id", PE_QUEUE_FAMILY_IGNORED);
                    p->SetCurrentInfo(info, layer.value_or(0), mip.value_or(0));
                };

                // SetCurrentInfoAll
                imageType["set_current_info_all"] = [](LuaImage &img, const sol::table &t) {
                    Image *p = img.Get();
                    if (!p) return;
                    ImageTrackInfo info{};
                    info.image = p;
                    info.layout = Lookup(t.get_or<std::string>("layout", "undefined"), s_imgLayoutMap, PE_IMAGE_LAYOUT_UNDEFINED);
                    info.stageFlags = LookupFlags<PeBarrierSync>(t.get_or<std::string>("stage_flags", "none"), s_imgStageMap);
                    info.accessMask = LookupFlags<PeBarrierAccess>(t.get_or<std::string>("access_mask", "none"), s_imgAccessMap);
                    info.baseArrayLayer = t.get_or<uint32_t>("base_array_layer", 0);
                    info.arrayLayers = t.get_or<uint32_t>("array_layers", 0);
                    info.baseMipLevel = t.get_or<uint32_t>("base_mip_level", 0);
                    info.mipLevels = t.get_or<uint32_t>("mip_levels", 0);
                    info.queueFamilyId = t.get_or("queue_family_id", PE_QUEUE_FAMILY_IGNORED);
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
                        if (p) p->CreateSRV(Lookup(viewType, s_imageViewTypeMap, PE_IMAGE_VIEW_TYPE_2D));
                    },
                    [](LuaImage &img, const std::string &viewType, int mip) {
                        Image *p = img.Get();
                        if (p) p->CreateSRV(Lookup(viewType, s_imageViewTypeMap, PE_IMAGE_VIEW_TYPE_2D), mip);
                    });

                // CreateUAV
                imageType["create_uav"] = [](LuaImage &img, const std::string &viewType, uint32_t mip) {
                    Image *p = img.Get();
                    if (p) p->CreateUAV(Lookup(viewType, s_imageViewTypeMap, PE_IMAGE_VIEW_TYPE_2D), mip);
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
                    Image *img = Image::LoadRGBA(cmd, Path::ResolveAsset(path), ToImageFormat(format), isFloat.value_or(false));
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // LoadRGBA8
                lua.set_function("load_image_rgba8", [](CommandBuffer *cmd, const std::string &path) -> std::shared_ptr<LuaImage> {
                    Image *img = Image::LoadRGBA8(cmd, Path::ResolveAsset(path));
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // LoadRGBA32F
                lua.set_function("load_image_rgba32f", [](CommandBuffer *cmd, const std::string &path) -> std::shared_ptr<LuaImage> {
                    Image *img = Image::LoadRGBA32F(cmd, Path::ResolveAsset(path));
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
                    Image *img = Image::LoadRaw(cmd, Path::ResolveAsset(path), p);
                    if (!img) return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = true;
                    return luaImg;
                });

                // Factory: create_image(width, height, format, usage, name [, mipLevels])
                lua.set_function("create_image", sol::overload(
                    [](uint32_t width, uint32_t height, const std::string &format, const std::string &usage, const std::string &name) -> std::shared_ptr<LuaImage> {
                        ImageDesc desc{};
                        desc.format = ToImageFormat(format);
                        desc.width = width;
                        desc.height = height;
                        desc.usage = ToImageUsage(usage);
                        desc.name = name;
                        Image *img = Image::Create(desc);
                        if (!img) return nullptr;
                        auto luaImg = std::make_shared<LuaImage>();
                        luaImg->ptr = img;
                        luaImg->owned = true;
                        return luaImg;
                    },
                    [](uint32_t width, uint32_t height, const std::string &format, const std::string &usage, const std::string &name, uint32_t mipLevels) -> std::shared_ptr<LuaImage> {
                        ImageDesc desc{};
                        desc.format = ToImageFormat(format);
                        desc.width = width;
                        desc.height = height;
                        desc.usage = ToImageUsage(usage);
                        desc.mipLevels = mipLevels;
                        desc.name = name;
                        Image *img = Image::Create(desc);
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
                        // Lua tests create transient images that may have been used to build
                        // cached framebuffers on the main queue
                        if (auto *queue = RHII.GetMainQueue())
                            queue->WaitIdle();
                        CommandBuffer::ClearFramebufferCache();
                        Image::Destroy(img->ptr);
                        img->ptr = nullptr;
                    }
                }); });
        }
    } s_imageBindings;
} // namespace pe
