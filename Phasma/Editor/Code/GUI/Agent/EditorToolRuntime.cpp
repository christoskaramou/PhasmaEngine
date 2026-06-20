#include "GUI/Agent/EditorToolRuntime.h"
#include "GUI/Widgets/ProfilerWidget.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Surface.h"
#include "Camera/Camera.h"
#include "GUI/SpriteAuthoring.h"
#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/ModelAssetCooked.h"
#include "Scene/Primitives.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"
#include "Phasma/MCP/Utils.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "stb_image.h"
#include <fstream>
#include <numeric>
#include <thread>

#if defined(PE_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace pmcp;

namespace pe
{
    namespace
    {
        static std::string ToLower(std::string s)
        {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        static std::vector<uint8_t> ResizeRGBA(const uint8_t *src, int srcW, int srcH, int &outW, int &outH, int maxDim = 1024)
        {
            if (srcW <= 0 || srcH <= 0 || maxDim <= 0)
            {
                outW = outH = 0;
                return {};
            }
            float scale = static_cast<float>(maxDim) / static_cast<float>(std::max(srcW, srcH));
            outW = static_cast<int>(srcW * scale);
            outH = static_cast<int>(srcH * scale);
            std::vector<uint8_t> resized(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4);
            for (int dy = 0; dy < outH; dy++)
            {
                int sy = dy * srcH / outH;
                for (int dx = 0; dx < outW; dx++)
                {
                    int sx = dx * srcW / outW;
                    std::memcpy(&resized[(static_cast<size_t>(dy) * outW + dx) * 4],
                                &src[(static_cast<size_t>(sy) * srcW + sx) * 4], 4);
                }
            }
            return resized;
        }

        static nlohmann::json Vec3Json(const vec3 &v)
        {
            return nlohmann::json::array({v.x, v.y, v.z});
        }

        static nlohmann::json Vec4Json(const vec4 &v)
        {
            return nlohmann::json::array({v.x, v.y, v.z, v.w});
        }

        static nlohmann::json Mat4Json(const mat4 &m)
        {
            nlohmann::json rows = nlohmann::json::array();
            for (int r = 0; r < 4; ++r)
            {
                nlohmann::json row = nlohmann::json::array();
                for (int c = 0; c < 4; ++c)
                    row.push_back(m[c][r]);
                rows.push_back(std::move(row));
            }
            return rows;
        }

        static nlohmann::json AabbJson(const AABB &aabb)
        {
            return nlohmann::json{{"min", Vec3Json(aabb.min)}, {"max", Vec3Json(aabb.max)}};
        }

        static bool ReadVec3Json(const nlohmann::json &value, vec3 &out)
        {
            if (!value.is_array() || value.size() < 3)
                return false;
            for (int i = 0; i < 3; ++i)
            {
                if (!value[i].is_number())
                    return false;
            }
            out = vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
            return true;
        }

        static bool IsFiniteVec3(const vec3 &v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        static void ApplyCameraOrientationFromDirection(Camera *camera, const vec3 &direction)
        {
            if (!camera)
                return;

            const float len = glm::length(direction);
            if (!std::isfinite(len) || len < 1e-6f)
                return;

            vec3 front = direction / len;
            if (!IsFiniteVec3(front))
                return;

            const float pitch = -std::asin(glm::clamp(front.y, -1.0f, 1.0f));
            float yaw = camera->GetEuler().y;
            if (std::abs(front.x) > 1e-5f || std::abs(front.z) > 1e-5f)
                yaw = std::atan2(front.x, front.z);

            camera->SetEuler(vec3(pitch, yaw, 0.0f));
        }

        static std::string SceneViewAspectModeName(SceneViewAspectMode mode)
        {
            switch (mode)
            {
            case SceneViewAspectMode::Free:
                return "free";
            case SceneViewAspectMode::Landscape16x9:
                return "landscape_16x9";
            case SceneViewAspectMode::Portrait9x16:
                return "portrait_9x16";
            case SceneViewAspectMode::Landscape19_5x9:
                return "landscape_19_5x9";
            case SceneViewAspectMode::Portrait9x19_5:
                return "portrait_9x19_5";
            case SceneViewAspectMode::Square1x1:
                return "square_1x1";
            default:
                return "unknown";
            }
        }

        static std::string RenderModeName(RenderMode mode)
        {
            switch (mode)
            {
            case RenderMode::Raster:
                return "raster";
            case RenderMode::Hybrid:
                return "hybrid";
            case RenderMode::RayTracing:
                return "ray_tracing";
            default:
                return "unknown";
            }
        }

        static std::string ImageId(Image *image)
        {
            std::ostringstream os;
            os << "image:0x" << std::hex << reinterpret_cast<uintptr_t>(image);
            return os.str();
        }

        static uint32_t MipExtent(uint32_t value, uint32_t mip)
        {
            return std::max(value >> mip, 1u);
        }

        static bool IsImageFileExt(const std::string &ext)
        {
            const std::string lower = ToLower(ext);
            return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".tga" ||
                   lower == ".bmp" || lower == ".hdr" || lower == ".dds";
        }

        static size_t FormatBytesPerPixel(::PeFormat format)
        {
            switch (format)
            {
            case PE_FORMAT_R8_UNORM:
            case PE_FORMAT_R8_SINT:
            case PE_FORMAT_R8_UINT:
                return 1;
            case PE_FORMAT_R16_SFLOAT:
            case PE_FORMAT_R16_SINT:
            case PE_FORMAT_R16_UINT:
                return 2;
            case PE_FORMAT_R32_SFLOAT:
            case PE_FORMAT_R32_SINT:
            case PE_FORMAT_R32_UINT:
            case PE_FORMAT_R16G16_SFLOAT:
            case PE_FORMAT_R8G8B8A8_UNORM:
            case PE_FORMAT_R8G8B8A8_SRGB:
            case PE_FORMAT_B8G8R8A8_UNORM:
            case PE_FORMAT_B8G8R8A8_SRGB:
            case PE_FORMAT_D32_SFLOAT:
                return 4;
            case PE_FORMAT_R16G16B16_SFLOAT:
                return 6;
            case PE_FORMAT_R16G16B16A16_SFLOAT:
            case PE_FORMAT_R32G32_SFLOAT:
            case PE_FORMAT_R32G32_SINT:
            case PE_FORMAT_R32G32_UINT:
                return 8;
            case PE_FORMAT_R32G32B32_SFLOAT:
            case PE_FORMAT_R32G32B32_SINT:
            case PE_FORMAT_R32G32B32_UINT:
                return 12;
            case PE_FORMAT_R32G32B32A32_SFLOAT:
            case PE_FORMAT_R32G32B32A32_SINT:
            case PE_FORMAT_R32G32B32A32_UINT:
                return 16;
            default:
                return 0;
            }
        }

        static float HalfToFloat(uint16_t h)
        {
            const uint32_t sign = (h & 0x8000u) << 16;
            uint32_t exp = (h >> 10) & 0x1Fu;
            uint32_t mant = h & 0x03FFu;
            uint32_t bits = 0;
            if (exp == 0)
            {
                if (mant == 0)
                {
                    bits = sign;
                }
                else
                {
                    exp = 1;
                    while ((mant & 0x0400u) == 0)
                    {
                        mant <<= 1;
                        --exp;
                    }
                    mant &= 0x03FFu;
                    bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
                }
            }
            else if (exp == 31)
            {
                bits = sign | 0x7F800000u | (mant << 13);
            }
            else
            {
                bits = sign | ((exp + 127u - 15u) << 23) | (mant << 13);
            }

            float out = 0.0f;
            std::memcpy(&out, &bits, sizeof(out));
            return out;
        }

        static uint8_t ToByte(float v)
        {
            if (!std::isfinite(v))
                v = 0.0f;
            v = glm::clamp(v, 0.0f, 1.0f);
            return static_cast<uint8_t>(v * 255.0f + 0.5f);
        }

        static uint16_t ReadU16(const uint8_t *p)
        {
            uint16_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        static float ReadF32(const uint8_t *p)
        {
            float v = 0.0f;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        static bool ConvertImageRowsToRGBA(const uint8_t *src,
                                           uint32_t width,
                                           uint32_t height,
                                           size_t rowPitch,
                                           ::PeFormat format,
                                           const std::string &visualize,
                                           std::vector<uint8_t> &rgba,
                                           std::string &error)
        {
            const size_t bpp = FormatBytesPerPixel(format);
            if (!src || width == 0 || height == 0 || bpp == 0)
            {
                error = "unsupported image format for PNG conversion";
                return false;
            }

            rgba.assign(static_cast<size_t>(width) * height * 4, 255);
            const bool normalView = visualize == "normal";
            const bool velocityView = visualize == "velocity";
            const bool depthView = visualize == "depth" || visualize == "linear_depth";

            for (uint32_t y = 0; y < height; ++y)
            {
                const uint8_t *row = src + static_cast<size_t>(y) * rowPitch;
                uint8_t *out = rgba.data() + static_cast<size_t>(y) * width * 4;
                for (uint32_t x = 0; x < width; ++x)
                {
                    const uint8_t *p = row + static_cast<size_t>(x) * bpp;
                    uint8_t *d = out + static_cast<size_t>(x) * 4;
                    switch (format)
                    {
                    case PE_FORMAT_R8_UNORM:
                    {
                        d[0] = d[1] = d[2] = p[0];
                        d[3] = 255;
                        break;
                    }
                    case PE_FORMAT_R8G8B8A8_UNORM:
                    case PE_FORMAT_R8G8B8A8_SRGB:
                    {
                        d[0] = p[0];
                        d[1] = p[1];
                        d[2] = p[2];
                        d[3] = p[3];
                        break;
                    }
                    case PE_FORMAT_B8G8R8A8_UNORM:
                    case PE_FORMAT_B8G8R8A8_SRGB:
                    {
                        d[0] = p[2];
                        d[1] = p[1];
                        d[2] = p[0];
                        d[3] = p[3];
                        break;
                    }
                    case PE_FORMAT_R16_SFLOAT:
                    {
                        const float v = HalfToFloat(ReadU16(p));
                        const uint8_t b = ToByte(depthView ? v : v);
                        d[0] = d[1] = d[2] = b;
                        d[3] = 255;
                        break;
                    }
                    case PE_FORMAT_R16G16_SFLOAT:
                    {
                        const float vx = HalfToFloat(ReadU16(p));
                        const float vy = HalfToFloat(ReadU16(p + 2));
                        d[0] = ToByte(velocityView ? vx * 0.5f + 0.5f : vx);
                        d[1] = ToByte(velocityView ? vy * 0.5f + 0.5f : vy);
                        d[2] = ToByte(velocityView ? glm::length(vec2(vx, vy)) : 0.0f);
                        d[3] = 255;
                        break;
                    }
                    case PE_FORMAT_R16G16B16_SFLOAT:
                    case PE_FORMAT_R16G16B16A16_SFLOAT:
                    {
                        const float r = HalfToFloat(ReadU16(p));
                        const float g = HalfToFloat(ReadU16(p + 2));
                        const float b = HalfToFloat(ReadU16(p + 4));
                        const float a = format == PE_FORMAT_R16G16B16A16_SFLOAT ? HalfToFloat(ReadU16(p + 6)) : 1.0f;
                        d[0] = ToByte(normalView ? r * 0.5f + 0.5f : r);
                        d[1] = ToByte(normalView ? g * 0.5f + 0.5f : g);
                        d[2] = ToByte(normalView ? b * 0.5f + 0.5f : b);
                        d[3] = ToByte(a);
                        break;
                    }
                    case PE_FORMAT_R32_SFLOAT:
                    case PE_FORMAT_D32_SFLOAT:
                    {
                        const float v = ReadF32(p);
                        const uint8_t b = ToByte(v);
                        d[0] = d[1] = d[2] = b;
                        d[3] = 255;
                        break;
                    }
                    case PE_FORMAT_R32G32_SFLOAT:
                    {
                        const float vx = ReadF32(p);
                        const float vy = ReadF32(p + 4);
                        d[0] = ToByte(velocityView ? vx * 0.5f + 0.5f : vx);
                        d[1] = ToByte(velocityView ? vy * 0.5f + 0.5f : vy);
                        d[2] = ToByte(velocityView ? glm::length(vec2(vx, vy)) : 0.0f);
                        d[3] = 255;
                        break;
                    }
                    case PE_FORMAT_R32G32B32_SFLOAT:
                    case PE_FORMAT_R32G32B32A32_SFLOAT:
                    {
                        const float r = ReadF32(p);
                        const float g = ReadF32(p + 4);
                        const float b = ReadF32(p + 8);
                        const float a = format == PE_FORMAT_R32G32B32A32_SFLOAT ? ReadF32(p + 12) : 1.0f;
                        d[0] = ToByte(normalView ? r * 0.5f + 0.5f : r);
                        d[1] = ToByte(normalView ? g * 0.5f + 0.5f : g);
                        d[2] = ToByte(normalView ? b * 0.5f + 0.5f : b);
                        d[3] = ToByte(a);
                        break;
                    }
                    default:
                        error = "unsupported image format for PNG conversion";
                        return false;
                    }
                }
            }
            return true;
        }

        static std::filesystem::path ResolveAssetPath(const std::string &path)
        {
            std::filesystem::path p(path);
            if (p.is_absolute())
                return p;
            return std::filesystem::path(Path::Assets) / p;
        }

        static std::string PathUtf8(const std::filesystem::path &path)
        {
            const auto u8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
        }

        static std::filesystem::path U8Path(const std::string &utf8)
        {
            return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
        }

        static std::filesystem::path ResolveExistingAssetCandidate(const std::string &path, std::string_view subdir)
        {
            std::filesystem::path p = U8Path(path);
            if (p.is_absolute())
                return p;

            const std::filesystem::path assets = U8Path(Path::Assets);
            std::array<std::filesystem::path, 4> candidates = {
                assets / p,
                assets / std::filesystem::path(std::string(subdir)) / p,
                U8Path(Path::Root) / p,
                p};

            for (const auto &candidate : candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec))
                    return candidate;
            }
            return candidates[0];
        }

        static std::filesystem::path ResolveTexturePathForTool(const std::string &path)
        {
            std::filesystem::path p = U8Path(path);
            if (p.is_absolute())
                return p;

            const std::filesystem::path assets = U8Path(Path::Assets);
            std::array<std::filesystem::path, 4> candidates = {
                assets / p,
                assets / "Textures" / p,
                assets / "Objects" / p,
                p};

            for (const auto &candidate : candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec))
                    return candidate;
            }
            return candidates[0];
        }

        static std::string RenderTypeName(RenderType type)
        {
            switch (type)
            {
            case RenderType::Opaque:
                return "opaque";
            case RenderType::AlphaCut:
                return "alpha_cut";
            case RenderType::AlphaBlend:
                return "alpha_blend";
            case RenderType::Transmission:
                return "transmission";
            case RenderType::Lines:
                return "lines";
            default:
                return "unknown";
            }
        }

        static std::string LightTypeName(LightType type)
        {
            switch (type)
            {
            case LightType::Directional:
                return "directional";
            case LightType::Point:
                return "point";
            case LightType::Spot:
                return "spot";
            case LightType::Area:
                return "area";
            default:
                return "unknown";
            }
        }

        static nlohmann::json ComponentFlagsJson(uint32_t flags)
        {
            nlohmann::json components = nlohmann::json::array();
            auto add = [&](uint32_t bit, const char *name)
            {
                if (flags & bit)
                    components.push_back(name);
            };
            add(Component_Mesh, "mesh");
            add(Component_Light, "light");
            add(Component_Physics, "physics");
            add(Component_Camera, "camera");
            add(Component_Script, "script");
            add(Component_Audio, "audio");
            add(Component_GpuPending, "gpu_pending");
            add(Component_Skybox, "skybox");
            add(Component_RuntimeUi, "runtime_ui");
            add(Component_Physics2D, "physics2d");
            add(Component_Prefab, "prefab");
            add(Component_Sprite, "sprite");
            return components;
        }

        static const char *TextureSlotName(int slot)
        {
            switch (static_cast<TextureType>(slot))
            {
            case TextureType::BaseColor:
                return "base_color";
            case TextureType::MetallicRoughness:
                return "metallic_roughness";
            case TextureType::Normal:
                return "normal";
            case TextureType::Occlusion:
                return "occlusion";
            case TextureType::Emissive:
                return "emissive";
            default:
                return "unknown";
            }
        }

        static bool TextureSlotFromName(const std::string &name, TextureType &out)
        {
            const std::string key = ToLower(name);
            static const std::unordered_map<std::string, TextureType> slots = {
                {"base_color", TextureType::BaseColor},
                {"basecolor", TextureType::BaseColor},
                {"albedo", TextureType::BaseColor},
                {"diffuse", TextureType::BaseColor},
                {"metallic_roughness", TextureType::MetallicRoughness},
                {"metallicroughness", TextureType::MetallicRoughness},
                {"roughness", TextureType::MetallicRoughness},
                {"normal", TextureType::Normal},
                {"occlusion", TextureType::Occlusion},
                {"ao", TextureType::Occlusion},
                {"emissive", TextureType::Emissive},
            };
            auto it = slots.find(key);
            if (it == slots.end())
                return false;
            out = it->second;
            return true;
        }

        static ResourceHandle<Image> DefaultTextureForSlot(TextureType slot)
        {
            const auto &defaults = ModelAsset::GetDefaultResources();
            switch (slot)
            {
            case TextureType::Normal:
                return ResourceHandle<Image>::FromRaw(defaults.normal);
            case TextureType::Emissive:
                return ResourceHandle<Image>::FromRaw(defaults.black);
            default:
                return ResourceHandle<Image>::FromRaw(defaults.white);
            }
        }

        static ResourceHandle<Image> LoadTextureForTool(const std::string &path, std::string &outError)
        {
            Queue *queue = RHII.GetMainQueue();
            if (!queue)
            {
                outError = "main queue not available";
                return ResourceHandle<Image>();
            }

            const std::filesystem::path resolved = ResolveTexturePathForTool(path);
            std::error_code ec;
            if (!std::filesystem::exists(resolved, ec))
            {
                outError = "texture file not found: " + path;
                return ResourceHandle<Image>();
            }

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            ModelAsset loader;
            ResourceHandle<Image> image = loader.LoadTexture(cmd, resolved);
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);

            if (!image)
                outError = "failed to load texture: " + path;
            return image;
        }

        static std::string BufferId(Buffer *buffer)
        {
            std::ostringstream os;
            os << "buffer:0x" << std::hex << reinterpret_cast<uintptr_t>(buffer);
            return os.str();
        }

        static std::string MemoryUsageName(PeMemoryUsage usage)
        {
            switch (usage)
            {
            case PE_MEMORY_USAGE_GPU_ONLY:
                return "gpu_only";
            case PE_MEMORY_USAGE_GPU_ONLY_DEDICATED:
                return "gpu_only_dedicated";
            case PE_MEMORY_USAGE_CPU_TO_GPU:
                return "cpu_to_gpu";
            case PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT:
                return "cpu_to_gpu_persistent";
            case PE_MEMORY_USAGE_GPU_TO_CPU:
                return "gpu_to_cpu";
            case PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT:
                return "gpu_to_cpu_persistent";
            default:
                return "unknown";
            }
        }

        static bool IsHostReadableMemory(PeMemoryUsage usage)
        {
            return usage == PE_MEMORY_USAGE_GPU_TO_CPU ||
                   usage == PE_MEMORY_USAGE_GPU_TO_CPU_PERSISTENT ||
                   usage == PE_MEMORY_USAGE_CPU_TO_GPU ||
                   usage == PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT;
        }

        static nlohmann::json BufferUsageJson(PeBufferUsageFlags usage)
        {
            nlohmann::json out = nlohmann::json::array();
            auto add = [&](PeBufferUsageFlags bit, const char *name)
            {
                if (usage & bit)
                    out.push_back(name);
            };
            add(PE_BUFFER_USAGE_TRANSFER_SRC, "transfer_src");
            add(PE_BUFFER_USAGE_TRANSFER_DST, "transfer_dst");
            add(PE_BUFFER_USAGE_UNIFORM_BUFFER, "uniform");
            add(PE_BUFFER_USAGE_STORAGE_BUFFER, "storage");
            add(PE_BUFFER_USAGE_INDEX_BUFFER, "index");
            add(PE_BUFFER_USAGE_VERTEX_BUFFER, "vertex");
            add(PE_BUFFER_USAGE_INDIRECT_BUFFER, "indirect");
            add(PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS, "device_address");
            add(PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR, "acceleration_structure");
            add(PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR, "accel_build_input");
            add(PE_BUFFER_USAGE_SHADER_BINDING_TABLE_KHR, "shader_binding_table");
            add(PE_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER, "resource_descriptor_buffer");
            add(PE_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER, "sampler_descriptor_buffer");
            return out;
        }

        static std::string HexEncode(const uint8_t *data, size_t size)
        {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string out;
            out.resize(size * 2);
            for (size_t i = 0; i < size; ++i)
            {
                out[i * 2] = kHex[data[i] >> 4];
                out[i * 2 + 1] = kHex[data[i] & 0x0F];
            }
            return out;
        }

        static std::string SanitizeFileStem(std::string name)
        {
            if (name.empty())
                name = "resource";
            for (char &c : name)
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
                    c = '_';
            }
            return name;
        }

        static bool CookTargetUpToDate(const std::filesystem::path &src, const std::filesystem::path &outPemesh)
        {
            std::error_code ec;
            if (!std::filesystem::exists(outPemesh, ec))
                return false;
            const auto srcTime = std::filesystem::last_write_time(src, ec);
            if (ec)
                return false;
            const auto outTime = std::filesystem::last_write_time(outPemesh, ec);
            if (ec)
                return false;
            return outTime >= srcTime;
        }

        static std::filesystem::path PhasmaCookExePath()
        {
            const std::filesystem::path exeDir(reinterpret_cast<const char8_t *>(Path::Executable.c_str()));
#if defined(PE_WIN32)
            return exeDir / "PhasmaCook.exe";
#else
            return exeDir / "PhasmaCook";
#endif
        }

        static bool RunPhasmaCookProcess(const std::filesystem::path &manifest)
        {
            const std::filesystem::path exe = PhasmaCookExePath();
            std::error_code ec;
            if (!std::filesystem::exists(exe, ec))
                return false;

#if defined(PE_WIN32)
            std::wstring cmd = L"\"" + exe.wstring() + L"\" \"--batch\" \"" + manifest.wstring() + L"\"";
            std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back(L'\0');

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(exe.wstring().c_str(), cmdBuf.data(), nullptr, nullptr, FALSE,
                                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                return false;

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return exitCode == 0;
#else
            std::string exeStr = exe.string();
            std::string manifestStr = manifest.string();
            std::array<char *, 4> argv = {exeStr.data(), const_cast<char *>("--batch"), manifestStr.data(), nullptr};
            const pid_t pid = fork();
            if (pid < 0)
                return false;
            if (pid == 0)
            {
                execv(exeStr.c_str(), argv.data());
                _exit(127);
            }
            int status = 0;
            int rc = 0;
            do
            {
                rc = waitpid(pid, &status, 0);
            }
            while (rc < 0 && errno == EINTR);
            return rc >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
        }

        struct ImportJob
        {
            std::string id;
            std::string status = "queued";
            std::string error;
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> jobs;
            std::vector<std::filesystem::path> cooked;
            std::vector<std::filesystem::path> skipped;
            std::vector<std::string> failed;
            uint32_t progress = 0;
            uint32_t total = 0;
            bool processOk = false;
            std::chrono::steady_clock::time_point started{};
            std::chrono::steady_clock::time_point finished{};
        };

        static std::mutex s_importJobsMutex;
        static std::unordered_map<std::string, std::shared_ptr<ImportJob>> s_importJobs;
        static std::atomic<uint64_t> s_importJobCounter{1};

        static void PruneImportJobsLocked(const std::string &protectedJobId)
        {
            constexpr size_t kMaxFinishedJobs = 64;
            constexpr auto kFinishedJobTtl = std::chrono::minutes(30);
            const auto now = std::chrono::steady_clock::now();

            for (auto it = s_importJobs.begin(); it != s_importJobs.end();)
            {
                const auto &job = it->second;
                if (it->first != protectedJobId && job && job->finished != std::chrono::steady_clock::time_point{} &&
                    now - job->finished > kFinishedJobTtl)
                {
                    it = s_importJobs.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            std::vector<std::pair<std::chrono::steady_clock::time_point, std::string>> finishedJobs;
            finishedJobs.reserve(s_importJobs.size());
            for (const auto &pair : s_importJobs)
            {
                const auto &job = pair.second;
                if (pair.first != protectedJobId && job && job->finished != std::chrono::steady_clock::time_point{})
                    finishedJobs.emplace_back(job->finished, pair.first);
            }
            if (finishedJobs.size() <= kMaxFinishedJobs)
                return;

            std::sort(finishedJobs.begin(), finishedJobs.end());
            const size_t toRemove = finishedJobs.size() - kMaxFinishedJobs;
            for (size_t i = 0; i < toRemove; ++i)
                s_importJobs.erase(finishedJobs[i].second);
        }

        static nlohmann::json ImportJobJson(const ImportJob &job)
        {
            nlohmann::json out;
            out["job_id"] = job.id;
            out["status"] = job.status;
            out["progress"] = job.progress;
            out["total"] = job.total;
            out["process_ok"] = job.processOk;
            if (!job.error.empty())
                out["error_message"] = job.error;

            auto pathsArray = [](const std::vector<std::filesystem::path> &paths)
            {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto &path : paths)
                    arr.push_back(PathUtf8(path));
                return arr;
            };
            out["cooked"] = pathsArray(job.cooked);
            out["skipped"] = pathsArray(job.skipped);
            out["failed"] = job.failed;

            nlohmann::json requests = nlohmann::json::array();
            for (const auto &pair : job.jobs)
                requests.push_back({{"source", PathUtf8(pair.first)}, {"output", PathUtf8(pair.second)}});
            out["requests"] = std::move(requests);
            return out;
        }
    } // namespace

    EditorToolRuntime::EditorToolRuntime(QueueActionFn queueAction, void *sdlWindow)
        : m_queueAction(std::move(queueAction)), m_sdlWindow(sdlWindow)
    {
    }

    void EditorToolRuntime::QueueAction(std::function<void()> fn) const
    {
        if (m_queueAction)
            m_queueAction(std::move(fn));
    }

    std::string EditorToolRuntime::ExecuteLua(const std::string &code) const
    {
        if (code.empty())
            return "{\"error\":\"missing code\"}";

        // Heap-allocate shared state so the lambda is safe to run even if wait_for times out
        // and this function has already returned (otherwise the lambda writes into freed stack).
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, code]()
                    {
            auto *ss = GetGlobalSystem<ScriptSystem>();
            if (!ss || !ss->IsInitialized())
                state->result = "error: ScriptSystem not available";
            else
                state->result = ss->ExecuteLua(code);
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for Lua execution\"}";
        }

        if (state->result.rfind("error:", 0) == 0)
            return JsonObj({{"error", JsonStr(state->result)}});
        return JsonObj({{"output", JsonStr(state->result)}});
    }

    // Encode a stable node ID: "node:<index>:<revision>"
    static std::string MakeNodeId(NodeId *node)
    {
        return "node:" + std::to_string(node->index) + ":" + std::to_string(node->revision);
    }

    static bool StrictParseInt(const std::string &s, size_t begin, size_t end, int &out)
    {
        if (begin >= end || end > s.size())
            return false;
        for (size_t i = begin; i < end; i++)
            if (!std::isdigit(static_cast<unsigned char>(s[i])) && !(i == begin && s[i] == '-'))
                return false;
        try
        {
            out = std::stoi(s.substr(begin, end - begin));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool StrictParseUint(const std::string &s, size_t begin, size_t end, uint32_t &out)
    {
        int v = 0;
        if (!StrictParseInt(s, begin, end, v) || v < 0)
            return false;
        out = static_cast<uint32_t>(v);
        return true;
    }

    static NodeId *ResolveNode(Scene &scene, const std::string &nodeId, std::string &outError)
    {
        // Stable ID: "node:<index>:<revision>" — strict parse, revision-checked
        if (nodeId.rfind("node:", 0) == 0)
        {
            size_t firstColon = 4;
            size_t secondColon = nodeId.find(':', firstColon + 1);
            if (secondColon == std::string::npos)
            {
                outError = "malformed node id (expected node:index:revision)";
                return nullptr;
            }
            int idx = 0;
            uint32_t rev = 0;
            if (!StrictParseInt(nodeId, firstColon + 1, secondColon, idx) ||
                !StrictParseUint(nodeId, secondColon + 1, nodeId.size(), rev))
            {
                outError = "malformed node id (non-numeric fields)";
                return nullptr;
            }
            if (idx < 0 || idx >= static_cast<int>(scene.GetNodeCount()))
            {
                outError = "stale node id (index out of range)";
                return nullptr;
            }
            NodeId *node = scene.GetNodeId(idx);
            if (node->revision != rev)
            {
                outError = "stale node id (revision mismatch)";
                return nullptr;
            }
            return node;
        }

        // Name-based lookup: fail if ambiguous
        NodeId *found = nullptr;
        int matchCount = 0;
        for (uint32_t i = 0; i < scene.GetNodeCount(); i++)
        {
            NodeId *n = scene.GetNodeId(i);
            if (scene.GetNodeName(n) == nodeId)
            {
                found = n;
                matchCount++;
                if (matchCount > 1)
                    break;
            }
        }
        if (matchCount == 0)
        {
            outError = "node not found: " + nodeId;
            return nullptr;
        }
        if (matchCount > 1)
        {
            outError = "ambiguous node name (found " + std::to_string(matchCount) + " matches): " + nodeId;
            return nullptr;
        }
        return found;
    }

    static std::string JsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    static bool ReadFloatArray(const nlohmann::json &value, float *out, int count)
    {
        if (!value.is_array() || static_cast<int>(value.size()) != count)
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (!value[i].is_number())
                return false;
            out[i] = value[i].get<float>();
        }
        return true;
    }

    static bool HasFloatArray(const nlohmann::json &args, const char *key, int count)
    {
        return args.contains(key) && args[key].is_array() && static_cast<int>(args[key].size()) == count;
    }

    static bool ApplySpriteJsonTransform(Scene &scene, NodeId *node, const nlohmann::json &args, int meshSlot, std::string &outError)
    {
        if (!node || !scene.IsNodeAlive(node))
        {
            outError = "node not found";
            return false;
        }

        const bool hasPos = HasFloatArray(args, "position", 3);
        const bool hasRot = HasFloatArray(args, "rotation", 3) || HasFloatArray(args, "rotation_euler_deg", 3);
        const bool hasScale = HasFloatArray(args, "scale", 3);
        const bool hasSize = HasFloatArray(args, "size", 2);
        if (!hasPos && !hasRot && !hasScale && !hasSize)
            return true;

        float pos[3]{}, rot[3]{}, scale[3]{}, size[2]{};
        if (hasPos && !ReadFloatArray(args["position"], pos, 3))
        {
            outError = "position must be [x,y,z]";
            return false;
        }
        if (hasRot)
        {
            const char *rotKey = HasFloatArray(args, "rotation", 3) ? "rotation" : "rotation_euler_deg";
            if (!ReadFloatArray(args[rotKey], rot, 3))
            {
                outError = "rotation must be [x,y,z] degrees";
                return false;
            }
        }
        if (hasScale && !ReadFloatArray(args["scale"], scale, 3))
        {
            outError = "scale must be [x,y,z]";
            return false;
        }
        if (hasSize && !ReadFloatArray(args["size"], size, 2))
        {
            outError = "size must be [width,height]";
            return false;
        }

        mat4 local = scene.GetLocalMatrix(node);
        vec3 curScale(length(vec3(local[0])), length(vec3(local[1])), length(vec3(local[2])));
        float sx = curScale.x > 1e-6f ? curScale.x : 1.0f;
        float sy = curScale.y > 1e-6f ? curScale.y : 1.0f;
        float sz = curScale.z > 1e-6f ? curScale.z : 1.0f;
        mat3 rotMat(vec3(local[0]) / sx, vec3(local[1]) / sy, vec3(local[2]) / sz);

        vec3 newPos = hasPos ? vec3(pos[0], pos[1], pos[2]) : vec3(local[3]);
        vec3 newRot = hasRot ? vec3(glm::radians(rot[0]), glm::radians(rot[1]), glm::radians(rot[2])) : glm::eulerAngles(quat_cast(rotMat));
        vec3 newScale = curScale;

        if (hasSize)
        {
            const auto &refs = scene.GetMeshRefs(node);
            if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()) || !scene.IsValidMeshIndex(refs[meshSlot]))
            {
                outError = "size requires a valid sprite mesh slot";
                return false;
            }

            const Mesh &mesh = scene.GetMesh(refs[meshSlot]);
            const vec3 extents = mesh.boundingBox.max - mesh.boundingBox.min;
            const float baseW = std::abs(extents.x) > 1e-6f ? std::abs(extents.x) : 1.0f;
            const float baseH = std::abs(extents.y) > 1e-6f ? std::abs(extents.y) : 1.0f;
            newScale.x = size[0] / baseW;
            newScale.y = size[1] / baseH;
        }
        if (hasScale)
            newScale = vec3(scale[0], scale[1], scale[2]);

        scene.SetLocalMatrix(node, glm::translate(mat4(1.f), newPos) * mat4_cast(quat(newRot)) * glm::scale(mat4(1.f), newScale));
        scene.MarkDirty();
        return true;
    }

    static void ReadSpriteOptions(const nlohmann::json &args, SpriteAuthoring::Options &options)
    {
        std::string path = args.value("path", args.value("asset_path", ""));
        if (path.empty())
            path = args.value("metadata_path", args.value("image_path", ""));
        options.assetPath = U8Path(path);
        options.name = args.value("name", "");
        options.frameIndex = args.value("frame", args.value("frame_index", -1));
        options.frameName = args.value("frame_name", "");

        float tint[4]{};
        if (args.contains("tint") && ReadFloatArray(args["tint"], tint, 4))
        {
            options.hasTint = true;
            options.tint = vec4(tint[0], tint[1], tint[2], tint[3]);
        }

        float uv[4]{};
        if (args.contains("uv_rect") && ReadFloatArray(args["uv_rect"], uv, 4))
        {
            options.hasUvRect = true;
            options.uvRect = vec4(uv[0], uv[1], uv[2], uv[3]);
        }
    }

    static nlohmann::json SpriteComponentJson(const NodeSpriteComponent &sprite);

    static nlohmann::json SpriteResultJson(Scene &scene, const SpriteAuthoring::Result &result)
    {
        nlohmann::json json;
        json["status"] = "ok";
        if (result.node)
        {
            json["node"] = MakeNodeId(result.node);
            json["name"] = scene.GetNodeName(result.node);
            json["index"] = result.node->index;
        }
        if (!result.imagePath.empty())
            json["image_path"] = PathUtf8(result.imagePath);
        if (!result.metadataPath.empty())
            json["metadata_path"] = PathUtf8(result.metadataPath);
        if (!result.frameName.empty())
            json["frame_name"] = result.frameName;
        json["frame_index"] = result.frameIndex;
        json["image_size"] = {{"width", result.imageWidth}, {"height", result.imageHeight}};
        json["frame_size"] = {{"width", result.frameWidth}, {"height", result.frameHeight}};
        json["quad_size"] = {result.quadWidth, result.quadHeight};
        json["uv_rect"] = {result.uvRect.x, result.uvRect.y, result.uvRect.z, result.uvRect.w};
        if (result.node)
        {
            if (const NodeSpriteComponent *sprite = scene.GetSpriteComponent(result.node))
                json["sprite"] = SpriteComponentJson(*sprite);
        }
        return json;
    }

    static nlohmann::json SpriteComponentJson(const NodeSpriteComponent &sprite)
    {
        nlohmann::json frames = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(sprite.frames.size()); ++i)
        {
            const NodeSpriteFrame &frame = sprite.frames[i];
            frames.push_back({
                {"index", i},
                {"name", frame.name},
                {"rect", {frame.x, frame.y, frame.w, frame.h}},
                {"pivot", {frame.pivotX, frame.pivotY}},
                {"duration", frame.duration},
                {"uv_rect", Vec4Json(frame.uvRect)},
            });
        }

        nlohmann::json clips = nlohmann::json::array();
        for (int i = 0; i < static_cast<int>(sprite.clips.size()); ++i)
        {
            const NodeSpriteClip &clip = sprite.clips[i];
            clips.push_back({
                {"index", i},
                {"name", clip.name},
                {"start", clip.start},
                {"end", clip.end},
                {"fps", clip.fps},
                {"loop", clip.loop},
            });
        }

        return {
            {"image_path", sprite.imagePath},
            {"metadata_path", sprite.metadataPath},
            {"frame_name", sprite.frameName},
            {"frame_index", sprite.frameIndex},
            {"image_size", {{"width", sprite.imageWidth}, {"height", sprite.imageHeight}}},
            {"frame_size", {{"width", sprite.frameWidth}, {"height", sprite.frameHeight}}},
            {"quad_size", {sprite.quadWidth, sprite.quadHeight}},
            {"uv_rect", Vec4Json(sprite.uvRect)},
            {"tint", Vec4Json(sprite.tint)},
            {"metadata_loaded", sprite.metadataLoaded},
            {"frame_count", sprite.frames.size()},
            {"clip_count", sprite.clips.size()},
            {"frames", std::move(frames)},
            {"clips", std::move(clips)},
            {"playback", {
                             {"playing", sprite.playing},
                             {"active_clip", sprite.activeClipName},
                             {"active_clip_index", sprite.activeClipIndex},
                             {"mesh_slot", sprite.meshSlot},
                             {"loop", sprite.loop},
                             {"speed", sprite.playbackSpeed},
                             {"accumulator", sprite.playbackAccumulator},
                         }},
        };
    }

    static std::filesystem::path ResolveSpriteMetadataPath(const std::string &path)
    {
        std::filesystem::path resolved = ResolveAssetPath(path);
        const std::string filename = ToLower(resolved.filename().string());
        if (filename.ends_with(".sprite.json"))
            return resolved;

        if (ToLower(resolved.extension().string()) == ".json")
        {
            std::filesystem::path renamed = resolved;
            renamed.replace_filename(resolved.stem().string() + ".sprite.json");
            return renamed;
        }

        resolved += ".sprite.json";
        return resolved;
    }

    static std::string AssetRelativePathForTool(const std::filesystem::path &path);

    static std::filesystem::path ResolveSpriteSheetPath(const std::string &path)
    {
        std::filesystem::path resolved = ResolveAssetPath(path);
        const std::string filename = ToLower(resolved.filename().string());
        if (filename.ends_with(".sheet.json"))
            return resolved;

        if (ToLower(resolved.extension().string()) == ".json")
        {
            std::filesystem::path renamed = resolved;
            renamed.replace_filename(resolved.stem().string() + ".sheet.json");
            return renamed;
        }

        resolved += ".sheet.json";
        return resolved;
    }

    static bool ReadSpriteSheetImageEntry(const nlohmann::json &entry, nlohmann::json &outEntry, std::string &outError)
    {
        std::string imagePath;
        std::string label;
        const bool objectEntry = entry.is_object();
        if (entry.is_string())
        {
            imagePath = entry.get<std::string>();
        }
        else if (objectEntry && entry.contains("path") && entry["path"].is_string())
        {
            imagePath = entry["path"].get<std::string>();
            label = entry.value("label", "");
        }
        else
        {
            outError = "sheet image entries must be strings or objects with path";
            return false;
        }

        if (imagePath.empty())
        {
            outError = "sheet image path cannot be empty";
            return false;
        }

        const std::filesystem::path resolved = ResolveTexturePathForTool(imagePath);
        if (!pmcp::IsPathSafe(resolved.string(), Path::Assets))
        {
            outError = "sheet image paths must stay inside Assets/";
            return false;
        }

        const std::string assetPath = AssetRelativePathForTool(resolved);
        if (objectEntry || !label.empty())
        {
            outEntry = nlohmann::json::object();
            outEntry["path"] = assetPath;
            if (!label.empty())
                outEntry["label"] = label;
        }
        else
        {
            outEntry = assetPath;
        }
        return true;
    }

    static std::string AssetRelativePathForTool(const std::filesystem::path &path)
    {
        if (path.empty())
            return {};

        const std::filesystem::path assets = U8Path(Path::Assets);
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(path, assets, ec);
        if (!ec && !rel.empty() && rel.begin() != rel.end() && *rel.begin() != "..")
            return PathUtf8(rel);
        return PathUtf8(path);
    }

    static nlohmann::json GenerateSpriteFramesJson(const nlohmann::json &grid, int imageW, int imageH)
    {
        nlohmann::json frames = nlohmann::json::array();
        if (!grid.is_object() || imageW <= 0 || imageH <= 0)
            return frames;

        const int frameW = std::max(1, grid.value("frame_width", 32));
        const int frameH = std::max(1, grid.value("frame_height", 32));
        const int marginX = std::max(0, grid.value("margin_x", 0));
        const int marginY = std::max(0, grid.value("margin_y", 0));
        const int spacingX = std::max(0, grid.value("spacing_x", 0));
        const int spacingY = std::max(0, grid.value("spacing_y", 0));
        const int maxFrames = std::max(0, grid.value("max_frames", 0));
        const float duration = grid.value("duration", 0.1f);

        int count = 0;
        for (int y = marginY; y + frameH <= imageH; y += frameH + spacingY)
        {
            for (int x = marginX; x + frameW <= imageW; x += frameW + spacingX)
            {
                if (maxFrames > 0 && count >= maxFrames)
                    return frames;
                char name[32];
                std::snprintf(name, sizeof(name), "frame_%03d", count);
                frames.push_back({
                    {"name", name},
                    {"rect", {x, y, frameW, frameH}},
                    {"pivot", {0.5f, 0.5f}},
                    {"duration", duration},
                });
                ++count;
            }
        }
        return frames;
    }

    static bool ReadIntArray(const nlohmann::json &value, int *out, int count)
    {
        if (!value.is_array() || static_cast<int>(value.size()) < count)
            return false;
        for (int i = 0; i < count; ++i)
        {
            if (!value[i].is_number_integer())
                return false;
            out[i] = value[i].get<int>();
        }
        return true;
    }

    static bool LoadSpriteMetadataForEdit(const std::string &pathArg, std::filesystem::path &path, nlohmann::json &root, std::string &error)
    {
        if (pathArg.empty())
        {
            error = "path required";
            return false;
        }

        path = ResolveSpriteMetadataPath(pathArg);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
        {
            error = "path must stay inside Assets/";
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "sprite metadata not found: " + pathArg;
            return false;
        }

        root = nlohmann::json::parse(in, nullptr, false);
        if (!root.is_object())
        {
            error = "invalid sprite metadata json";
            return false;
        }

        if (!root.contains("frames") || !root["frames"].is_array())
            root["frames"] = nlohmann::json::array();
        if (!root.contains("clips") || !root["clips"].is_array())
            root["clips"] = nlohmann::json::array();
        if (!root.contains("schema") || !root["schema"].is_string())
            root["schema"] = "phasma.sprite_editor.v1";
        return true;
    }

    static bool SaveJsonAsset(const std::filesystem::path &path, const nlohmann::json &root, std::string &error)
    {
        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            error = "failed to create asset directory: " + ec.message();
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            error = "failed to open asset for writing: " + PathUtf8(path);
            return false;
        }
        out << root.dump(2) << "\n";
        if (!out)
        {
            error = "failed to write asset: " + PathUtf8(path);
            return false;
        }
        return true;
    }

    static nlohmann::json SpriteMetadataEditResult(const std::filesystem::path &path, const nlohmann::json &root)
    {
        return {
            {"status", "ok"},
            {"path", PathUtf8(path)},
            {"asset_path", AssetRelativePathForTool(path)},
            {"frame_count", root.value("frames", nlohmann::json::array()).size()},
            {"clip_count", root.value("clips", nlohmann::json::array()).size()},
            {"metadata", root},
        };
    }

    static int FindNamedOrIndexedItem(const nlohmann::json &items,
                                      const nlohmann::json &args,
                                      std::initializer_list<const char *> indexKeys,
                                      std::initializer_list<const char *> nameKeys,
                                      std::string &error)
    {
        int index = -1;
        for (const char *key : indexKeys)
        {
            if (args.contains(key) && args[key].is_number_integer())
            {
                index = args[key].get<int>();
                break;
            }
        }

        if (index >= 0)
        {
            if (index >= static_cast<int>(items.size()))
            {
                error = "index out of range";
                return -1;
            }
            return index;
        }

        std::string name;
        for (const char *key : nameKeys)
        {
            if (args.contains(key) && args[key].is_string())
            {
                name = args[key].get<std::string>();
                break;
            }
        }

        if (name.empty())
        {
            error = "index or name required";
            return -1;
        }

        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            if (items[i].is_object() && items[i].value("name", "") == name)
                return i;
        }

        error = "name not found: " + name;
        return -1;
    }

    static bool ApplySpriteFrameFields(nlohmann::json &frame, const nlohmann::json &source, bool requireRect, std::string &error)
    {
        const nlohmann::json *src = &source;
        if (source.contains("frame") && source["frame"].is_object())
            src = &source["frame"];

        if (src->contains("name"))
        {
            if (!(*src)["name"].is_string())
            {
                error = "frame name must be a string";
                return false;
            }
            frame["name"] = (*src)["name"].get<std::string>();
        }

        int rect[4]{};
        const bool hasRect = src->contains("rect") && ReadIntArray((*src)["rect"], rect, 4);
        const bool hasLooseRect = src->contains("x") || src->contains("y") || src->contains("w") || src->contains("h") ||
                                  src->contains("width") || src->contains("height");
        if (hasRect)
        {
            frame["rect"] = {std::max(0, rect[0]), std::max(0, rect[1]), std::max(1, rect[2]), std::max(1, rect[3])};
        }
        else if (hasLooseRect)
        {
            auto existingRectValue = [&](int index, int fallback) -> int
            {
                return frame.contains("rect") && frame["rect"].is_array() &&
                               frame["rect"].size() > static_cast<size_t>(index) && frame["rect"][index].is_number_integer()
                           ? frame["rect"][index].get<int>()
                           : fallback;
            };
            int x = existingRectValue(0, 0);
            int y = existingRectValue(1, 0);
            int w = existingRectValue(2, 1);
            int h = existingRectValue(3, 1);
            x = std::max(0, src->value("x", x));
            y = std::max(0, src->value("y", y));
            w = std::max(1, src->value("w", src->value("width", w)));
            h = std::max(1, src->value("h", src->value("height", h)));
            frame["rect"] = {x, y, w, h};
        }
        else if (requireRect)
        {
            error = "frame rect required";
            return false;
        }

        float pivot[2]{};
        if (src->contains("pivot"))
        {
            if (!ReadFloatArray((*src)["pivot"], pivot, 2))
            {
                error = "pivot must be [x,y]";
                return false;
            }
            frame["pivot"] = {std::clamp(pivot[0], 0.0f, 1.0f), std::clamp(pivot[1], 0.0f, 1.0f)};
        }

        if (src->contains("duration"))
        {
            if (!(*src)["duration"].is_number())
            {
                error = "duration must be a number";
                return false;
            }
            frame["duration"] = std::max(0.01f, (*src)["duration"].get<float>());
        }

        if (src->contains("hitbox"))
        {
            if (!(*src)["hitbox"].is_object())
            {
                error = "hitbox must be an object";
                return false;
            }
            frame["hitbox"] = (*src)["hitbox"];
        }
        else if (src->contains("hitbox_rect"))
        {
            int hitbox[4]{};
            if (!ReadIntArray((*src)["hitbox_rect"], hitbox, 4))
            {
                error = "hitbox_rect must be [x,y,w,h]";
                return false;
            }
            frame["hitbox"] = {
                {"enabled", true},
                {"rect", {hitbox[0], hitbox[1], std::max(1, hitbox[2]), std::max(1, hitbox[3])}},
            };
        }

        if (!frame.contains("name") || !frame["name"].is_string() || frame["name"].get<std::string>().empty())
            frame["name"] = "frame";
        if (!frame.contains("pivot"))
            frame["pivot"] = {0.5f, 0.5f};
        if (!frame.contains("duration"))
            frame["duration"] = 0.1f;
        return true;
    }

    static bool ApplySpriteClipFields(nlohmann::json &clip, const nlohmann::json &source, bool requireRange, std::string &error)
    {
        const nlohmann::json *src = &source;
        if (source.contains("clip") && source["clip"].is_object())
            src = &source["clip"];

        if (src->contains("name"))
        {
            if (!(*src)["name"].is_string())
            {
                error = "clip name must be a string";
                return false;
            }
            clip["name"] = (*src)["name"].get<std::string>();
        }
        if (src->contains("start"))
        {
            if (!(*src)["start"].is_number_integer())
            {
                error = "clip start must be an integer";
                return false;
            }
            clip["start"] = std::max(0, (*src)["start"].get<int>());
        }
        if (src->contains("end"))
        {
            if (!(*src)["end"].is_number_integer())
            {
                error = "clip end must be an integer";
                return false;
            }
            clip["end"] = std::max(0, (*src)["end"].get<int>());
        }
        if (requireRange && (!clip.contains("start") || !clip.contains("end")))
        {
            error = "clip start and end required";
            return false;
        }
        if (clip.contains("start") && clip.contains("end") && clip["start"].get<int>() > clip["end"].get<int>())
            std::swap(clip["start"], clip["end"]);

        if (src->contains("fps"))
        {
            if (!(*src)["fps"].is_number())
            {
                error = "clip fps must be a number";
                return false;
            }
            clip["fps"] = std::max(0.1f, (*src)["fps"].get<float>());
        }
        if (src->contains("loop"))
        {
            if (!(*src)["loop"].is_boolean())
            {
                error = "clip loop must be a boolean";
                return false;
            }
            clip["loop"] = (*src)["loop"].get<bool>();
        }

        if (!clip.contains("name") || !clip["name"].is_string() || clip["name"].get<std::string>().empty())
            clip["name"] = "clip";
        if (!clip.contains("fps"))
            clip["fps"] = 10.0f;
        if (!clip.contains("loop"))
            clip["loop"] = true;
        return true;
    }

    static void ClampSpriteClips(nlohmann::json &root)
    {
        if (!root.contains("clips") || !root["clips"].is_array())
            return;

        const int maxFrame = root.contains("frames") && root["frames"].is_array()
                                 ? std::max(0, static_cast<int>(root["frames"].size()) - 1)
                                 : 0;
        for (auto &clip : root["clips"])
        {
            if (!clip.is_object())
                continue;
            int start = clip.contains("start") && clip["start"].is_number_integer() ? clip["start"].get<int>() : 0;
            int end = clip.contains("end") && clip["end"].is_number_integer() ? clip["end"].get<int>() : start;
            start = std::clamp(start, 0, maxFrame);
            end = std::clamp(end, 0, maxFrame);
            if (start > end)
                std::swap(start, end);
            clip["start"] = start;
            clip["end"] = end;
            const float fps = clip.contains("fps") && clip["fps"].is_number() ? clip["fps"].get<float>() : 10.0f;
            clip["fps"] = std::max(0.1f, fps);
            if (!clip.contains("loop") || !clip["loop"].is_boolean())
                clip["loop"] = true;
        }
    }

    static std::filesystem::path ResolveSpriteMetadataImagePath(const std::filesystem::path &metadataPath, const std::string &imagePath)
    {
        if (imagePath.empty())
            return {};

        std::filesystem::path p = U8Path(imagePath);
        if (p.is_absolute())
            return p.lexically_normal();

        const std::filesystem::path metadataRelative = (metadataPath.parent_path() / p).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(metadataRelative, ec))
            return metadataRelative;

        return ResolveTexturePathForTool(imagePath).lexically_normal();
    }

    static bool ReadSpriteFrameRectRaw(const nlohmann::json &frame, int rect[4])
    {
        return frame.is_object() && frame.contains("rect") && ReadIntArray(frame["rect"], rect, 4);
    }

    static std::string SpriteFrameName(const nlohmann::json &frame, int index)
    {
        if (frame.is_object() && frame.contains("name") && frame["name"].is_string() && !frame["name"].get<std::string>().empty())
            return frame["name"].get<std::string>();
        return "frame_" + std::to_string(index);
    }

    static nlohmann::json SpriteFrameTimelineEntry(const nlohmann::json &frames, int index, float seconds)
    {
        nlohmann::json item;
        item["index"] = index;
        item["name"] = SpriteFrameName(frames[index], index);
        item["seconds"] = seconds;
        int rect[4]{};
        if (ReadSpriteFrameRectRaw(frames[index], rect))
            item["rect"] = {rect[0], rect[1], rect[2], rect[3]};
        return item;
    }

    static nlohmann::json ValidateSpriteMetadataAsset(const std::string &pathArg)
    {
        nlohmann::json result;
        result["status"] = "ok";
        result["valid"] = false;
        result["errors"] = nlohmann::json::array();
        result["warnings"] = nlohmann::json::array();
        result["frames"] = nlohmann::json::array();
        result["clips"] = nlohmann::json::array();
        result["clip_timelines"] = nlohmann::json::array();

        auto addError = [&](const std::string &message)
        {
            result["errors"].push_back(message);
        };
        auto addWarning = [&](const std::string &message)
        {
            result["warnings"].push_back(message);
        };

        if (pathArg.empty())
        {
            addError("path required");
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            return result;
        }

        const std::filesystem::path path = ResolveSpriteMetadataPath(pathArg);
        result["path"] = PathUtf8(path);
        result["asset_path"] = AssetRelativePathForTool(path);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
        {
            addError("path must stay inside Assets/");
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            return result;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            addError("sprite metadata not found: " + pathArg);
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            return result;
        }

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (!root.is_object())
        {
            addError("invalid sprite metadata json");
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            return result;
        }

        result["schema"] = root.contains("schema") && root["schema"].is_string() ? root["schema"].get<std::string>() : "";
        std::string imageValue;
        if (root.contains("image"))
        {
            if (root["image"].is_string())
                imageValue = root["image"].get<std::string>();
            else
                addError("metadata image path must be a string");
        }
        result["image"] = imageValue;

        int actualW = 0;
        int actualH = 0;
        if (imageValue.empty())
        {
            addError("metadata image path is empty");
        }
        else
        {
            const std::filesystem::path imagePath = ResolveSpriteMetadataImagePath(path, imageValue);
            result["resolved_image"] = PathUtf8(imagePath);
            if (!pmcp::IsPathSafe(imagePath.string(), Path::Assets))
                addWarning("resolved image is outside Assets/");

            std::error_code ec;
            if (!std::filesystem::exists(imagePath, ec))
            {
                addError("image not found: " + imageValue);
            }
            else
            {
                int channels = 0;
                if (!stbi_info(PathUtf8(imagePath).c_str(), &actualW, &actualH, &channels))
                {
                    addError("failed to read image dimensions: " + imageValue);
                }
                else
                {
                    result["actual_image_size"] = {{"width", actualW}, {"height", actualH}, {"channels", channels}};
                }
            }
        }

        int metaW = 0;
        int metaH = 0;
        if (root.contains("image_size") && root["image_size"].is_object())
        {
            const auto &imageSize = root["image_size"];
            if (imageSize.contains("width") && imageSize["width"].is_number_integer())
                metaW = imageSize["width"].get<int>();
            else
                addError("metadata image_size.width must be an integer");
            if (imageSize.contains("height") && imageSize["height"].is_number_integer())
                metaH = imageSize["height"].get<int>();
            else
                addError("metadata image_size.height must be an integer");
            result["metadata_image_size"] = {{"width", metaW}, {"height", metaH}};
            if (actualW > 0 && actualH > 0 && (metaW != actualW || metaH != actualH))
                addError("metadata image_size does not match actual image dimensions");
        }
        else
        {
            addWarning("metadata image_size is missing");
        }

        const nlohmann::json frames = root.contains("frames") && root["frames"].is_array() ? root["frames"] : nlohmann::json::array();
        if (!root.contains("frames") || !root["frames"].is_array())
            addError("frames array is missing");
        if (frames.empty())
            addError("frames array is empty");

        std::unordered_map<std::string, int> frameNameCounts;
        for (int i = 0; i < static_cast<int>(frames.size()); ++i)
        {
            nlohmann::json frameInfo;
            frameInfo["index"] = i;
            frameInfo["valid"] = true;
            frameInfo["name"] = SpriteFrameName(frames[i], i);
            frameNameCounts[frameInfo["name"].get<std::string>()]++;

            int rect[4]{};
            if (!ReadSpriteFrameRectRaw(frames[i], rect))
            {
                frameInfo["valid"] = false;
                addError("frame " + std::to_string(i) + " has no valid rect [x,y,w,h]");
            }
            else
            {
                frameInfo["rect"] = {rect[0], rect[1], rect[2], rect[3]};
                if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0)
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " rect must have non-negative origin and positive size");
                }
                if (actualW > 0 && actualH > 0 && (rect[0] + rect[2] > actualW || rect[1] + rect[3] > actualH))
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " rect is outside image bounds");
                }
            }

            if (frames[i].is_object() && frames[i].contains("pivot"))
            {
                const auto &pivot = frames[i]["pivot"];
                if (!pivot.is_array() || pivot.size() < 2 || !pivot[0].is_number() || !pivot[1].is_number())
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " pivot must be [x,y]");
                }
            }
            if (frames[i].is_object() && frames[i].contains("duration") &&
                (!frames[i]["duration"].is_number() || frames[i]["duration"].get<float>() <= 0.0f))
            {
                frameInfo["valid"] = false;
                addError("frame " + std::to_string(i) + " duration must be positive");
            }

            if (frames[i].is_object() && frames[i].contains("hitbox"))
            {
                const auto &hitbox = frames[i]["hitbox"];
                if (!hitbox.is_object())
                {
                    frameInfo["valid"] = false;
                    addError("frame " + std::to_string(i) + " hitbox must be an object");
                }
                else if (hitbox.contains("rect"))
                {
                    int hitboxRect[4]{};
                    if (!ReadIntArray(hitbox["rect"], hitboxRect, 4) || hitboxRect[2] <= 0 || hitboxRect[3] <= 0)
                    {
                        frameInfo["valid"] = false;
                        addError("frame " + std::to_string(i) + " hitbox rect must be [x,y,w,h] with positive size");
                    }
                }
            }

            result["frames"].push_back(std::move(frameInfo));
        }
        for (const auto &[name, count] : frameNameCounts)
        {
            if (count > 1)
                addWarning("duplicate frame name: " + name);
        }

        const nlohmann::json clips = root.contains("clips") && root["clips"].is_array() ? root["clips"] : nlohmann::json::array();
        if (!root.contains("clips") || !root["clips"].is_array())
            addWarning("clips array is missing");
        if (clips.empty())
            addWarning("clips array is empty");

        const int frameCount = static_cast<int>(frames.size());
        for (int i = 0; i < static_cast<int>(clips.size()); ++i)
        {
            nlohmann::json clipInfo;
            clipInfo["index"] = i;
            clipInfo["valid"] = true;

            if (!clips[i].is_object())
            {
                clipInfo["valid"] = false;
                addError("clip " + std::to_string(i) + " must be an object");
                result["clips"].push_back(std::move(clipInfo));
                continue;
            }

            const auto &clip = clips[i];
            const std::string clipName = clip.contains("name") && clip["name"].is_string() && !clip["name"].get<std::string>().empty()
                                             ? clip["name"].get<std::string>()
                                             : "clip_" + std::to_string(i);
            clipInfo["name"] = clipName;
            const bool hasStart = clip.contains("start") && clip["start"].is_number_integer();
            const bool hasEnd = clip.contains("end") && clip["end"].is_number_integer();
            int start = hasStart ? clip["start"].get<int>() : 0;
            int end = hasEnd ? clip["end"].get<int>() : start;
            if (!hasStart || !hasEnd)
            {
                clipInfo["valid"] = false;
                addError("clip " + std::to_string(i) + " requires integer start and end");
            }
            if (start > end)
            {
                clipInfo["valid"] = false;
                addError("clip " + clipName + " start is greater than end");
            }
            if (frameCount == 0 || start < 0 || end < 0 || start >= frameCount || end >= frameCount)
            {
                clipInfo["valid"] = false;
                addError("clip " + clipName + " frame range is outside frame bounds");
            }

            float fps = 10.0f;
            if (clip.contains("fps") && clip["fps"].is_number())
                fps = clip["fps"].get<float>();
            if (fps <= 0.0f)
            {
                clipInfo["valid"] = false;
                addError("clip " + clipName + " fps must be positive");
                fps = 10.0f;
            }
            else if (clip.contains("fps") && !clip["fps"].is_number())
            {
                clipInfo["valid"] = false;
                addError("clip " + clipName + " fps must be a number");
            }
            bool loop = true;
            if (clip.contains("loop") && !clip["loop"].is_boolean())
            {
                clipInfo["valid"] = false;
                addError("clip " + clipName + " loop must be boolean");
            }
            else if (clip.contains("loop"))
            {
                loop = clip["loop"].get<bool>();
            }

            clipInfo["start"] = start;
            clipInfo["end"] = end;
            clipInfo["fps"] = fps;
            clipInfo["loop"] = loop;
            result["clips"].push_back(clipInfo);

            if (frameCount > 0)
            {
                const int clampedStart = std::clamp(start, 0, frameCount - 1);
                const int clampedEnd = std::clamp(end, 0, frameCount - 1);
                if (clampedStart <= clampedEnd)
                {
                    nlohmann::json timeline;
                    timeline["clip_index"] = i;
                    timeline["name"] = clipName;
                    timeline["start"] = clampedStart;
                    timeline["end"] = clampedEnd;
                    timeline["fps"] = fps;
                    timeline["loop"] = loop;
                    timeline["frame_count"] = clampedEnd - clampedStart + 1;
                    timeline["total_seconds"] = static_cast<float>(clampedEnd - clampedStart + 1) / std::max(0.1f, fps);
                    timeline["frames"] = nlohmann::json::array();
                    for (int frameIndex = clampedStart; frameIndex <= clampedEnd; ++frameIndex)
                        timeline["frames"].push_back(SpriteFrameTimelineEntry(frames, frameIndex, 1.0f / std::max(0.1f, fps)));
                    result["clip_timelines"].push_back(std::move(timeline));
                }
            }
        }

        result["frame_count"] = frames.size();
        result["clip_count"] = clips.size();
        result["valid"] = result["errors"].empty();
        result["error_count"] = result["errors"].size();
        result["warning_count"] = result["warnings"].size();
        return result;
    }

    static std::filesystem::path ResolveSpriteSheetImagePath(const std::filesystem::path &sheetPath, const std::string &imagePath)
    {
        std::filesystem::path p = U8Path(imagePath);
        if (p.is_absolute())
            return p.lexically_normal();

        const std::filesystem::path sheetRelative = (sheetPath.parent_path() / p).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(sheetRelative, ec))
            return sheetRelative;

        return ResolveTexturePathForTool(imagePath).lexically_normal();
    }

    static bool ReadSpriteSheetImageAt(const nlohmann::json &sheet,
                                       const std::filesystem::path &sheetPath,
                                       int index,
                                       std::filesystem::path &outImage,
                                       std::string &outLabel,
                                       std::string &error)
    {
        if (!sheet.contains("images") || !sheet["images"].is_array() || sheet["images"].empty())
        {
            error = "sheet asset has no images";
            return false;
        }

        if (index < 0 || index >= static_cast<int>(sheet["images"].size()))
        {
            error = "image_index out of range";
            return false;
        }

        const nlohmann::json &entry = sheet["images"][index];
        std::string imagePath;
        if (entry.is_string())
        {
            imagePath = entry.get<std::string>();
        }
        else if (entry.is_object() && entry.contains("path") && entry["path"].is_string())
        {
            imagePath = entry["path"].get<std::string>();
            outLabel = entry.value("label", "");
        }
        else
        {
            error = "sheet image entry must be a string or object with path";
            return false;
        }

        outImage = ResolveSpriteSheetImagePath(sheetPath, imagePath);
        if (!pmcp::IsPathSafe(outImage.string(), Path::Assets))
        {
            error = "sheet image must stay inside Assets/";
            return false;
        }
        return true;
    }

    std::string EditorToolRuntime::CreateNode(const std::string &name, const std::string &parent) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, name, parent]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = r->GetScene();
                NodeId *parentNode = nullptr;
                if (!parent.empty())
                {
                    std::string err;
                    parentNode = ResolveNode(sc, parent, err);
                    if (!parentNode)
                    {
                        state->result = JsonObj({{"error", JsonStr(err)}});
                        std::lock_guard lock(state->mtx);
                        state->done = true;
                        state->cv.notify_one();
                        return;
                    }
                }
                NodeId *node = sc.CreateNode(name, parentNode);
                sc.MarkDirty();
                state->result = JsonObj({
                    {"name", JsonStr(sc.GetNodeName(node))},
                    {"index", std::to_string(node->index)},
                    {"id", JsonStr(MakeNodeId(node))}
                });
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::CreateSpriteNode(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                NodeId *parentNode = nullptr;
                const std::string parent = args.value("parent", "");
                if (!parent.empty())
                {
                    std::string err;
                    parentNode = ResolveNode(sc, parent, err);
                    if (!parentNode)
                    {
                        state->result = JsonObj({{"error", JsonStr(err)}});
                        std::lock_guard lock(state->mtx);
                        state->done = true;
                        state->cv.notify_one();
                        return;
                    }
                }

                SpriteAuthoring::Options options;
                ReadSpriteOptions(args, options);
                SpriteAuthoring::Result sprite = SpriteAuthoring::CreateNode(sc, options, parentNode);
                if (!sprite.error.empty())
                {
                    state->result = JsonObj({{"error", JsonStr(sprite.error)}});
                }
                    else
                    {
                        std::string metadataError;
                        sc.LoadSpriteMetadata(sprite.node, &metadataError);
                        std::string transformError;
                        const int meshSlot = std::max(0, args.value("mesh_slot", 0));
                        if (!ApplySpriteJsonTransform(sc, sprite.node, args, meshSlot, transformError))
                        state->result = JsonObj({{"error", JsonStr(transformError)}});
                    else
                        state->result = SpriteResultJson(sc, sprite).dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::GetSpriteNode(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else
                {
                    const NodeSpriteComponent *sprite = sc.GetSpriteComponent(node);
                    const mat4 &local = sc.GetLocalMatrix(node);
                    const vec3 localScale(length(vec3(local[0])), length(vec3(local[1])), length(vec3(local[2])));
                    const float sx = localScale.x > 1e-6f ? localScale.x : 1.0f;
                    const float sy = localScale.y > 1e-6f ? localScale.y : 1.0f;
                    const float sz = localScale.z > 1e-6f ? localScale.z : 1.0f;
                    const mat3 rotMat(vec3(local[0]) / sx, vec3(local[1]) / sy, vec3(local[2]) / sz);

                    nlohmann::json result;
                    result["status"] = "ok";
                    result["node"] = MakeNodeId(node);
                    result["name"] = sc.GetNodeName(node);
                    result["index"] = node->index;
                    result["has_sprite"] = sprite != nullptr;
                    result["component_flags"] = sc.GetComponentFlags(node);
                    result["transform"] = {
                        {"position", Vec3Json(vec3(local[3]))},
                        {"rotation_euler_deg", Vec3Json(glm::degrees(glm::eulerAngles(quat_cast(rotMat))))},
                        {"scale", Vec3Json(localScale)},
                    };

                    if (sprite)
                        result["sprite"] = SpriteComponentJson(*sprite);

                    state->result = result.dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetSpriteNode(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else
                {
                    SpriteAuthoring::Options options;
                    ReadSpriteOptions(args, options);

                    const bool frameRequested = args.contains("frame") || args.contains("frame_index") || args.contains("frame_name");
                    if (frameRequested && options.assetPath.empty() && !options.hasUvRect)
                    {
                        if (const NodeSpriteComponent *component = sc.GetSpriteComponent(node))
                            options.assetPath = U8Path(component->metadataPath);
                    }
                    if (frameRequested && options.assetPath.empty() && !options.hasUvRect)
                    {
                        state->result = R"({"error":"path or uv_rect required to resolve a sprite frame"})";
                    }
                    else
                    {
                        const int meshSlot = std::max(0, args.value("mesh_slot", 0));
                        SpriteAuthoring::Result sprite = SpriteAuthoring::ApplyToNode(sc, node, options, meshSlot);
                        if (!sprite.error.empty())
                        {
                            state->result = JsonObj({{"error", JsonStr(sprite.error)}});
                        }
                        else
                        {
                            std::string metadataError;
                            sc.LoadSpriteMetadata(node, &metadataError);
                            std::string transformError;
                            if (!ApplySpriteJsonTransform(sc, node, args, meshSlot, transformError))
                                state->result = JsonObj({{"error", JsonStr(transformError)}});
                            else
                                state->result = SpriteResultJson(sc, sprite).dump();
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetSpriteFrame(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else
                {
                    const NodeSpriteComponent *component = sc.GetSpriteComponent(node);
                    std::string path = args.value("path", args.value("metadata_path", ""));
                    if (path.empty() && component)
                        path = component->metadataPath;
                    if (path.empty())
                    {
                        state->result = R"({"error":"sprite metadata path required"})";
                    }
                    else
                    {
                        SpriteAuthoring::Options options;
                        ReadSpriteOptions(args, options);
                        options.assetPath = U8Path(path);
                        if (options.frameIndex < 0 && options.frameName.empty())
                            options.frameIndex = 0;

                        const int meshSlot = std::max(0, args.value("mesh_slot", 0));
                        SpriteAuthoring::Result sprite = SpriteAuthoring::ApplyToNode(sc, node, options, meshSlot);
                        if (!sprite.error.empty())
                        {
                            state->result = JsonObj({{"error", JsonStr(sprite.error)}});
                        }
                        else
                        {
                            std::string metadataError;
                            sc.LoadSpriteMetadata(node, &metadataError);
                            state->result = SpriteResultJson(sc, sprite).dump();
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::ReloadSpriteMetadata(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = nlohmann::json{{"error", err}}.dump();
                }
                else if (!sc.LoadSpriteMetadata(node, &err))
                {
                    state->result = nlohmann::json{{"error", err}}.dump();
                }
                else
                {
                    const NodeSpriteComponent *sprite = sc.GetSpriteComponent(node);
                    state->result = nlohmann::json{{"status", "ok"}, {"node", MakeNodeId(node)}, {"sprite", sprite ? SpriteComponentJson(*sprite) : nlohmann::json(nullptr)}}.dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::PlaySpriteClip(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = nlohmann::json{{"error", err}}.dump();
                }
                else
                {
                    const std::string clipName = args.value("clip_name", args.value("clip", ""));
                    const bool restart = args.value("restart", true);
                    const int meshSlot = args.value("mesh_slot", -1);
                    if (!sc.PlaySpriteClip(node, clipName, restart, meshSlot, &err))
                    {
                        state->result = nlohmann::json{{"error", err}}.dump();
                    }
                    else
                    {
                        if (NodeSpriteComponent *sprite = sc.GetSpriteComponent(node))
                        {
                            if (args.contains("speed") && args["speed"].is_number())
                                sprite->playbackSpeed = std::max(0.0f, args["speed"].get<float>());
                            if (args.contains("loop") && args["loop"].is_boolean())
                                sprite->loop = args["loop"].get<bool>();
                            state->result = nlohmann::json{{"status", "ok"}, {"node", MakeNodeId(node)}, {"sprite", SpriteComponentJson(*sprite)}}.dump();
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetSpritePlayback(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = nlohmann::json{{"error", err}}.dump();
                }
                else
                {
                    NodeSpriteComponent *sprite = sc.GetSpriteComponent(node);
                    if (!sprite)
                    {
                        state->result = R"({"error":"node has no sprite component"})";
                    }
                    else
                    {
                        if (args.value("reload_metadata", false) && !sc.LoadSpriteMetadata(node, &err))
                        {
                            state->result = nlohmann::json{{"error", err}}.dump();
                            std::lock_guard lock(state->mtx);
                            state->done = true;
                            state->cv.notify_one();
                            return;
                        }

                        const int meshSlot = args.value("mesh_slot", -1);
                        bool persistentChanged = false;
                        if (args.contains("frame_name") && args["frame_name"].is_string())
                        {
                            if (!sc.SetSpriteFrame(node, args["frame_name"].get<std::string>(), meshSlot, &err))
                            {
                                state->result = nlohmann::json{{"error", err}}.dump();
                                std::lock_guard lock(state->mtx);
                                state->done = true;
                                state->cv.notify_one();
                                return;
                            }
                            persistentChanged = true;
                        }
                        else if (args.contains("frame") || args.contains("frame_index"))
                        {
                            const int frameIndex = args.value("frame", args.value("frame_index", 0));
                            if (!sc.SetSpriteFrame(node, frameIndex, meshSlot, &err))
                            {
                                state->result = nlohmann::json{{"error", err}}.dump();
                                std::lock_guard lock(state->mtx);
                                state->done = true;
                                state->cv.notify_one();
                                return;
                            }
                            persistentChanged = true;
                        }

                        const bool wantsClip = args.contains("clip") || args.contains("clip_name");
                        const bool wantsPlaying = args.contains("playing") && args["playing"].is_boolean();
                        if (wantsClip || (wantsPlaying && args.value("playing", false)))
                        {
                            const std::string clipName = args.value("clip_name", args.value("clip", ""));
                            const bool restart = args.value("restart", wantsClip);
                            if (!sc.PlaySpriteClip(node, clipName, restart, meshSlot, &err))
                            {
                                state->result = nlohmann::json{{"error", err}}.dump();
                                std::lock_guard lock(state->mtx);
                                state->done = true;
                                state->cv.notify_one();
                                return;
                            }
                            if (wantsClip)
                                persistentChanged = true;
                        }

                        sprite = sc.GetSpriteComponent(node);
                        if (sprite)
                        {
                            if (args.contains("speed") && args["speed"].is_number())
                            {
                                sprite->playbackSpeed = std::max(0.0f, args["speed"].get<float>());
                                persistentChanged = true;
                            }
                            if (args.contains("loop") && args["loop"].is_boolean())
                            {
                                sprite->loop = args["loop"].get<bool>();
                                persistentChanged = true;
                            }
                            if (wantsPlaying)
                                sprite->playing = args["playing"].get<bool>();
                            if (persistentChanged)
                                sc.MarkDirty();
                            state->result = nlohmann::json{{"status", "ok"}, {"node", MakeNodeId(node)}, {"sprite", SpriteComponentJson(*sprite)}}.dump();
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::PauseSprite(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                    state->result = nlohmann::json{{"error", err}}.dump();
                else
                {
                    const bool paused = args.value("paused", true);
                    sc.SetSpritePlaying(node, !paused);
                    const NodeSpriteComponent *sprite = sc.GetSpriteComponent(node);
                    state->result = nlohmann::json{{"status", "ok"}, {"node", MakeNodeId(node)}, {"sprite", sprite ? SpriteComponentJson(*sprite) : nlohmann::json(nullptr)}}.dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::StopSprite(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                    state->result = nlohmann::json{{"error", err}}.dump();
                else
                {
                    sc.StopSprite(node);
                    const NodeSpriteComponent *sprite = sc.GetSpriteComponent(node);
                    state->result = nlohmann::json{{"status", "ok"}, {"node", MakeNodeId(node)}, {"sprite", sprite ? SpriteComponentJson(*sprite) : nlohmann::json(nullptr)}}.dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::ReadSpriteMetadata(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string pathArg = args.value("path", args.value("metadata_path", ""));
        if (pathArg.empty())
            return R"({"error":"path required"})";

        const std::filesystem::path path = ResolveSpriteMetadataPath(pathArg);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            return R"({"error":"path must stay inside Assets/"})";

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return JsonObj({{"error", JsonStr("sprite metadata not found: " + pathArg)}});

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded())
            return R"({"error":"invalid sprite metadata json"})";

        nlohmann::json result;
        result["path"] = PathUtf8(path);
        result["metadata"] = std::move(root);
        return result.dump();
    }

    std::string EditorToolRuntime::ValidateSpriteAsset(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        std::string pathArg = args.value("path", args.value("metadata_path", ""));
        if (nodeId.empty())
            return ValidateSpriteMetadataAsset(pathArg).dump();

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, nodeId, pathArg]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            nlohmann::json result;
            if (!renderer)
            {
                result = ValidateSpriteMetadataAsset(pathArg);
                result["node_validation"] = {{"valid", false}, {"error", "renderer not available"}};
                result["errors"].push_back("renderer not available for node validation");
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                const NodeSpriteComponent *sprite = node ? sc.GetSpriteComponent(node) : nullptr;
                std::string resolvedPathArg = pathArg;
                if (resolvedPathArg.empty() && sprite)
                    resolvedPathArg = sprite->metadataPath;

                result = ValidateSpriteMetadataAsset(resolvedPathArg);
                nlohmann::json nodeValidation;
                nodeValidation["valid"] = false;
                nodeValidation["requested"] = nodeId;
                if (!node)
                {
                    nodeValidation["error"] = err;
                    result["errors"].push_back(err);
                }
                else
                {
                    nodeValidation["node"] = MakeNodeId(node);
                    nodeValidation["name"] = sc.GetNodeName(node);
                    nodeValidation["has_sprite"] = sprite != nullptr;
                    if (!sprite)
                        result["errors"].push_back("node has no sprite component");

                    const int meshSlot = args.value("mesh_slot", sprite ? sprite->meshSlot : 0);
                    nodeValidation["mesh_slot"] = meshSlot;
                    const auto &refs = sc.GetMeshRefs(node);
                    nodeValidation["mesh_ref_count"] = refs.size();
                    if (meshSlot < 0 || meshSlot >= static_cast<int>(refs.size()))
                    {
                        nodeValidation["mesh_valid"] = false;
                        result["errors"].push_back("node sprite mesh slot out of range");
                    }
                    else if (!sc.IsValidMeshIndex(refs[meshSlot]))
                    {
                        nodeValidation["mesh_valid"] = false;
                        result["errors"].push_back("node sprite mesh slot does not reference a valid mesh");
                    }
                    else
                    {
                        const Mesh &mesh = sc.GetMesh(refs[meshSlot]);
                        const bool quad = mesh.vertexCount == 4;
                        nodeValidation["mesh_valid"] = true;
                        nodeValidation["mesh_index"] = refs[meshSlot];
                        nodeValidation["vertex_count"] = mesh.vertexCount;
                        nodeValidation["index_count"] = mesh.indexCount;
                        nodeValidation["is_quad"] = quad;
                        if (!quad)
                            result["errors"].push_back("node sprite mesh is not a four-vertex quad");
                    }
                    nodeValidation["valid"] = nodeValidation.value("has_sprite", false) &&
                                               nodeValidation.value("mesh_valid", false) &&
                                               nodeValidation.value("is_quad", false);
                }

                result["node_validation"] = std::move(nodeValidation);
            }

            result["valid"] = result["errors"].empty();
            result["error_count"] = result["errors"].size();
            result["warning_count"] = result["warnings"].size();
            state->result = result.dump();
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::WriteSpriteMetadata(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string pathArg = args.value("path", args.value("metadata_path", ""));
        if (pathArg.empty())
            return R"({"error":"path required"})";

        const std::filesystem::path path = ResolveSpriteMetadataPath(pathArg);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            return R"({"error":"path must stay inside Assets/"})";

        const std::string imagePath = args.value("image", args.value("image_path", ""));
        int imageW = 0;
        int imageH = 0;
        if (args.contains("image_size") && args["image_size"].is_object())
        {
            imageW = args["image_size"].value("width", 0);
            imageH = args["image_size"].value("height", 0);
        }
        if ((imageW <= 0 || imageH <= 0) && !imagePath.empty())
        {
            const std::filesystem::path imageResolved = ResolveTexturePathForTool(imagePath);
            int channels = 0;
            stbi_info(PathUtf8(imageResolved).c_str(), &imageW, &imageH, &channels);
        }

        nlohmann::json grid = args.contains("grid") && args["grid"].is_object() ? args["grid"] : nlohmann::json::object();
        nlohmann::json frames = args.contains("frames") && args["frames"].is_array() ? args["frames"] : nlohmann::json::array();
        if ((frames.empty() || args.value("generate_grid", false)) && !grid.empty())
            frames = GenerateSpriteFramesJson(grid, imageW, imageH);

        nlohmann::json clips = args.contains("clips") && args["clips"].is_array() ? args["clips"] : nlohmann::json::array();
        if (clips.empty() && !frames.empty())
        {
            clips.push_back({
                {"name", "default"},
                {"start", 0},
                {"end", static_cast<int>(frames.size()) - 1},
                {"fps", args.value("fps", 10.0f)},
                {"loop", args.value("loop", true)},
            });
        }

        nlohmann::json root;
        root["schema"] = "phasma.sprite_editor.v1";
        root["image"] = imagePath;
        if (imageW > 0 && imageH > 0)
            root["image_size"] = {{"width", imageW}, {"height", imageH}};
        if (!grid.empty())
            root["grid"] = std::move(grid);
        root["frames"] = std::move(frames);
        root["clips"] = std::move(clips);

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return JsonObj({{"error", JsonStr("failed to create metadata directory: " + ec.message())}});

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return JsonObj({{"error", JsonStr("failed to open metadata for writing: " + PathUtf8(path))}});
        out << root.dump(2) << "\n";
        if (!out)
            return JsonObj({{"error", JsonStr("failed to write metadata: " + PathUtf8(path))}});

        nlohmann::json result;
        result["status"] = "ok";
        result["path"] = PathUtf8(path);
        result["asset_path"] = AssetRelativePathForTool(path);
        result["frame_count"] = root["frames"].size();
        result["clip_count"] = root["clips"].size();
        result["metadata"] = std::move(root);
        return result.dump();
    }

    std::string EditorToolRuntime::AddSpriteFrame(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        nlohmann::json frame;
        if (!ApplySpriteFrameFields(frame, args, true, error))
            return nlohmann::json{{"error", error}}.dump();

        if (frame.value("name", "") == "frame")
            frame["name"] = "frame_" + std::to_string(root["frames"].size());

        int index = args.value("index", static_cast<int>(root["frames"].size()));
        index = std::clamp(index, 0, static_cast<int>(root["frames"].size()));
        root["frames"].insert(root["frames"].begin() + index, std::move(frame));
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["frame_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::UpdateSpriteFrame(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        const int index = FindNamedOrIndexedItem(root["frames"], args, {"index", "frame", "frame_index"}, {"frame_name"}, error);
        if (index < 0)
            return nlohmann::json{{"error", error}}.dump();

        if (!ApplySpriteFrameFields(root["frames"][index], args, false, error))
            return nlohmann::json{{"error", error}}.dump();
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["frame_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::RemoveSpriteFrame(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        const int index = FindNamedOrIndexedItem(root["frames"], args, {"index", "frame", "frame_index"}, {"name", "frame_name"}, error);
        if (index < 0)
            return nlohmann::json{{"error", error}}.dump();

        root["frames"].erase(root["frames"].begin() + index);
        for (auto &clip : root["clips"])
        {
            if (!clip.is_object())
                continue;
            int start = clip.value("start", 0);
            int end = clip.value("end", start);
            if (start > index)
                --start;
            if (end > index)
                --end;
            clip["start"] = std::max(0, start);
            clip["end"] = std::max(0, end);
        }
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["removed_frame_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::ReorderSpriteFrames(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        nlohmann::json &frames = root["frames"];
        const int frameCount = static_cast<int>(frames.size());
        if (frameCount <= 0)
            return R"({"error":"sprite metadata has no frames"})";

        ClampSpriteClips(root);

        std::vector<int> newToOld;
        newToOld.reserve(frameCount);
        if (args.contains("order"))
        {
            if (!args["order"].is_array() || static_cast<int>(args["order"].size()) != frameCount)
                return nlohmann::json{{"error", "order must contain every old frame index exactly once"}}.dump();

            std::vector<bool> seen(frameCount, false);
            for (const auto &item : args["order"])
            {
                if (!item.is_number_integer())
                    return nlohmann::json{{"error", "order entries must be integers"}}.dump();
                const int oldIndex = item.get<int>();
                if (oldIndex < 0 || oldIndex >= frameCount || seen[oldIndex])
                    return nlohmann::json{{"error", "order must be a permutation of old frame indices"}}.dump();
                seen[oldIndex] = true;
                newToOld.push_back(oldIndex);
            }
        }
        else
        {
            const int from = FindNamedOrIndexedItem(frames,
                                                    args,
                                                    {"from_index", "index", "frame", "frame_index"},
                                                    {"frame_name", "name"},
                                                    error);
            if (from < 0)
                return nlohmann::json{{"error", error}}.dump();

            int to = args.value("to_index", args.value("to", -1));
            if (to < 0)
                return R"({"error":"to_index required"})";
            to = std::clamp(to, 0, frameCount - 1);

            newToOld.resize(frameCount);
            std::iota(newToOld.begin(), newToOld.end(), 0);
            const int movedOldIndex = newToOld[from];
            newToOld.erase(newToOld.begin() + from);
            newToOld.insert(newToOld.begin() + to, movedOldIndex);
        }

        std::vector<int> oldToNew(frameCount, -1);
        nlohmann::json reorderedFrames = nlohmann::json::array();
        for (int newIndex = 0; newIndex < frameCount; ++newIndex)
        {
            const int oldIndex = newToOld[newIndex];
            oldToNew[oldIndex] = newIndex;
            reorderedFrames.push_back(frames[oldIndex]);
        }

        nlohmann::json clipRemaps = nlohmann::json::array();
        if (root.contains("clips") && root["clips"].is_array())
        {
            for (int clipIndex = 0; clipIndex < static_cast<int>(root["clips"].size()); ++clipIndex)
            {
                auto &clip = root["clips"][clipIndex];
                if (!clip.is_object())
                    continue;

                const int oldStart = clip.value("start", 0);
                const int oldEnd = clip.value("end", oldStart);
                int newStart = frameCount - 1;
                int newEnd = 0;
                for (int oldFrame = oldStart; oldFrame <= oldEnd; ++oldFrame)
                {
                    if (oldFrame < 0 || oldFrame >= frameCount || oldToNew[oldFrame] < 0)
                        continue;
                    newStart = std::min(newStart, oldToNew[oldFrame]);
                    newEnd = std::max(newEnd, oldToNew[oldFrame]);
                }

                if (newStart > newEnd)
                {
                    newStart = 0;
                    newEnd = 0;
                }

                clip["start"] = newStart;
                clip["end"] = newEnd;
                const bool expanded = (newEnd - newStart) != (oldEnd - oldStart);
                const std::string clipName = clip.contains("name") && clip["name"].is_string() && !clip["name"].get<std::string>().empty()
                                                 ? clip["name"].get<std::string>()
                                                 : "clip_" + std::to_string(clipIndex);
                clipRemaps.push_back({
                    {"clip_index", clipIndex},
                    {"name", clipName},
                    {"old_range", {oldStart, oldEnd}},
                    {"new_range", {newStart, newEnd}},
                    {"range_expanded", expanded},
                });
            }
        }

        root["frames"] = std::move(reorderedFrames);
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        nlohmann::json oldToNewJson = nlohmann::json::array();
        for (int value : oldToNew)
            oldToNewJson.push_back(value);
        nlohmann::json newToOldJson = nlohmann::json::array();
        for (int value : newToOld)
            newToOldJson.push_back(value);

        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["old_to_new"] = std::move(oldToNewJson);
        result["new_to_old"] = std::move(newToOldJson);
        result["clip_remaps"] = std::move(clipRemaps);
        return result.dump();
    }

    std::string EditorToolRuntime::AddSpriteClip(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        nlohmann::json clip;
        if (!ApplySpriteClipFields(clip, args, true, error))
            return nlohmann::json{{"error", error}}.dump();
        if (clip.value("name", "") == "clip")
            clip["name"] = "clip_" + std::to_string(root["clips"].size());

        int index = args.value("index", static_cast<int>(root["clips"].size()));
        index = std::clamp(index, 0, static_cast<int>(root["clips"].size()));
        root["clips"].insert(root["clips"].begin() + index, std::move(clip));
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["clip_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::UpdateSpriteClip(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        const int index = FindNamedOrIndexedItem(root["clips"], args, {"index", "clip", "clip_index"}, {"clip_name"}, error);
        if (index < 0)
            return nlohmann::json{{"error", error}}.dump();

        if (!ApplySpriteClipFields(root["clips"][index], args, false, error))
            return nlohmann::json{{"error", error}}.dump();
        ClampSpriteClips(root);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["clip_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::RemoveSpriteClip(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::filesystem::path path;
        nlohmann::json root;
        std::string error;
        if (!LoadSpriteMetadataForEdit(args.value("path", args.value("metadata_path", "")), path, root, error))
            return nlohmann::json{{"error", error}}.dump();

        const int index = FindNamedOrIndexedItem(root["clips"], args, {"index", "clip", "clip_index"}, {"name", "clip_name"}, error);
        if (index < 0)
            return nlohmann::json{{"error", error}}.dump();

        root["clips"].erase(root["clips"].begin() + index);

        if (!SaveJsonAsset(path, root, error))
            return nlohmann::json{{"error", error}}.dump();
        nlohmann::json result = SpriteMetadataEditResult(path, root);
        result["removed_clip_index"] = index;
        return result.dump();
    }

    std::string EditorToolRuntime::ReadSpriteSheet(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string pathArg = args.value("path", args.value("sheet_path", ""));
        if (pathArg.empty())
            return R"({"error":"path required"})";

        const std::filesystem::path path = ResolveSpriteSheetPath(pathArg);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            return R"({"error":"path must stay inside Assets/"})";

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return JsonObj({{"error", JsonStr("sprite sheet asset not found: " + pathArg)}});

        nlohmann::json root = nlohmann::json::parse(in, nullptr, false);
        if (root.is_discarded())
            return R"({"error":"invalid sprite sheet json"})";

        nlohmann::json result;
        result["path"] = PathUtf8(path);
        result["asset_path"] = AssetRelativePathForTool(path);
        result["sheet"] = std::move(root);
        return result.dump();
    }

    std::string EditorToolRuntime::WriteSpriteSheet(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string pathArg = args.value("path", args.value("sheet_path", ""));
        if (pathArg.empty())
            return R"({"error":"path required"})";

        const std::filesystem::path path = ResolveSpriteSheetPath(pathArg);
        if (!pmcp::IsPathSafe(path.string(), Path::Assets))
            return R"({"error":"path must stay inside Assets/"})";

        if (!args.contains("images") || !args["images"].is_array() || args["images"].empty())
            return R"({"error":"images array required"})";

        nlohmann::json images = nlohmann::json::array();
        for (const nlohmann::json &entry : args["images"])
        {
            nlohmann::json normalizedEntry;
            std::string error;
            if (!ReadSpriteSheetImageEntry(entry, normalizedEntry, error))
                return JsonObj({{"error", JsonStr(error)}});
            images.push_back(std::move(normalizedEntry));
        }

        nlohmann::json root;
        root["schema"] = "phasma.sprite_sheet.v1";
        root["images"] = std::move(images);

        std::error_code ec;
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
            return JsonObj({{"error", JsonStr("failed to create sheet directory: " + ec.message())}});

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return JsonObj({{"error", JsonStr("failed to open sheet asset for writing: " + PathUtf8(path))}});
        out << root.dump(2) << "\n";
        if (!out)
            return JsonObj({{"error", JsonStr("failed to write sheet asset: " + PathUtf8(path))}});

        nlohmann::json result;
        result["status"] = "ok";
        result["path"] = PathUtf8(path);
        result["asset_path"] = AssetRelativePathForTool(path);
        result["image_count"] = root["images"].size();
        result["sheet"] = std::move(root);
        return result.dump();
    }

    std::string EditorToolRuntime::CreateSpriteFromSheet(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string sheetArg = args.value("sheet", args.value("sheet_path", ""));
        if (sheetArg.empty())
            return R"({"error":"sheet required"})";

        const std::filesystem::path sheetPath = ResolveSpriteSheetPath(sheetArg);
        if (!pmcp::IsPathSafe(sheetPath.string(), Path::Assets))
            return R"({"error":"sheet path must stay inside Assets/"})";

        std::ifstream in(sheetPath, std::ios::binary);
        if (!in)
            return JsonObj({{"error", JsonStr("sprite sheet asset not found: " + sheetArg)}});

        nlohmann::json sheet = nlohmann::json::parse(in, nullptr, false);
        if (!sheet.is_object())
            return R"({"error":"invalid sprite sheet json"})";

        const int imageIndex = std::max(0, args.value("image_index", args.value("index", 0)));
        std::filesystem::path imagePath;
        std::string imageLabel;
        std::string error;
        if (!ReadSpriteSheetImageAt(sheet, sheetPath, imageIndex, imagePath, imageLabel, error))
            return nlohmann::json{{"error", error}}.dump();

        int imageW = 0;
        int imageH = 0;
        int channels = 0;
        stbi_info(PathUtf8(imagePath).c_str(), &imageW, &imageH, &channels);
        if (imageW <= 0 || imageH <= 0)
            return JsonObj({{"error", JsonStr("failed to inspect sheet image: " + PathUtf8(imagePath))}});

        std::filesystem::path metadataPath;
        const std::string metadataArg = args.value("metadata_path", args.value("path", ""));
        if (!metadataArg.empty())
        {
            metadataPath = ResolveSpriteMetadataPath(metadataArg);
        }
        else
        {
            std::filesystem::path stem = sheetPath;
            std::string base = stem.stem().generic_string();
            if (base.ends_with(".sheet"))
                base.resize(base.size() - 6);
            if (!imageLabel.empty())
                base += "_" + imageLabel;
            else
                base += "_" + imagePath.stem().generic_string();
            metadataPath = sheetPath.parent_path() / (base + ".sprite.json");
        }

        if (!pmcp::IsPathSafe(metadataPath.string(), Path::Assets))
            return R"({"error":"metadata path must stay inside Assets/"})";

        nlohmann::json grid = args.contains("grid") && args["grid"].is_object() ? args["grid"] : nlohmann::json::object();
        nlohmann::json frames = args.contains("frames") && args["frames"].is_array() ? args["frames"] : nlohmann::json::array();
        if ((frames.empty() || args.value("generate_grid", false)) && !grid.empty())
            frames = GenerateSpriteFramesJson(grid, imageW, imageH);
        if (frames.empty())
        {
            std::string frameName = args.value("frame_name", imageLabel.empty() ? imagePath.stem().generic_string() : imageLabel);
            if (frameName.empty())
                frameName = "frame_000";
            frames.push_back({
                {"name", frameName},
                {"rect", {0, 0, imageW, imageH}},
                {"pivot", {0.5f, 0.5f}},
                {"duration", args.value("duration", 0.1f)},
            });
        }

        nlohmann::json clips = args.contains("clips") && args["clips"].is_array() ? args["clips"] : nlohmann::json::array();
        if (clips.empty())
        {
            clips.push_back({
                {"name", args.value("clip_name", "default")},
                {"start", 0},
                {"end", static_cast<int>(frames.size()) - 1},
                {"fps", args.value("fps", 10.0f)},
                {"loop", args.value("loop", true)},
            });
        }

        nlohmann::json metadata;
        metadata["schema"] = "phasma.sprite_editor.v1";
        metadata["image"] = AssetRelativePathForTool(imagePath);
        metadata["sheet"] = AssetRelativePathForTool(sheetPath);
        metadata["image_index"] = imageIndex;
        metadata["image_size"] = {{"width", imageW}, {"height", imageH}};
        if (!grid.empty())
            metadata["grid"] = std::move(grid);
        metadata["frames"] = std::move(frames);
        metadata["clips"] = std::move(clips);

        if (!SaveJsonAsset(metadataPath, metadata, error))
            return nlohmann::json{{"error", error}}.dump();

        nlohmann::json result = SpriteMetadataEditResult(metadataPath, metadata);
        result["sheet_path"] = PathUtf8(sheetPath);
        result["sheet_asset_path"] = AssetRelativePathForTool(sheetPath);
        result["image_path"] = PathUtf8(imagePath);
        result["image_asset_path"] = AssetRelativePathForTool(imagePath);
        result["image_index"] = imageIndex;
        result["note"] = "Created metadata for one image from the sheet. Runtime clip playback is available through sprite playback tools.";

        if (!args.value("create_node", false))
            return result.dump();

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            nlohmann::json result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, args, metadataPath, result]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = nlohmann::json{{"error", "renderer not available"}};
            }
            else
            {
                Scene &sc = renderer->GetScene();
                NodeId *parentNode = nullptr;
                const std::string parent = args.value("parent", "");
                if (!parent.empty())
                {
                    std::string err;
                    parentNode = ResolveNode(sc, parent, err);
                    if (!parentNode)
                    {
                        state->result = nlohmann::json{{"error", err}};
                        std::lock_guard lock(state->mtx);
                        state->done = true;
                        state->cv.notify_one();
                        return;
                    }
                }

                SpriteAuthoring::Options options;
                ReadSpriteOptions(args, options);
                options.assetPath = metadataPath;
                if (options.frameIndex < 0 && options.frameName.empty())
                    options.frameIndex = 0;

                SpriteAuthoring::Result sprite = SpriteAuthoring::CreateNode(sc, options, parentNode);
                if (!sprite.error.empty())
                {
                    state->result = nlohmann::json{{"error", sprite.error}};
                }
                else
                {
                    std::string transformError;
                    const int meshSlot = std::max(0, args.value("mesh_slot", 0));
                    if (!ApplySpriteJsonTransform(sc, sprite.node, args, meshSlot, transformError))
                    {
                        state->result = nlohmann::json{{"error", transformError}};
                    }
                    else
                    {
                        state->result = result;
                        state->result["node_result"] = SpriteResultJson(sc, sprite);
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result.dump();
    }

    std::string EditorToolRuntime::SetNodeTransform(const std::string &nodeId, const float *pos, const float *rot, const float *scale) const
    {
        // Copy values (caller may free)
        float p[3] = {}, r[3] = {}, s[3] = {};
        bool hasPos = pos != nullptr, hasRot = rot != nullptr, hasScale = scale != nullptr;
        if (hasPos)
        {
            p[0] = pos[0];
            p[1] = pos[1];
            p[2] = pos[2];
        }
        if (hasRot)
        {
            r[0] = rot[0];
            r[1] = rot[1];
            r[2] = rot[2];
        }
        if (hasScale)
        {
            s[0] = scale[0];
            s[1] = scale[1];
            s[2] = scale[2];
        }

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, nodeId, p, r, s, hasPos, hasRot, hasScale]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node) { state->result = JsonObj({{"error", JsonStr(err)}}); }
                else
                {
                    mat4 local = sc.GetLocalMatrix(node);

                    // Position-only: modify translation column in-place (no decomposition)
                    if (hasPos && !hasRot && !hasScale)
                    {
                        local[3] = vec4(p[0], p[1], p[2], local[3].w);
                        sc.SetLocalMatrix(node, local);
                    }
                    else
                    {
                        // Full TRS recomposition — guard against zero-length axes
                        vec3 curScale(length(vec3(local[0])), length(vec3(local[1])), length(vec3(local[2])));
                        float sx = curScale.x > 1e-6f ? curScale.x : 1.0f;
                        float sy = curScale.y > 1e-6f ? curScale.y : 1.0f;
                        float sz = curScale.z > 1e-6f ? curScale.z : 1.0f;
                        mat3 rotMat(vec3(local[0]) / sx, vec3(local[1]) / sy, vec3(local[2]) / sz);

                        vec3 newPos = hasPos ? vec3(p[0], p[1], p[2]) : vec3(local[3]);
                        vec3 newRot = hasRot ? vec3(glm::radians(r[0]), glm::radians(r[1]), glm::radians(r[2])) : glm::eulerAngles(quat_cast(rotMat));
                        vec3 newScale = hasScale ? vec3(s[0], s[1], s[2]) : curScale;

                        mat4 newLocal = glm::translate(mat4(1.f), newPos) * mat4_cast(quat(newRot)) * glm::scale(mat4(1.f), newScale);
                        sc.SetLocalMatrix(node, newLocal);
                    }
                    sc.MarkDirty();
                    state->result = R"({"ok":true})";
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::AddMeshToNode(const std::string &nodeId, const std::string &primitive) const
    {
        // Validate primitive type upfront
        static const std::unordered_set<std::string> validPrims = {
            "plane", "grid", "cube", "sphere", "uv_sphere", "ico_sphere", "cylinder", "cone", "pyramid", "quad", "circle", "torus", "skinned_strip_2d"};
        if (validPrims.find(primitive) == validPrims.end())
            return R"({"error":"unknown primitive type. Valid: plane, grid, cube, sphere, uv_sphere, ico_sphere, cylinder, cone, pyramid, quad, circle, torus, skinned_strip_2d"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, nodeId, primitive]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node) { state->result = JsonObj({{"error", JsonStr(err)}}); }
                else
                {
                    ModelAsset *model = nullptr;
                    if (primitive == "plane") model = Primitives::CreatePlane();
                    else if (primitive == "grid") model = Primitives::CreateGrid();
                    else if (primitive == "cube") model = Primitives::CreateCube();
                    else if (primitive == "sphere") model = Primitives::CreateSphere();
                    else if (primitive == "uv_sphere") model = Primitives::CreateUvSphere();
                    else if (primitive == "ico_sphere") model = Primitives::CreateIcoSphere();
                    else if (primitive == "cylinder") model = Primitives::CreateCylinder();
                    else if (primitive == "cone") model = Primitives::CreateCone();
                    else if (primitive == "pyramid") model = Primitives::CreatePyramid();
                    else if (primitive == "quad") model = Primitives::CreateQuad();
                    else if (primitive == "circle") model = Primitives::CreateCircle();
                    else if (primitive == "torus") model = Primitives::CreateTorus();
                    else if (primitive == "skinned_strip_2d") model = Primitives::CreateSkinnedStrip2D();

                    if (model)
                    {
                        sc.AttachPrimitiveToNode(node, model);
                        sc.SetGeometryDirty();
                        int meshCount = static_cast<int>(sc.GetNodeCache(node).meshRefs->meshRefs.size());
                        state->result = JsonObj({{"ok", "true"}, {"mesh_count", std::to_string(meshCount)}});
                    }
                    else
                    {
                        state->result = R"({"error":"failed to create primitive"})";
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetNodeMaterial(const std::string &nodeId, int slot, const std::string &propsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();
        auto props = nlohmann::json::parse(propsJson, nullptr, false);
        if (props.is_discarded())
            return R"({"error":"invalid properties JSON"})";

        QueueAction([state, nodeId, slot, props]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node) { state->result = JsonObj({{"error", JsonStr(err)}}); }
                else
                {
                    const auto &refs = sc.GetNodeCache(node).meshRefs->meshRefs;
                    if (slot < 0 || slot >= static_cast<int>(refs.size()))
                    {
                        state->result = R"({"error":"mesh slot out of range"})";
                    }
                    else
                    {
                        int meshIdx = refs[slot];
                        if (meshIdx < 0)
                        {
                            state->result = R"({"error":"no mesh at slot"})";
                        }
                        else
                        {
                            Mesh &mesh = sc.GetMesh(meshIdx);
                            if (!mesh.material)
                            {
                                state->result = R"({"error":"no material on mesh"})";
                            }
                            else
                            {
                                // Auto-create MaterialInstance for per-mesh edits if shared
                                MaterialInstance *inst = mesh.materialInstance;
                                if (!inst)
                                    inst = sc.CreateMaterialInstance(mesh);

                                bool renderTypeChanged = false;
                                if (inst)
                                {
                                    // Validate and apply base_color
                                    if (props.contains("base_color") && props["base_color"].is_array() && props["base_color"].size() >= 3)
                                    {
                                        auto &c = props["base_color"];
                                        try
                                        {
                                            if (!c[0].is_number() || !c[1].is_number() || !c[2].is_number())
                                                throw std::exception();
                                            float a = c.size() >= 4 && c[3].is_number() ? c[3].get<float>() : 1.0f;
                                            inst->SetBaseColorFactor(vec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(), a));
                                        }
                                        catch (...)
                                        {
                                            state->result = R"({"error":"invalid base_color array: must contain 3-4 numbers"})";
                                            return;
                                        }
                                    }
                                    // Validate and apply metallic
                                    if (props.contains("metallic"))
                                    {
                                        try
                                        {
                                            if (!props["metallic"].is_number())
                                                throw std::exception();
                                            inst->SetMetallic(props["metallic"].get<float>());
                                        }
                                        catch (...)
                                        {
                                            state->result = R"({"error":"invalid metallic: must be a number"})";
                                            return;
                                        }
                                    }
                                    // Validate and apply roughness
                                    if (props.contains("roughness"))
                                    {
                                        try
                                        {
                                            if (!props["roughness"].is_number())
                                                throw std::exception();
                                            inst->SetRoughness(props["roughness"].get<float>());
                                        }
                                        catch (...)
                                        {
                                            state->result = R"({"error":"invalid roughness: must be a number"})";
                                            return;
                                        }
                                    }
                                    // Validate and apply transmission
                                    if (props.contains("transmission"))
                                    {
                                        try
                                        {
                                            if (!props["transmission"].is_number())
                                                throw std::exception();
                                            inst->SetTransmissionFactor(props["transmission"].get<float>());
                                        }
                                        catch (...)
                                        {
                                            state->result = R"({"error":"invalid transmission: must be a number"})";
                                            return;
                                        }
                                    }
                                    if (props.contains("render_type"))
                                    {
                                        std::string rt = props["render_type"].get<std::string>();
                                        RenderType newRT = mesh.renderType;
                                        if (rt == "opaque") newRT = RenderType::Opaque;
                                        else if (rt == "alpha_cut") newRT = RenderType::AlphaCut;
                                        else if (rt == "alpha_blend") newRT = RenderType::AlphaBlend;
                                        else if (rt == "transmission") newRT = RenderType::Transmission;
                                        else if (rt == "lines") newRT = RenderType::Lines;
                                        if (newRT != mesh.renderType)
                                        {
                                            inst->SetRenderType(newRT);
                                            mesh.renderType = newRT;
                                            renderTypeChanged = true;
                                        }
                                    }
                                }

                                if (renderTypeChanged)
                                    sc.SetGeometryDirty();
                                sc.SetTexturesDirty();
                                sc.SetMaterialDirty();
                                sc.MarkNodeDirty(node);
                                state->result = R"({"ok":true})";
                            }
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::AddLight(const std::string &type) const
    {
        static const std::unordered_set<std::string> valid = {"directional", "point", "spot", "area"};
        if (valid.find(type) == valid.end())
            return R"({"error":"unknown light type. Valid: directional, point, spot, area"})";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, type]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = r->GetScene();
                if (type == "directional") sc.CreateDirectionalLight();
                else if (type == "point") sc.CreatePointLight();
                else if (type == "spot") sc.CreateSpotLight();
                else if (type == "area") sc.CreateAreaLight();
                state->result = JsonObj({{"ok", "true"}, {"type", JsonStr(type)}});
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::QueryScene() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r) { state->result = R"({"error":"renderer not available"})"; }
            else
            {
                Scene &sc = r->GetScene();
                nlohmann::json result;
                result["cameras"] = static_cast<int>(sc.GetCameras().size());
                result["total_nodes"] = sc.GetNodeCount();

                nlohmann::json nodes = nlohmann::json::array();
                std::function<void(NodeId *, int)> walk = [&](NodeId *node, int depth)
                {
                    const auto &cache = sc.GetNodeCache(node);
                    nlohmann::json n;
                    n["name"] = cache.name->name;
                    n["depth"] = depth;
                    n["index"] = node->index;
                    n["id"] = MakeNodeId(node);
                    n["mesh_count"] = static_cast<int>(cache.meshRefs->meshRefs.size());
                    n["children"] = static_cast<int>(cache.hierarchy->children.size());
                    nodes.push_back(n);
                    for (NodeId *child : cache.hierarchy->children)
                        walk(child, depth + 1);
                };

                // Walk root nodes (no parent)
                for (uint32_t i = 0; i < sc.GetNodeCount(); i++)
                {
                    NodeId *n = sc.GetNodeId(i);
                    if (!sc.GetParent(n))
                        walk(n, 0);
                }
                result["nodes"] = nodes;
                state->result = result.dump();
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::GetSceneInfo(bool includeTree) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, includeTree]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = r->GetScene();
                const auto &settings = Settings::Get<GlobalSettings>();
                nlohmann::json result;
                result["scene_path"] = sc.GetScenePath().empty() ? "" : sc.GetScenePath().string();
                result["scene_name"] = sc.GetSceneName();
                result["render_mode"] = RenderModeName(settings.render_mode);
                result["viewport_aspect_mode"] = SceneViewAspectModeName(settings.scene_view_aspect_mode);
                result["render_scale"] = settings.render_scale;
                result["skybox"] = settings.skybox_path;
                result["scene_dirty"] = sc.IsDirty();

                nlohmann::json counts;
                counts["nodes"] = sc.GetNodeCount();
                counts["meshes"] = sc.GetMeshCount();
                counts["models"] = sc.GetModels().size();
                counts["cameras"] = sc.GetCameras().size();
                counts["lights"] = sc.GetDirectionalLights().size() + sc.GetPointLights().size() +
                                   sc.GetSpotLights().size() + sc.GetAreaLights().size();
                counts["directional_lights"] = sc.GetDirectionalLights().size();
                counts["point_lights"] = sc.GetPointLights().size();
                counts["spot_lights"] = sc.GetSpotLights().size();
                counts["area_lights"] = sc.GetAreaLights().size();
                result["counts"] = std::move(counts);

                nlohmann::json camera = nullptr;
                if (!sc.GetCameras().empty())
                {
                    Camera *active = sc.GetActiveCamera();
                    if (active)
                    {
                        camera = nlohmann::json::object();
                        camera["name"] = active->GetName();
                        if (NodeId *node = active->GetNodeId())
                            camera["node_id"] = MakeNodeId(node);
                        camera["projection"] = active->IsOrthographic() ? "orthographic" : "perspective";
                        camera["position"] = Vec3Json(active->GetPosition());
                        camera["rotation_euler_deg"] = Vec3Json(glm::degrees(active->GetEuler()));
                        camera["front"] = Vec3Json(active->GetFront());
                        camera["up"] = Vec3Json(active->GetUp());
                        camera["right"] = Vec3Json(active->GetRight());
                        camera["fov_y_deg"] = glm::degrees(active->Fovy());
                        camera["fov_x_deg"] = glm::degrees(active->Fovx());
                        camera["orthographic_size"] = active->GetOrthographicSize();
                        camera["near_plane"] = active->GetNearPlane();
                        camera["far_plane"] = active->GetFarPlane();
                    }
                }
                result["active_camera"] = std::move(camera);

                nlohmann::json editor;
                editor["play_mode"] = IsScriptPlayMode() ? "playing" : "stopped";
                editor["paused"] = IsScriptPaused();
                result["editor"] = std::move(editor);

                if (includeTree)
                {
                    nlohmann::json nodes = nlohmann::json::array();
                    std::function<void(NodeId *, int)> walk = [&](NodeId *node, int depth)
                    {
                        const auto &cache = sc.GetNodeCache(node);
                        nlohmann::json n;
                        n["name"] = cache.name->name;
                        n["depth"] = depth;
                        n["index"] = node->index;
                        n["id"] = MakeNodeId(node);
                        n["mesh_count"] = static_cast<int>(cache.meshRefs->meshRefs.size());
                        n["children"] = static_cast<int>(cache.hierarchy->children.size());
                        nodes.push_back(n);
                        for (NodeId *child : cache.hierarchy->children)
                            walk(child, depth + 1);
                    };
                    for (uint32_t i = 0; i < sc.GetNodeCount(); i++)
                    {
                        NodeId *n = sc.GetNodeId(i);
                        if (!sc.GetParent(n))
                            walk(n, 0);
                    }
                    result["nodes"] = std::move(nodes);
                }

                state->result = result.dump();
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::GetSceneDigest() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                SceneDigest d = ComputeSceneDigest(r->GetScene());

                auto aabbJson = [](const AABB &a) {
                    return nlohmann::json{
                        {"min", Vec3Json(a.min)}, {"max", Vec3Json(a.max)},
                        {"center", Vec3Json(a.GetCenter())}, {"size", Vec3Json(a.GetSize())}};
                };

                nlohmann::json result;
                result["ground_y"] = d.groundY;
                result["total_node_count"] = d.totalNodeCount;
                result["mesh_node_count"] = d.meshNodeCount;
                result["overlaps_truncated"] = d.overlapsTruncated;
                result["world_bounds"] = d.hasBounds ? aabbJson(d.worldBounds) : nlohmann::json(nullptr);

                nlohmann::json nodes = nlohmann::json::array();
                for (const SceneDigestNode &n : d.nodes)
                {
                    nlohmann::json jn;
                    jn["id"] = n.id;
                    jn["name"] = n.name;
                    jn["enabled"] = n.enabled;
                    jn["visible"] = n.visible;
                    jn["in_frustum"] = n.inFrustum;
                    jn["ground_outlier"] = n.groundOutlier;
                    jn["parent_name"] = n.parentName;
                    jn["aabb"] = aabbJson(n.aabb);
                    nodes.push_back(std::move(jn));
                }
                result["nodes"] = std::move(nodes);

                nlohmann::json overlaps = nlohmann::json::array();
                for (const auto &pair : d.overlaps)
                {
                    overlaps.push_back(nlohmann::json{
                        {"a", pair.first}, {"b", pair.second},
                        {"a_name", d.nodes[pair.first].name}, {"b_name", d.nodes[pair.second].name}});
                }
                result["overlaps"] = std::move(overlaps);

                state->result = result.dump();
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    // Annotated "map shot": frame an ortho top-down camera over the scene's visible
    // world bounds, turn on AABB-box display, capture the composited 'display' RT to
    // PNG, and write a sidecar manifest that projects every visible node's AABB to
    // pixel space (so labels can be drawn on the image and boxes mapped to names).
    // Composes existing passes (GridPass + AabbsPass already draw into 'display') and
    // the existing CaptureImageResource readback — no new render pass.
    std::string EditorToolRuntime::GetMapShot(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const float padding = std::max(1.0f, args.value("padding", 1.1f));
        const int maxDim = std::max(256, args.value("max_dimension", 2048));
        // Boxes are opt-in: the sidecar manifest already carries every node's AABB in
        // pixel space, so the default image is a clean top-down render. Drawing the
        // engine AABB pass into the image (draw_boxes:true) is noisy on dense scenes.
        const bool drawBoxes = args.value("draw_boxes", false);

        struct Shot
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            bool ok = false;
            std::string error;

            // Prior camera + settings, restored after the shot.
            vec3 camPos{};
            vec3 camEuler{};
            bool wasOrtho = false;
            float orthoSize = 1.0f;
            float nearP = 0.1f;
            float farP = 1000.0f;
            bool drawAabbs = false;
            bool aabbsDepthAware = true;

            SceneDigest digest;
        };
        auto shot = std::make_shared<Shot>();

        // --- Phase A: frame the ortho top-down camera (+ optional AABB display) ------
        QueueAction([shot, padding, drawBoxes]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r) { shot->error = "renderer not available"; }
            else
            {
                Scene &sc = r->GetScene();
                if (sc.GetCameras().empty()) { shot->error = "no camera in scene"; }
                else
                {
                    SceneDigest d = ComputeSceneDigest(sc);
                    if (!d.hasBounds) { shot->error = "no enabled+visible geometry to frame"; }
                    else
                    {
                        Camera *cam = sc.GetActiveCamera();
                        shot->camPos = cam->GetPosition();
                        shot->camEuler = cam->GetEuler();
                        shot->wasOrtho = cam->IsOrthographic();
                        shot->orthoSize = cam->GetOrthographicSize();
                        shot->nearP = cam->GetNearPlane();
                        shot->farP = cam->GetFarPlane();
                        auto &gs = Settings::Get<GlobalSettings>();
                        shot->drawAabbs = gs.draw_aabbs;
                        shot->aabbsDepthAware = gs.aabbs_depth_aware;
                        shot->digest = d;

                        const vec3 c = d.worldBounds.GetCenter();
                        const vec3 sz = d.worldBounds.GetSize();
                        const float aspect = std::max(0.01f, cam->GetAspect());
                        const float span = std::max(sz.x, sz.z);
                        // orthographic_size is the full visible world HEIGHT; cover both the
                        // vertical (height) and horizontal (height*aspect) screen axes.
                        float orthoSize = std::max(std::max(span, span / aspect) * padding, 1.0f);

                        cam->SetProjectionMode(CameraProjectionMode::Orthographic);
                        cam->SetOrthographicSize(orthoSize);
                        cam->SetPosition(vec3(c.x, d.worldBounds.max.y + std::max(10.0f, sz.y) + 10.0f, c.z));
                        cam->SetNearPlane(0.05f);
                        cam->SetFarPlane(sz.y + 2000.0f);
                        ApplyCameraOrientationFromDirection(cam, vec3(0.0f, -1.0f, 0.0f));
                        cam->Update();

                        gs.draw_aabbs = drawBoxes;    // off by default → clean image
                        if (drawBoxes)
                            gs.aabbs_depth_aware = false; // unoccluded, full footprint
                        sc.MarkDirty();
                        shot->ok = true;
                    }
                }
            }
            {
                std::lock_guard lock(shot->mtx);
                shot->done = true;
            }
            shot->cv.notify_one(); });

        {
            std::unique_lock lock(shot->mtx);
            if (!shot->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                   { return shot->done; }))
                return R"({"error":"timeout framing camera"})";
        }
        if (!shot->ok)
            return JsonObj({{"error", JsonStr(shot->error)}});

        // Let frames render with the new camera/settings (TAA settles on a static shot).
        // The render loop runs on the main thread; this sleep does not block it.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        // --- Phase B: capture the clean scene RT (reuses the readback path) ----------
        // 'viewport' is the scene color BEFORE the editor composites ImGui (unlike
        // 'display', which in-editor contains the whole UI). Grid is a 'display'-only
        // pass, so it is absent here — coordinates come from the manifest instead.
        nlohmann::json capArgs;
        capArgs["id"] = "rt:viewport";
        capArgs["format"] = "png";
        capArgs["max_dimension"] = maxDim;
        auto cap = nlohmann::json::parse(CaptureImageResource(capArgs.dump()), nullptr, false);

        // --- Phase C: project nodes → pixels, write manifest, restore camera/settings -
        auto out = std::make_shared<std::string>();
        auto outDone = std::make_shared<bool>(false);
        auto outMtx = std::make_shared<std::mutex>();
        auto outCv = std::make_shared<std::condition_variable>();

        QueueAction([shot, cap, out, outDone, outMtx, outCv]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            nlohmann::json result;
            if (!r) { result["error"] = "renderer not available"; }
            else
            {
                Scene &sc = r->GetScene();
                Camera *cam = sc.GetCameras().empty() ? nullptr : sc.GetActiveCamera();

                if (cam && !cap.contains("error"))
                {
                    const mat4 vp = cam->GetViewProjection();
                    // Project to the display RT's native size, then scale into the
                    // (possibly downscaled) encoded PNG's pixel space.
                    const float origW = static_cast<float>(cap.value("original_width", cap.value("width", 1)));
                    const float origH = static_cast<float>(cap.value("original_height", cap.value("height", 1)));
                    const float encW = static_cast<float>(cap.value("width", 1));
                    const float encH = static_cast<float>(cap.value("height", 1));
                    const float sx = origW > 0.0f ? encW / origW : 1.0f;
                    const float sy = origH > 0.0f ? encH / origH : 1.0f;

                    auto toPx = [&](const vec3 &world) -> vec2 {
                        vec4 clip = vp * vec4(world, 1.0f);
                        float w = (std::abs(clip.w) < 1e-6f) ? 1.0f : clip.w;
                        vec3 ndc = vec3(clip) / w;
                        float px = (ndc.x * 0.5f + 0.5f) * origW;
                        float py = (1.0f - (ndc.y * 0.5f + 0.5f)) * origH;
                        return vec2(px * sx, py * sy);
                    };

                    nlohmann::json nodes = nlohmann::json::array();
                    for (const SceneDigestNode &n : shot->digest.nodes)
                    {
                        if (!n.enabled || !n.visible || n.groundOutlier) // only what is actually in the framed shot
                            continue;
                        const AABB &bb = n.aabb;
                        float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
                        for (int ci = 0; ci < 8; ++ci)
                        {
                            vec3 corner((ci & 1) ? bb.max.x : bb.min.x,
                                        (ci & 2) ? bb.max.y : bb.min.y,
                                        (ci & 4) ? bb.max.z : bb.min.z);
                            vec2 p = toPx(corner);
                            minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                            maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
                        }
                        vec2 cpx = toPx(bb.GetCenter());

                        nlohmann::json jn;
                        jn["id"] = n.id;
                        jn["name"] = n.name;
                        jn["in_frustum"] = n.inFrustum;
                        jn["center_world"] = Vec3Json(bb.GetCenter());
                        jn["center_px"] = nlohmann::json::array({cpx.x, cpx.y});
                        jn["aabb_px"] = nlohmann::json::array({minx, miny, maxx, maxy});
                        nodes.push_back(std::move(jn));
                    }

                    nlohmann::json manifest;
                    manifest["image"] = {{"path", cap.value("path", "")},
                                         {"width", cap.value("width", 0)},
                                         {"height", cap.value("height", 0)}};
                    manifest["pixel_mapping"] = {
                        {"origin", "top_left"},
                        {"note", "px=(ndc.x*0.5+0.5)*W; py=(1-(ndc.y*0.5+0.5))*H; ndc=view_projection*vec4(world,1)/w (view_projection is row-major json)"}};
                    manifest["view_projection"] = Mat4Json(vp);
                    manifest["camera"] = {{"projection", "orthographic"},
                                          {"position", Vec3Json(cam->GetPosition())},
                                          {"orthographic_size", cam->GetOrthographicSize()},
                                          {"aspect", cam->GetAspect()},
                                          {"direction", Vec3Json(cam->GetFront())}};
                    manifest["world_bounds"] = {{"min", Vec3Json(shot->digest.worldBounds.min)},
                                                {"max", Vec3Json(shot->digest.worldBounds.max)},
                                                {"center", Vec3Json(shot->digest.worldBounds.GetCenter())},
                                                {"size", Vec3Json(shot->digest.worldBounds.GetSize())}};
                    manifest["ground_y"] = shot->digest.groundY;
                    manifest["nodes"] = std::move(nodes);

                    std::string pngPath = cap.value("path", "");
                    std::string manifestPath = pngPath;
                    size_t dot = manifestPath.find_last_of('.');
                    if (dot != std::string::npos) manifestPath = manifestPath.substr(0, dot);
                    manifestPath += ".map.json";

                    bool wrote = false;
                    {
                        std::ofstream f(manifestPath, std::ios::binary);
                        if (f) { f << manifest.dump(2); wrote = f.good(); }
                    }

                    result["status"] = "ok";
                    result["image"] = manifest["image"];
                    result["manifest_path"] = wrote ? manifestPath : "";
                    result["node_count"] = manifest["nodes"].size();
                    result["world_bounds"] = manifest["world_bounds"];
                    result["ground_y"] = shot->digest.groundY;
                    if (!wrote)
                    {
                        result["warning"] = "manifest file write failed; manifest returned inline";
                        result["manifest"] = manifest;
                    }
                }
                else
                {
                    result["error"] = cap.contains("error") ? cap["error"] : nlohmann::json("display RT capture failed");
                }

                // Restore camera + settings whether or not the capture succeeded.
                if (cam)
                {
                    cam->SetProjectionMode(shot->wasOrtho ? CameraProjectionMode::Orthographic : CameraProjectionMode::Perspective);
                    cam->SetOrthographicSize(shot->orthoSize);
                    cam->SetPosition(shot->camPos);
                    cam->SetEuler(shot->camEuler);
                    cam->SetNearPlane(shot->nearP);
                    cam->SetFarPlane(shot->farP);
                    cam->Update();
                    auto &gs = Settings::Get<GlobalSettings>();
                    gs.draw_aabbs = shot->drawAabbs;
                    gs.aabbs_depth_aware = shot->aabbsDepthAware;
                    sc.MarkDirty();
                }
            }
            *out = result.dump();
            {
                std::lock_guard lock(*outMtx);
                *outDone = true;
            }
            outCv->notify_one(); });

        {
            std::unique_lock lock(*outMtx);
            if (!outCv->wait_for(lock, std::chrono::seconds(15), [&]
                                 { return *outDone; }))
                return R"({"error":"timeout building manifest"})";
        }
        return *out;
    }

    std::string EditorToolRuntime::GetNodeInfo(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args, nodeId]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else
                {
                    sc.UpdateNodeMatrices();
                    const auto &cache = sc.GetNodeCache(node);
                    const uint32_t flags = sc.GetComponentFlags(node);
                    nlohmann::json result;
                    result["id"] = MakeNodeId(node);
                    result["name"] = cache.name->name;
                    result["index"] = node->index;
                    result["revision"] = node->revision;
                    result["components"] = ComponentFlagsJson(flags);
                    result["component_flags"] = flags;

                    if (NodeId *parent = sc.GetParent(node))
                        result["parent"] = {{"id", MakeNodeId(parent)}, {"name", sc.GetNodeName(parent)}};
                    else
                        result["parent"] = nullptr;

                    nlohmann::json children = nlohmann::json::array();
                    for (NodeId *child : sc.GetChildren(node))
                        children.push_back({{"id", MakeNodeId(child)}, {"name", sc.GetNodeName(child)}});
                    result["children"] = std::move(children);

                    const mat4 &local = sc.GetLocalMatrix(node);
                    const mat4 &world = sc.GetWorldMatrix(node);
                    result["local_matrix"] = Mat4Json(local);
                    result["world_matrix"] = Mat4Json(world);
                    result["local_position"] = Vec3Json(vec3(local[3]));
                    result["world_position"] = Vec3Json(vec3(world[3]));
                    result["world_aabb"] = AabbJson(sc.GetWorldAABB(node));
                    const NodeRuntime &runtime = sc.GetNodeRuntime(node);
                    result["runtime"] = {
                        {"data_offset", runtime.dataOffset},
                        {"dirty", runtime.dirty},
                        {"gpu_pending", runtime.gpuPending},
                        {"has_uniform_data", runtime.hasUniformData},
                    };

                    if (flags & Component_Camera)
                    {
                        if (Camera *camera = sc.GetCameraForNode(node))
                        {
                            result["camera"] = {
                                {"name", camera->GetName()},
                                {"active", sc.GetActiveCamera() == camera},
                                {"projection", camera->IsOrthographic() ? "orthographic" : "perspective"},
                                {"position", Vec3Json(camera->GetPosition())},
                                {"rotation_euler_deg", Vec3Json(glm::degrees(camera->GetEuler()))},
                                {"front", Vec3Json(camera->GetFront())},
                                {"up", Vec3Json(camera->GetUp())},
                                {"right", Vec3Json(camera->GetRight())},
                                {"fov_y_deg", glm::degrees(camera->Fovy())},
                                {"fov_x_deg", glm::degrees(camera->Fovx())},
                                {"orthographic_size", camera->GetOrthographicSize()},
                                {"near_plane", camera->GetNearPlane()},
                                {"far_plane", camera->GetFarPlane()},
                            };
                        }
                    }

                    if (flags & Component_Light)
                    {
                        auto [type, index] = sc.GetLightForNode(node);
                        if (index >= 0)
                            result["light"] = {{"type", LightTypeName(type)}, {"index", index}};
                    }

                    if (flags & Component_Script)
                        result["script"] = {{"path", sc.GetNodeScriptPath(node)}};

                    if (flags & Component_Sprite)
                    {
                        if (const NodeSpriteComponent *sprite = sc.GetSpriteComponent(node))
                            result["sprite"] = SpriteComponentJson(*sprite);
                    }

                    nlohmann::json meshes = nlohmann::json::array();
                    const auto &refs = sc.GetMeshRefs(node);
                    for (int slot = 0; slot < static_cast<int>(refs.size()); ++slot)
                    {
                        const int meshIndex = refs[slot];
                        if (!sc.IsValidMeshIndex(meshIndex))
                        {
                            meshes.push_back({{"slot", slot}, {"mesh_index", meshIndex}, {"valid", false}});
                            continue;
                        }

                        Mesh &mesh = sc.GetMesh(meshIndex);
                        const MeshRuntime &meshRuntime = sc.GetMeshRuntime(meshIndex);
                        nlohmann::json meshJson;
                        meshJson["slot"] = slot;
                        meshJson["mesh_index"] = meshIndex;
                        meshJson["valid"] = true;
                        meshJson["vertex_offset"] = mesh.vertexOffset;
                        meshJson["vertex_count"] = mesh.vertexCount;
                        meshJson["index_offset"] = mesh.indexOffset;
                        meshJson["index_count"] = mesh.indexCount;
                        meshJson["positions_offset"] = mesh.positionsOffset;
                        meshJson["skinned"] = mesh.skinned;
                        meshJson["live"] = mesh.live;
                        meshJson["render_type"] = RenderTypeName(mesh.renderType);
                        meshJson["local_aabb"] = AabbJson(mesh.boundingBox);
                        meshJson["material_gpu_index"] = meshRuntime.materialGpuIndex;

                        if (mesh.material)
                        {
                            MaterialInstance *inst = mesh.materialInstance;
                            nlohmann::json material;
                            material["name"] = mesh.material->name;
                            material["has_instance"] = inst != nullptr;
                            material["render_type"] = RenderTypeName(inst ? inst->GetRenderType() : mesh.material->renderType);
                            material["base_color"] = Vec4Json(inst ? inst->GetBaseColorFactor() : mesh.material->baseColorFactor);
                            material["metallic"] = inst ? inst->GetMetallic() : mesh.material->metallic;
                            material["roughness"] = inst ? inst->GetRoughness() : mesh.material->roughness;
                            material["alpha_cutoff"] = inst ? inst->GetAlphaCutoff() : mesh.material->alphaCutoff;
                            material["transmission"] = inst ? inst->GetTransmissionFactor() : mesh.material->transmissionFactor;
                            material["texture_mask"] = inst ? inst->GetTextureMask() : mesh.material->textureMask;

                            nlohmann::json textures = nlohmann::json::array();
                            for (int i = 0; i < 5; ++i)
                            {
                                Image *texture = inst ? inst->GetTexture(i) : mesh.material->textures[i].get();
                                nlohmann::json tex;
                                tex["slot"] = TextureSlotName(i);
                                tex["image_view_index"] = meshRuntime.imageViewIndices[i];
                                if (texture)
                                {
                                    tex["id"] = ImageId(texture);
                                    tex["name"] = texture->GetName();
                                    tex["format"] = PeFormatName(texture->GetFormat());
                                    tex["width"] = texture->GetWidth();
                                    tex["height"] = texture->GetHeight();
                                }
                                else
                                {
                                    tex["id"] = nullptr;
                                }
                                textures.push_back(std::move(tex));
                            }
                            material["textures"] = std::move(textures);
                            meshJson["material"] = std::move(material);
                        }
                        meshes.push_back(std::move(meshJson));
                    }
                    result["meshes"] = std::move(meshes);
                    state->result = result.dump();
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::GetRendererStatus() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            const auto &settings = Settings::Get<GlobalSettings>();
            nlohmann::json result;
            result["api"] = PeGraphicsApiName(RHII.GetApi());
            result["gpu_name"] = RHII.GetGpuName();
            result["render_scale"] = settings.render_scale;
            result["swapchain_width"] = RHII.GetWidth();
            result["swapchain_height"] = RHII.GetHeight();
            result["present_mode"] = RHII.GetSurface() ? RHII.PresentModeToString(RHII.GetSurface()->GetPresentMode()) : "unknown";
            result["preferred_present_mode"] = RHII.PresentModeToString(settings.preferred_present_mode);
            const double dt = FrameTimer::Instance().GetDelta();
            result["fps"] = dt > 0.0 ? 1.0 / dt : 0.0;
            result["delta_ms"] = dt * 1000.0;
            result["taa"] = settings.taa;
            result["fxaa"] = settings.fxaa;
            result["ssao"] = settings.ssao;
            result["forward_plus"] = settings.forward_plus;
            result["ray_tracing_supported"] = settings.ray_tracing_support;

            if (r)
            {
                if (Image *display = r->GetDisplayRT())
                    result["display"] = {{"width", display->GetWidth()}, {"height", display->GetHeight()}, {"format", PeFormatName(display->GetFormat())}};
                if (Image *viewport = r->GetViewportRT())
                    result["viewport"] = {{"width", viewport->GetWidth()}, {"height", viewport->GetHeight()}, {"format", PeFormatName(viewport->GetFormat())}};
            }

            const auto memory = RHII.GetGpuMemorySnapshot();
            result["gpu_memory"] = {
                {"vram", {{"used", memory.vram.used}, {"budget", memory.vram.budget}, {"size", memory.vram.size}, {"app", memory.vram.app}}},
                {"host", {{"used", memory.host.used}, {"budget", memory.host.budget}, {"size", memory.host.size}, {"app", memory.host.app}}},
            };

            state->result = result.dump();
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetCamera(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        auto state = std::make_shared<State>();

        QueueAction([state, args]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = r->GetScene();
                Camera *camera = nullptr;
                if (args.contains("node") && args["node"].is_string())
                {
                    std::string err;
                    NodeId *node = ResolveNode(sc, args["node"].get<std::string>(), err);
                    if (!node)
                    {
                        state->result = JsonObj({{"error", JsonStr(err)}});
                    }
                    else
                    {
                        camera = sc.GetCameraForNode(node);
                        if (!camera)
                            state->result = R"({"error":"node is not a camera"})";
                    }
                }
                else if (!sc.GetCameras().empty())
                {
                    camera = sc.GetActiveCamera();
                }

                if (camera && state->result.empty())
                {
                    vec3 position = camera->GetPosition();
                    if (args.contains("position"))
                    {
                        if (!ReadVec3Json(args["position"], position))
                            state->result = R"({"error":"position must be [x,y,z]"})";
                        else
                            camera->SetPosition(position);
                    }

                    if (state->result.empty() && args.contains("rotation_euler_deg"))
                    {
                        vec3 rotationDeg;
                        if (!ReadVec3Json(args["rotation_euler_deg"], rotationDeg))
                            state->result = R"({"error":"rotation_euler_deg must be [x,y,z]"})";
                        else
                            camera->SetEuler(glm::radians(rotationDeg));
                    }

                    if (state->result.empty() && args.contains("target"))
                    {
                        vec3 target;
                        if (!ReadVec3Json(args["target"], target))
                            state->result = R"({"error":"target must be [x,y,z]"})";
                        else
                            ApplyCameraOrientationFromDirection(camera, target - camera->GetPosition());
                    }
                    else if (state->result.empty() && args.contains("direction"))
                    {
                        vec3 direction;
                        if (!ReadVec3Json(args["direction"], direction))
                            state->result = R"({"error":"direction must be [x,y,z]"})";
                        else
                            ApplyCameraOrientationFromDirection(camera, direction);
                    }

                    if (state->result.empty() && args.contains("projection") && args["projection"].is_string())
                    {
                        const std::string mode = ToLower(args["projection"].get<std::string>());
                        if (mode == "orthographic" || mode == "ortho")
                            camera->SetProjectionMode(CameraProjectionMode::Orthographic);
                        else if (mode == "perspective")
                            camera->SetProjectionMode(CameraProjectionMode::Perspective);
                        else
                            state->result = R"({"error":"projection must be perspective or orthographic"})";
                    }

                    if (state->result.empty() && args.contains("fov_y_deg") && args["fov_y_deg"].is_number())
                        camera->SetFovx(camera->FovyToFovx(glm::radians(args["fov_y_deg"].get<float>())));
                    if (state->result.empty() && args.contains("fov_x_deg") && args["fov_x_deg"].is_number())
                        camera->SetFovx(glm::radians(args["fov_x_deg"].get<float>()));
                    if (state->result.empty() && args.contains("orthographic_size") && args["orthographic_size"].is_number())
                        camera->SetOrthographicSize(args["orthographic_size"].get<float>());
                    if (state->result.empty() && args.contains("near_plane") && args["near_plane"].is_number())
                        camera->SetNearPlane(std::max(0.0001f, args["near_plane"].get<float>()));
                    if (state->result.empty() && args.contains("far_plane") && args["far_plane"].is_number())
                        camera->SetFarPlane(std::max(camera->GetNearPlane() + 0.001f, args["far_plane"].get<float>()));

                    if (state->result.empty())
                    {
                        if (args.value("make_active", false))
                            sc.SetActiveCamera(camera);
                        camera->Update();
                        sc.MarkDirty();
                        nlohmann::json result;
                        result["status"] = "ok";
                        result["camera"] = camera->GetName();
                        if (NodeId *node = camera->GetNodeId())
                            result["node_id"] = MakeNodeId(node);
                        result["position"] = Vec3Json(camera->GetPosition());
                        result["rotation_euler_deg"] = Vec3Json(glm::degrees(camera->GetEuler()));
                        result["front"] = Vec3Json(camera->GetFront());
                        result["projection"] = camera->IsOrthographic() ? "orthographic" : "perspective";
                        result["fov_y_deg"] = glm::degrees(camera->Fovy());
                        result["orthographic_size"] = camera->GetOrthographicSize();
                        state->result = result.dump();
                    }
                }
                else if (state->result.empty())
                {
                    state->result = R"({"error":"no camera available"})";
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::FrameNode(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        if (!args.contains("node") || !args["node"].is_string())
            return R"({"error":"node required"})";
        auto state = std::make_shared<State>();

        QueueAction([state, args]()
                    {
            auto *r = GetGlobalSystem<RendererSystem>();
            if (!r)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = r->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, args["node"].get<std::string>(), err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else if (sc.GetCameras().empty() || !sc.GetActiveCamera())
                {
                    state->result = R"({"error":"no active camera"})";
                }
                else
                {
                    Camera *camera = sc.GetActiveCamera();
                    sc.UpdateNodeMatrices();
                    const AABB &bounds = sc.GetWorldAABB(node);
                    vec3 center = (bounds.min + bounds.max) * 0.5f;
                    vec3 extent = bounds.max - bounds.min;
                    if (!IsFiniteVec3(center) || !IsFiniteVec3(extent))
                    {
                        center = vec3(0.0f);
                        extent = vec3(1.0f);
                    }
                    float radius = glm::length(extent) * 0.5f;
                    if (!std::isfinite(radius) || radius < 0.001f)
                        radius = 1.0f;

                    vec3 direction = -camera->GetFront();
                    if (args.contains("direction"))
                    {
                        vec3 requested;
                        if (!ReadVec3Json(args["direction"], requested) || glm::length(requested) < 1e-6f)
                        {
                            state->result = R"({"error":"direction must be a nonzero [x,y,z]"})";
                        }
                        else
                        {
                            direction = glm::normalize(requested);
                        }
                    }
                    if (state->result.empty())
                    {
                        const float padding = args.value("padding", 1.25f);
                        if (camera->IsOrthographic())
                        {
                            if (args.value("adjust_orthographic_size", true))
                                camera->SetOrthographicSize(std::max(radius * 2.0f * padding, 0.001f));
                            camera->SetPosition(center + direction * std::max(radius * padding, camera->GetNearPlane()));
                        }
                        else
                        {
                            const float fovy = std::max(camera->Fovy(), glm::radians(1.0f));
                            const float dist = (radius / std::tan(fovy * 0.5f)) * padding;
                            camera->SetPosition(center + direction * std::max(dist, camera->GetNearPlane() + radius));
                        }
                        ApplyCameraOrientationFromDirection(camera, center - camera->GetPosition());
                        camera->Update();
                        sc.MarkDirty();

                        nlohmann::json result;
                        result["status"] = "ok";
                        result["node"] = MakeNodeId(node);
                        result["camera"] = camera->GetName();
                        result["center"] = Vec3Json(center);
                        result["radius"] = radius;
                        result["position"] = Vec3Json(camera->GetPosition());
                        result["rotation_euler_deg"] = Vec3Json(glm::degrees(camera->GetEuler()));
                        result["orthographic_size"] = camera->GetOrthographicSize();
                        state->result = result.dump();
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::SetNodeTexture(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string nodeId = args.value("node", "");
        if (nodeId.empty())
            return R"({"error":"node required"})";

        std::string slotName = args.value("slot", args.value("texture_slot", args.value("type", "")));
        TextureType textureSlot{};
        if (!TextureSlotFromName(slotName, textureSlot))
            return R"({"error":"slot must be base_color, metallic_roughness, normal, occlusion, or emissive"})";

        const bool clear = args.value("clear", false);
        const std::string path = args.value("path", "");
        if (!clear && path.empty())
            return R"({"error":"path required unless clear=true"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args, nodeId, textureSlot, clear, path]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                std::string err;
                NodeId *node = ResolveNode(sc, nodeId, err);
                if (!node)
                {
                    state->result = JsonObj({{"error", JsonStr(err)}});
                }
                else
                {
                    const int meshSlot = std::max(0, args.value("mesh_slot", args.value("slot_index", 0)));
                    const auto &refs = sc.GetMeshRefs(node);
                    if (meshSlot >= static_cast<int>(refs.size()))
                    {
                        state->result = R"({"error":"mesh_slot out of range"})";
                    }
                    else if (!sc.IsValidMeshIndex(refs[meshSlot]))
                    {
                        state->result = R"({"error":"mesh_slot does not reference a valid mesh"})";
                    }
                    else
                    {
                        Mesh &mesh = sc.GetMesh(refs[meshSlot]);
                        if (!mesh.material)
                        {
                            state->result = R"({"error":"mesh has no material"})";
                        }
                        else
                        {
                            MaterialInstance *inst = mesh.materialInstance;
                            if (!inst)
                                inst = sc.CreateMaterialInstance(mesh);

                            if (!inst)
                            {
                                state->result = R"({"error":"failed to create material instance"})";
                            }
                            else
                            {
                                ResourceHandle<Image> image;
                                if (clear)
                                {
                                    image = DefaultTextureForSlot(textureSlot);
                                    inst->SetTexture(textureSlot, image);
                                    inst->SetTextureMask(inst->GetTextureMask() & ~TextureBit(textureSlot));
                                }
                                else
                                {
                                    std::string loadError;
                                    image = LoadTextureForTool(path, loadError);
                                    if (!image)
                                    {
                                        state->result = JsonObj({{"error", JsonStr(loadError)}});
                                    }
                                    else
                                    {
                                        inst->SetTexture(textureSlot, image);
                                        inst->SetTextureMask(inst->GetTextureMask() | TextureBit(textureSlot));
                                    }
                                }

                                if (state->result.empty())
                                {
                                    sc.SetTexturesDirty();
                                    sc.SetMaterialDirty();
                                    sc.MarkNodeDirty(node);

                                    nlohmann::json result;
                                    result["status"] = "ok";
                                    result["node"] = MakeNodeId(node);
                                    result["mesh_slot"] = meshSlot;
                                    result["mesh_index"] = refs[meshSlot];
                                    result["slot"] = TextureSlotName(static_cast<int>(textureSlot));
                                    result["cleared"] = clear;
                                    result["texture_mask"] = inst->GetTextureMask();
                                    if (Image *raw = image.get())
                                    {
                                        result["image"] = {
                                            {"id", ImageId(raw)},
                                            {"name", raw->GetName()},
                                            {"format", PeFormatName(raw->GetFormat())},
                                            {"width", raw->GetWidth()},
                                            {"height", raw->GetHeight()},
                                            {"mips", raw->GetMipLevels()},
                                            {"array_layers", raw->GetArrayLayers()},
                                        };
                                    }
                                    state->result = result.dump();
                                }
                            }
                        }
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(20), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::TakeSceneScreenshot(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const int settleFrames = std::clamp(args.value("settle_frames", 2), 0, 120);
        if (settleFrames > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(16 * settleFrames));

        std::string target = ToLower(args.value("target", "display"));
        if (target == "scene" || target == "color")
            target = "display";
        if (target != "display" && target != "viewport")
            return R"({"error":"target must be display or viewport"})";

        nlohmann::json captureArgs;
        captureArgs["id"] = "rt:" + target;
        captureArgs["format"] = "png";
        captureArgs["max_width"] = args.value("max_width", args.value("max_dimension", 1024));
        captureArgs["return_base64"] = args.value("return_base64", false);
        captureArgs["visualize"] = args.value("visualize", "auto");

        auto result = nlohmann::json::parse(CaptureImageResource(captureArgs.dump()), nullptr, false);
        if (result.is_discarded())
            return R"({"error":"failed to capture scene screenshot"})";
        if (result.is_object() && !result.contains("error"))
        {
            result["tool"] = "take_scene_screenshot";
            result["target"] = target;
            result["settle_frames"] = settleFrames;
        }
        return result.dump();
    }

    std::string EditorToolRuntime::ListImageResources(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args]()
                    {
            const std::string scope = ToLower(args.value("scope", "all"));
            const bool includeRuntime = scope == "all" || scope == "runtime" || scope == "images" || scope == "textures" || scope == "render_targets";
            const bool includeFiles = scope == "all" || scope == "files" || scope == "assets";
            const int maxFiles = std::max(0, args.value("max_files", 200));
            nlohmann::json resources = nlohmann::json::array();

            if (includeRuntime)
            {
                std::unordered_set<Image *> seen;
                for (Image *image : Image::GetHandles())
                {
                    if (!image || seen.count(image))
                        continue;
                    seen.insert(image);

                    const PeImageUsageFlags usage = image->GetUsage();
                    const bool depth = PeFormatHasDepthOrStencil(image->GetFormat());
                    std::string kind = "texture";
                    if (depth)
                        kind = "depth_target";
                    else if (usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
                        kind = "render_target";
                    else if (usage & PE_IMAGE_USAGE_STORAGE)
                        kind = "storage_image";

                    nlohmann::json entry;
                    entry["id"] = ImageId(image);
                    entry["name"] = image->GetName();
                    entry["kind"] = kind;
                    entry["format"] = PeFormatName(image->GetFormat());
                    entry["width"] = image->GetWidth();
                    entry["height"] = image->GetHeight();
                    entry["mips"] = image->GetMipLevels();
                    entry["array_layers"] = image->GetArrayLayers();
                    entry["samples"] = static_cast<uint32_t>(image->GetSamples());
                    entry["usage"] = static_cast<uint32_t>(usage);
                    entry["readback"] = FormatBytesPerPixel(image->GetFormat()) != 0 &&
                                        image->GetSamples() == PE_SAMPLE_COUNT_1 &&
                                        !(depth && RHII.GetApi() == PE_GRAPHICS_API_DX12) &&
                                        !(RHII.GetApi() == PE_GRAPHICS_API_VULKAN && !(usage & PE_IMAGE_USAGE_TRANSFER_SRC));

                    nlohmann::json aliases = nlohmann::json::array();
                    if (!image->GetName().empty())
                    {
                        if (depth)
                            aliases.push_back("depth:" + image->GetName());
                        else if (usage & PE_IMAGE_USAGE_COLOR_ATTACHMENT)
                            aliases.push_back("rt:" + image->GetName());
                        aliases.push_back("image:" + image->GetName());
                    }
                    if (!aliases.empty())
                        entry["aliases"] = std::move(aliases);
                    resources.push_back(std::move(entry));
                }
            }

            int fileCount = 0;
            if (includeFiles && maxFiles > 0)
            {
                const std::filesystem::path root(Path::Assets);
                std::error_code itEc;
                for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, itEc);
                     it != std::filesystem::recursive_directory_iterator() && fileCount < maxFiles; it.increment(itEc))
                {
                    if (itEc)
                    {
                        itEc.clear();
                        continue;
                    }
                    std::error_code ec;
                    if (!it->is_regular_file(ec) || ec || !IsImageFileExt(it->path().extension().string()))
                        continue;

                    std::string rel = std::filesystem::relative(it->path(), root, ec).string();
                    if (ec)
                        continue;
                    std::replace(rel.begin(), rel.end(), '\\', '/');

                    int width = 0, height = 0, channels = 0;
                    stbi_info(it->path().string().c_str(), &width, &height, &channels);
                    nlohmann::json entry;
                    entry["id"] = "file:" + rel;
                    entry["name"] = it->path().filename().string();
                    entry["kind"] = "asset_file";
                    entry["path"] = rel;
                    entry["width"] = width;
                    entry["height"] = height;
                    entry["channels"] = channels;
                    entry["readback"] = true;
                    resources.push_back(std::move(entry));
                    ++fileCount;
                }
            }

            state->result = nlohmann::json{{"count", resources.size()}, {"resources", std::move(resources)}}.dump();
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::CaptureImageResource(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        const std::string id = args.value("id", "");
        if (id.empty())
            return R"({"error":"id required"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args, id]()
                    {
            const int mip = args.value("mip", 0);
            const int arrayIndex = args.value("array_index", args.value("layer", 0));
            const std::string outputFormat = ToLower(args.value("format", "png"));
            const std::string aspect = ToLower(args.value("aspect", "auto"));
            const bool returnBase64 = args.value("return_base64", false);
            int maxDim = args.value("max_width", args.value("max_dimension", 1024));
            if (maxDim <= 0)
                maxDim = std::numeric_limits<int>::max();

            if (outputFormat != "png")
            {
                state->result = R"({"error":"only png output is supported"})";
            }
            else if (mip < 0 || arrayIndex < 0)
            {
                state->result = R"({"error":"mip and array_index must be non-negative"})";
            }
            else if (id.rfind("file:", 0) == 0)
            {
                const std::string rel = id.substr(5);
                std::filesystem::path path = ResolveAssetPath(rel);
                if (mip != 0 || arrayIndex != 0)
                {
                    state->result = R"({"error":"file image capture does not support mip or array_index"})";
                }
                else if (!pmcp::IsPathSafe(path.string(), Path::Assets))
                {
                    state->result = R"({"error":"file path outside Assets"})";
                }
                else
                {
                    int width = 0, height = 0, channels = 0;
                    uint8_t *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
                    if (!pixels)
                    {
                        state->result = R"({"error":"failed to decode image file"})";
                    }
                    else
                    {
                        int outW = width;
                        int outH = height;
                        std::vector<uint8_t> resized;
                        const uint8_t *src = pixels;
                        if (std::max(width, height) > maxDim)
                        {
                            resized = ResizeRGBA(pixels, width, height, outW, outH, maxDim);
                            src = resized.data();
                        }
                        std::vector<uint8_t> png = pmcp::EncodeRGBA_PNG(src, outW, outH);
                        stbi_image_free(pixels);

                        if (png.empty())
                        {
                            state->result = R"({"error":"failed to encode PNG"})";
                        }
                        else
                        {
                            const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count();
                            const std::string outDir = Path::Assets + "Agent/";
                            std::filesystem::create_directories(outDir);
                            const std::string outPath = outDir + "image_file_" + std::to_string(ts) + ".png";
                            std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                            out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
                            out.close();

                            nlohmann::json result;
                            result["id"] = id;
                            result["source"] = "file";
                            result["path"] = outPath;
                            result["mime_type"] = "image/png";
                            result["width"] = outW;
                            result["height"] = outH;
                            result["bytes"] = png.size();
                            result["original_width"] = width;
                            result["original_height"] = height;
                            if (returnBase64)
                                result["image_base64"] = pmcp::Base64Encode(png.data(), png.size());
                            state->result = result.dump();
                        }
                    }
                }
            }
            else
            {
                auto *renderer = GetGlobalSystem<RendererSystem>();
                Image *image = nullptr;
                if (renderer && id.rfind("rt:", 0) == 0)
                {
                    image = renderer->GetRenderTarget(id.substr(3));
                }
                else if (renderer && id.rfind("depth:", 0) == 0)
                {
                    image = renderer->GetDepthStencilTarget(id.substr(6));
                }
                else if (id.rfind("image:", 0) == 0)
                {
                    const std::string key = id.substr(6);
                    if (key.rfind("0x", 0) == 0)
                    {
                        uintptr_t address = 0;
                        std::istringstream is(key.substr(2));
                        is >> std::hex >> address;
                        for (Image *candidate : Image::GetHandles())
                        {
                            if (reinterpret_cast<uintptr_t>(candidate) == address)
                            {
                                image = candidate;
                                break;
                            }
                        }
                    }
                    else
                    {
                        int matches = 0;
                        for (Image *candidate : Image::GetHandles())
                        {
                            if (candidate && candidate->GetName() == key)
                            {
                                image = candidate;
                                ++matches;
                            }
                        }
                        if (matches > 1)
                            state->result = R"({"error":"image name is ambiguous; use image:0x... id from list_image_resources"})";
                    }
                }

                if (!image && state->result.empty())
                    state->result = R"({"error":"image resource not found"})";

                if (image && state->result.empty())
                {
                    const uint32_t requestedMip = static_cast<uint32_t>(mip);
                    const uint32_t requestedLayer = static_cast<uint32_t>(arrayIndex);
                    if (requestedMip >= image->GetMipLevels())
                    {
                        state->result = R"({"error":"mip out of range"})";
                    }
                    else if (requestedLayer >= image->GetArrayLayers())
                    {
                        state->result = R"({"error":"array_index out of range"})";
                    }
                    else if (image->GetSamples() != PE_SAMPLE_COUNT_1)
                    {
                        state->result = R"({"error":"multisampled image readback is not supported"})";
                    }
                    else if (aspect != "auto" && aspect != "color" && aspect != "depth" && aspect != "stencil")
                    {
                        state->result = R"({"error":"aspect must be auto, color, depth, or stencil"})";
                    }
                    else if (aspect == "stencil")
                    {
                        state->result = R"({"error":"stencil image capture is not supported yet"})";
                    }
                    else if (aspect == "depth" && !PeFormatHasDepth(image->GetFormat()))
                    {
                        state->result = R"({"error":"requested depth aspect for an image without depth"})";
                    }
                    else if (aspect == "color" && PeFormatHasDepthOrStencil(image->GetFormat()))
                    {
                        state->result = R"({"error":"requested color aspect for a depth/stencil image"})";
                    }
                    else if (PeFormatHasDepthOrStencil(image->GetFormat()) && RHII.GetApi() == PE_GRAPHICS_API_DX12)
                    {
                        state->result = R"({"error":"DX12 depth/stencil image readback is not supported by the current RHI copy path"})";
                    }
                    else if (PeFormatHasStencil(image->GetFormat()))
                    {
                        state->result = R"({"error":"stencil image capture is not supported yet"})";
                    }
                    else if (FormatBytesPerPixel(image->GetFormat()) == 0)
                    {
                        state->result = JsonObj({{"error", JsonStr(std::string("unsupported image format: ") + PeFormatName(image->GetFormat()))}});
                    }
                    else if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN && !(image->GetUsage() & PE_IMAGE_USAGE_TRANSFER_SRC))
                    {
                        state->result = R"({"error":"image is not transfer-src; cannot stage readback on Vulkan"})";
                    }
                    else
                    {
                        Queue *queue = RHII.GetMainQueue();
                        if (!queue)
                        {
                            state->result = R"({"error":"main queue not available"})";
                        }
                        else
                        {
                            if (renderer)
                                renderer->WaitAllFramesCommands();

                            const uint32_t width = MipExtent(image->GetWidth(), requestedMip);
                            const uint32_t height = MipExtent(image->GetHeight(), requestedMip);
	                            const size_t rowBytes = static_cast<size_t>(width) * FormatBytesPerPixel(image->GetFormat());
	                            const size_t rowPitch = RHII.GetApi() == PE_GRAPHICS_API_DX12 ? RHII.AlignTextureRowPitch(rowBytes) : rowBytes;
	                            const size_t bufferSize = rowPitch * height;
	                            constexpr size_t kDefaultImageReadbackCap = 64 * 1024 * 1024;
	                            constexpr size_t kLargeImageReadbackCap = 512 * 1024 * 1024;
	                            const bool allowLarge = args.value("allow_large", false);
	                            const size_t cap = allowLarge ? kLargeImageReadbackCap : kDefaultImageReadbackCap;
	                            if (bufferSize > cap)
	                            {
	                                state->result = nlohmann::json{
	                                                    {"error", "image readback exceeds staging allocation cap; pass allow_large=true"},
	                                                    {"readback_size", bufferSize},
	                                                    {"cap", cap},
	                                                }
	                                                    .dump();
	                            }
	                            else
	                            {
	                                Buffer *staging = Buffer::Create({
	                                    .size = bufferSize,
	                                    .usage = PE_BUFFER_USAGE_TRANSFER_DST,
	                                    .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
	                                    .name = "McpImageReadback",
	                                });

                            CommandBuffer *cmd = queue->AcquireCommandBuffer();
                            cmd->Begin();
                            cmd->CopyImageToBuffer(image, staging, requestedMip, requestedLayer, 1);
                            cmd->End();
                            queue->Submit(1, &cmd, nullptr, nullptr);
                            cmd->Wait();
                            queue->ReturnCommandBuffer(cmd);

                            staging->Map();
                            const uint8_t *src = static_cast<const uint8_t *>(staging->Data());
                            std::string visualize = ToLower(args.value("visualize", "auto"));
                            if (visualize == "auto")
                            {
                                const std::string name = ToLower(image->GetName());
                                if (PeFormatHasDepth(image->GetFormat()))
                                    visualize = "depth";
                                else if (name.find("normal") != std::string::npos)
                                    visualize = "normal";
                                else if (name.find("velocity") != std::string::npos)
                                    visualize = "velocity";
                                else
                                    visualize = "linear";
                            }

                            std::vector<uint8_t> rgba;
                            std::string error;
                            const bool converted = ConvertImageRowsToRGBA(src, width, height, rowPitch, image->GetFormat(), visualize, rgba, error);
                            staging->Unmap();
                            Buffer::Destroy(staging);

                            if (!converted)
                            {
                                state->result = JsonObj({{"error", JsonStr(error)}});
                            }
                            else
                            {
                                int outW = static_cast<int>(width);
                                int outH = static_cast<int>(height);
                                std::vector<uint8_t> resized;
                                const uint8_t *pngSrc = rgba.data();
                                if (std::max(outW, outH) > maxDim)
                                {
                                    resized = ResizeRGBA(rgba.data(), outW, outH, outW, outH, maxDim);
                                    pngSrc = resized.data();
                                }
                                std::vector<uint8_t> png = pmcp::EncodeRGBA_PNG(pngSrc, outW, outH);
                                if (png.empty())
                                {
                                    state->result = R"({"error":"failed to encode PNG"})";
                                }
                                else
                                {
                                    const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now().time_since_epoch())
                                                        .count();
                                    const std::string outDir = Path::Assets + "Agent/";
                                    std::filesystem::create_directories(outDir);
                                    std::string safeName = image->GetName().empty() ? "image" : image->GetName();
                                    for (char &c : safeName)
                                    {
                                        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
                                            c = '_';
                                    }
                                    const std::string outPath = outDir + "image_" + safeName + "_mip" + std::to_string(requestedMip) +
                                                                "_layer" + std::to_string(requestedLayer) + "_" + std::to_string(ts) + ".png";
                                    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                                    out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
                                    out.close();

                                    nlohmann::json result;
                                    result["id"] = id;
                                    result["source"] = "gpu_image";
                                    result["name"] = image->GetName();
                                    result["path"] = outPath;
                                    result["mime_type"] = "image/png";
                                    result["format"] = PeFormatName(image->GetFormat());
                                    result["visualize"] = visualize;
                                    result["mip"] = requestedMip;
                                    result["array_index"] = requestedLayer;
                                    result["width"] = outW;
                                    result["height"] = outH;
                                    result["original_width"] = width;
                                    result["original_height"] = height;
	                                    result["readback_size"] = bufferSize;
	                                    result["bytes"] = png.size();
	                                    if (returnBase64)
	                                        result["image_base64"] = pmcp::Base64Encode(png.data(), png.size());
	                                    state->result = result.dump();
	                                }
	                            }
	                            }
	                        }
	                    }
                }
            }

            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(30), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::ListBufferResources(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args]()
                    {
            const int maxResults = std::clamp(args.value("max_results", 500), 1, 5000);
            const bool includeEmpty = args.value("include_empty", false);
            nlohmann::json resources = nlohmann::json::array();
            std::unordered_set<Buffer *> seen;

            for (Buffer *buffer : Buffer::GetHandles())
            {
                if (!buffer || seen.count(buffer))
                    continue;
                seen.insert(buffer);
                if (!includeEmpty && buffer->Size() == 0)
                    continue;
                if (static_cast<int>(resources.size()) >= maxResults)
                    break;

                nlohmann::json entry;
                entry["id"] = BufferId(buffer);
                entry["name"] = buffer->GetName();
                entry["size"] = buffer->Size();
                entry["usage_flags"] = buffer->Usage();
                entry["usage"] = BufferUsageJson(buffer->Usage());
                entry["memory_usage"] = MemoryUsageName(buffer->MemoryUsage());
                const bool hostReadable = IsHostReadableMemory(buffer->MemoryUsage());
                const bool canStageReadback = !hostReadable && (buffer->Usage() & PE_BUFFER_USAGE_TRANSFER_SRC);
                entry["host_readable"] = hostReadable;
                entry["staged_readback"] = canStageReadback;
                entry["readback"] = buffer->Size() > 0 && (hostReadable || canStageReadback);
                if (!buffer->GetName().empty())
                    entry["aliases"] = nlohmann::json::array({"buffer:" + buffer->GetName()});
                resources.push_back(std::move(entry));
            }

            state->result = nlohmann::json{{"count", resources.size()}, {"resources", std::move(resources)}}.dump();
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(10), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::CaptureBufferResource(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        const std::string id = args.value("id", "");
        if (id.empty())
            return R"({"error":"id required"})";

        auto state = std::make_shared<State>();
        QueueAction([state, args, id]()
                    {
            Buffer *buffer = nullptr;
            if (id.rfind("buffer:", 0) == 0)
            {
                const std::string key = id.substr(7);
                if (key.rfind("0x", 0) == 0)
                {
                    uintptr_t address = 0;
                    std::istringstream is(key.substr(2));
                    is >> std::hex >> address;
                    for (Buffer *candidate : Buffer::GetHandles())
                    {
                        if (reinterpret_cast<uintptr_t>(candidate) == address)
                        {
                            buffer = candidate;
                            break;
                        }
                    }
                }
                else
                {
                    int matches = 0;
                    for (Buffer *candidate : Buffer::GetHandles())
                    {
                        if (candidate && candidate->GetName() == key)
                        {
                            buffer = candidate;
                            ++matches;
                        }
                    }
                    if (matches > 1)
                        state->result = R"({"error":"buffer name is ambiguous; use buffer:0x... id from list_buffer_resources"})";
                }
            }

            if (!buffer && state->result.empty())
                state->result = R"({"error":"buffer resource not found"})";

            if (buffer && state->result.empty())
            {
                const size_t bufferSize = buffer->Size();
                size_t offset = static_cast<size_t>(std::max<int64_t>(0, args.value("offset", static_cast<int64_t>(0))));
                if (offset > bufferSize)
                {
                    state->result = R"({"error":"offset out of range"})";
                }
                else
                {
                    const size_t remaining = bufferSize - offset;
                    bool explicitSize = false;
                    size_t requestedSize = remaining;
                    if (args.contains("size") && args["size"].is_number_unsigned())
                    {
                        requestedSize = args["size"].get<size_t>();
                        explicitSize = true;
                    }
                    else if (args.contains("size") && args["size"].is_number_integer())
                    {
                        requestedSize = static_cast<size_t>(std::max<int64_t>(0, args["size"].get<int64_t>()));
                        explicitSize = true;
                    }

                    constexpr size_t kDefaultCap = 1 * 1024 * 1024;
                    constexpr size_t kLargeCap = 64 * 1024 * 1024;
                    const bool allowLarge = args.value("allow_large", false);
                    const size_t cap = allowLarge ? kLargeCap : kDefaultCap;
                    const size_t readSize = explicitSize ? std::min({requestedSize, remaining, cap}) : std::min(remaining, cap);
                    if (explicitSize && requestedSize > cap && !allowLarge)
                    {
                        state->result = nlohmann::json{
                            {"error", "requested buffer capture exceeds default cap; pass allow_large=true"},
                            {"requested_size", requestedSize},
                            {"cap", cap},
                        }.dump();
                    }
                    else
                    {
                        std::vector<uint8_t> bytes(readSize);
                        const bool hostReadable = IsHostReadableMemory(buffer->MemoryUsage());
                        if (readSize > 0 && hostReadable)
                        {
                            buffer->Map();
                            const uint8_t *src = static_cast<const uint8_t *>(buffer->Data());
                            if (src)
                                std::memcpy(bytes.data(), src + offset, readSize);
                            buffer->Unmap();
                        }
                        else if (readSize > 0)
                        {
                            if (!(buffer->Usage() & PE_BUFFER_USAGE_TRANSFER_SRC))
                            {
                                state->result = R"({"error":"buffer is not transfer-src; cannot stage readback"})";
                            }
                            else
                            {
                                Queue *queue = RHII.GetMainQueue();
                                if (!queue)
                                {
                                    state->result = R"({"error":"main queue not available"})";
                                }
                                else
                                {
                                    auto *renderer = GetGlobalSystem<RendererSystem>();
                                    if (renderer)
                                        renderer->WaitAllFramesCommands();

                                    Buffer *staging = Buffer::Create({
                                        .size = readSize,
                                        .usage = PE_BUFFER_USAGE_TRANSFER_DST,
                                        .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
                                        .name = "McpBufferReadback",
                                    });

                                    CommandBuffer *cmd = queue->AcquireCommandBuffer();
                                    cmd->Begin();
                                    cmd->CopyBuffer(buffer, staging, readSize, offset, 0);
                                    cmd->End();
                                    queue->Submit(1, &cmd, nullptr, nullptr);
                                    cmd->Wait();
                                    queue->ReturnCommandBuffer(cmd);

                                    staging->Map();
                                    const uint8_t *src = static_cast<const uint8_t *>(staging->Data());
                                    if (src)
                                        std::memcpy(bytes.data(), src, readSize);
                                    staging->Unmap();
                                    Buffer::Destroy(staging);
                                }
                            }
                        }

                        if (state->result.empty())
                        {
                            const auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count();
                            const std::string outDir = Path::Assets + "Agent/";
                            std::filesystem::create_directories(outDir);
                            const std::string safeName = SanitizeFileStem(buffer->GetName().empty() ? "buffer" : buffer->GetName());
                            const std::string outPath = outDir + "buffer_" + safeName + "_" + std::to_string(ts) + ".bin";
                            std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
                            if (readSize > 0)
                                out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                            out.close();

                            nlohmann::json result;
                            result["id"] = id;
                            result["name"] = buffer->GetName();
                            result["path"] = outPath;
                            result["mime_type"] = "application/octet-stream";
                            result["offset"] = offset;
                            result["size"] = readSize;
                            result["buffer_size"] = bufferSize;
                            result["memory_usage"] = MemoryUsageName(buffer->MemoryUsage());
                            result["staged_readback"] = !hostReadable && (buffer->Usage() & PE_BUFFER_USAGE_TRANSFER_SRC);
                            if (args.value("return_base64", false))
                                result["data_base64"] = pmcp::Base64Encode(bytes.data(), bytes.size());
                            if (args.value("return_hex", false))
                                result["data_hex"] = HexEncode(bytes.data(), bytes.size());
                            if (args.value("preview_u32", false))
                            {
                                nlohmann::json preview = nlohmann::json::array();
                                const size_t count = std::min(bytes.size() / sizeof(uint32_t), static_cast<size_t>(64));
                                for (size_t i = 0; i < count; ++i)
                                {
                                    uint32_t v = 0;
                                    std::memcpy(&v, bytes.data() + i * sizeof(uint32_t), sizeof(v));
                                    preview.push_back(v);
                                }
                                result["preview_u32"] = std::move(preview);
                            }
                            state->result = result.dump();
                        }
                    }
                }
            }

            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(30), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::ImportModel(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        std::vector<std::string> inputs;
        if (args.contains("paths") && args["paths"].is_array())
        {
            for (const auto &value : args["paths"])
                if (value.is_string())
                    inputs.push_back(value.get<std::string>());
        }
        if (inputs.empty() && args.contains("path") && args["path"].is_string())
            inputs.push_back(args["path"].get<std::string>());
        if (inputs.empty())
            return R"({"error":"path or paths required"})";

        const bool force = args.value("force", false);
        const std::string outputPathArg = args.value("output_path", "");
        if (!outputPathArg.empty() && inputs.size() != 1)
            return R"({"error":"output_path is only valid with one input path"})";

        auto job = std::make_shared<ImportJob>();
        job->id = "import:" + std::to_string(s_importJobCounter.fetch_add(1));
        job->started = std::chrono::steady_clock::now();

        nlohmann::json invalid = nlohmann::json::array();
        for (const std::string &input : inputs)
        {
            std::filesystem::path src = ResolveExistingAssetCandidate(input, "Objects");
            std::error_code ec;
            if (!std::filesystem::exists(src, ec) || std::filesystem::is_directory(src, ec))
            {
                invalid.push_back({{"path", input}, {"error", "source model not found"}});
                continue;
            }
            if (!pmcp::IsPathSafe(src.string(), Path::Assets))
            {
                invalid.push_back({{"path", input}, {"error", "source path outside Assets"}});
                continue;
            }

            const std::string ext = ToLower(src.extension().string());
            if (ext == ModelAssetCooked::kExtension)
            {
                job->skipped.push_back(src);
                continue;
            }
            if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx")
            {
                invalid.push_back({{"path", input}, {"error", "unsupported model extension; expected .glb, .gltf, .obj, .fbx, or .pemesh"}});
                continue;
            }

            std::filesystem::path outPemesh;
            if (!outputPathArg.empty())
            {
                outPemesh = U8Path(outputPathArg);
                if (outPemesh.is_relative())
                    outPemesh = U8Path(Path::Assets) / outPemesh;
            }
            else
            {
                const std::filesystem::path stem = src.stem();
                outPemesh = U8Path(Path::Assets) / "Models" / stem / stem;
                outPemesh += ModelAssetCooked::kExtension;
            }

            const std::string outExt = ToLower(outPemesh.extension().string());
            if (outExt.empty())
            {
                outPemesh += ModelAssetCooked::kExtension;
            }
            else if (outExt != ModelAssetCooked::kExtension)
            {
                invalid.push_back({{"path", input}, {"error", "output_path must end with .pemesh"}});
                continue;
            }

            if (!pmcp::IsPathSafe(outPemesh.string(), Path::Assets))
            {
                invalid.push_back({{"path", input}, {"error", "output path outside Assets"}});
                continue;
            }

            if (!force && CookTargetUpToDate(src, outPemesh))
            {
                job->skipped.push_back(outPemesh);
                continue;
            }
            job->jobs.emplace_back(std::move(src), std::move(outPemesh));
        }

        job->total = static_cast<uint32_t>(job->jobs.size() + job->skipped.size() + invalid.size());
        job->progress = static_cast<uint32_t>(job->skipped.size());
        if (!invalid.empty())
            job->failed.push_back(invalid.dump());

        if (job->jobs.empty())
        {
            job->status = job->failed.empty() ? "complete" : "failed";
            job->processOk = job->failed.empty();
            job->finished = std::chrono::steady_clock::now();
            std::lock_guard lock(s_importJobsMutex);
            s_importJobs[job->id] = job;
            PruneImportJobsLocked(job->id);
            return ImportJobJson(*job).dump();
        }

        {
            std::lock_guard lock(s_importJobsMutex);
            s_importJobs[job->id] = job;
            PruneImportJobsLocked(job->id);
        }

        std::thread([job]()
                    {
            {
                std::lock_guard lock(s_importJobsMutex);
                job->status = "running";
            }

            std::error_code ec;
            std::vector<bool> existedBefore(job->jobs.size(), false);
            std::vector<std::filesystem::file_time_type> beforeTime(job->jobs.size());
            for (size_t i = 0; i < job->jobs.size(); ++i)
            {
                std::filesystem::create_directories(job->jobs[i].second.parent_path(), ec);
                existedBefore[i] = std::filesystem::exists(job->jobs[i].second, ec);
                if (existedBefore[i])
                    beforeTime[i] = std::filesystem::last_write_time(job->jobs[i].second, ec);
            }

            static std::atomic<unsigned> s_manifestCounter{0};
            const std::filesystem::path manifest =
                std::filesystem::temp_directory_path(ec) /
                ("phasma_mcp_cook_" + std::to_string(s_manifestCounter.fetch_add(1)) + ".txt");
            {
                std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    std::lock_guard lock(s_importJobsMutex);
                    job->status = "failed";
                    job->error = "could not write cook manifest";
                    job->finished = std::chrono::steady_clock::now();
                    return;
                }
                for (const auto &pair : job->jobs)
                    out << PathUtf8(pair.first) << '\t' << PathUtf8(pair.second) << '\n';
            }

            const bool processOk = RunPhasmaCookProcess(manifest);
            std::filesystem::path failedPath = manifest;
            failedPath += ".failed";

            std::vector<std::filesystem::path> cooked;
            std::vector<std::string> failed;
            for (size_t i = 0; i < job->jobs.size(); ++i)
            {
                std::error_code fec;
                const bool exists = std::filesystem::exists(job->jobs[i].second, fec);
                bool freshlyCooked = exists && !existedBefore[i];
                if (exists && existedBefore[i])
                    freshlyCooked = std::filesystem::last_write_time(job->jobs[i].second, fec) != beforeTime[i];
                if (freshlyCooked)
                    cooked.push_back(job->jobs[i].second);
            }

            if (std::ifstream failedIn{failedPath, std::ios::binary})
            {
                std::string line;
                while (std::getline(failedIn, line))
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (!line.empty())
                        failed.push_back(line);
                }
            }

            for (const auto &pair : job->jobs)
            {
                const bool wasCooked = std::find(cooked.begin(), cooked.end(), pair.second) != cooked.end();
                if (!wasCooked)
                {
                    const std::string src = PathUtf8(pair.first);
                    const bool alreadyFailed = std::any_of(failed.begin(), failed.end(),
                                                           [&](const std::string &line)
                                                           { return line.find(src) != std::string::npos; });
                    if (!alreadyFailed)
                        failed.push_back(src + "\tcook did not produce output");
                }
            }

            std::filesystem::remove(manifest, ec);
            std::filesystem::remove(failedPath, ec);

            {
                std::lock_guard lock(s_importJobsMutex);
                job->processOk = processOk;
                job->cooked = std::move(cooked);
                job->failed.insert(job->failed.end(), failed.begin(), failed.end());
                job->progress = static_cast<uint32_t>(job->skipped.size() + job->cooked.size() + failed.size());
                job->finished = std::chrono::steady_clock::now();
                if (!job->failed.empty() && !job->cooked.empty())
                    job->status = "partial";
                else if (!job->failed.empty())
                    job->status = "failed";
                else
                    job->status = "complete";
                if (!processOk && job->error.empty())
                    job->error = "PhasmaCook exited with a non-zero status";
                PruneImportJobsLocked(job->id);
            } })
            .detach();

        {
            std::lock_guard lock(s_importJobsMutex);
            return ImportJobJson(*job).dump();
        }
    }

    std::string EditorToolRuntime::GetImportStatus(const std::string &argsJson) const
    {
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";
        const std::string jobId = args.value("job_id", "");
        if (jobId.empty())
            return R"({"error":"job_id required"})";

        std::lock_guard lock(s_importJobsMutex);
        auto it = s_importJobs.find(jobId);
        if (it == s_importJobs.end() || !it->second)
            return R"({"error":"import job not found"})";
        return ImportJobJson(*it->second).dump();
    }

    std::string EditorToolRuntime::LoadCookedMesh(const std::string &argsJson) const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto args = nlohmann::json::parse(argsJson.empty() ? "{}" : argsJson, nullptr, false);
        if (args.is_discarded() || !args.is_object())
            return R"({"error":"invalid args json"})";

        const std::string pathArg = args.value("path", "");
        if (pathArg.empty())
            return R"({"error":"path required"})";

        std::filesystem::path path = ResolveExistingAssetCandidate(pathArg, "Models");
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return R"({"error":"cooked mesh file not found"})";
        if (!ModelAssetCooked::IsCookedPath(path))
            return R"({"error":"load_cooked_mesh expects a .pemesh path"})";

        const std::string parentArg = args.value("parent", "");
        auto state = std::make_shared<State>();
        QueueAction([state, path, parentArg]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
            {
                state->result = R"({"error":"renderer not available"})";
            }
            else
            {
                Scene &sc = renderer->GetScene();
                NodeId *parent = nullptr;
                if (!parentArg.empty())
                {
                    std::string err;
                    parent = ResolveNode(sc, parentArg, err);
                    if (!parent)
                    {
                        state->result = JsonObj({{"error", JsonStr(err)}});
                    }
                }

                if (state->result.empty())
                {
                    ModelAsset *model = ModelAsset::Load(path);
                    if (!model)
                    {
                        state->result = R"({"error":"failed to load cooked mesh"})";
                    }
                    else
                    {
                        renderer->WaitAllFramesCommands();
                        SceneNodeHandle handle = sc.AddModelDeferred(model);
                        if (parent && handle.nodeId)
                            sc.ReparentNode(handle.nodeId, parent);
                        sc.UpdateGeometryBuffers();
                        sc.MarkDirty();

                        nlohmann::json roots = nlohmann::json::array();
                        for (NodeId *root : sc.GetModelRootNodes(model))
                        {
                            if (root && sc.IsNodeAlive(root))
                                roots.push_back({{"id", MakeNodeId(root)}, {"name", sc.GetNodeName(root)}});
                        }

                        nlohmann::json result;
                        result["status"] = "ok";
                        result["path"] = PathUtf8(path);
                        result["model_id"] = model->GetId();
                        result["label"] = model->GetLabel();
                        if (handle.nodeId && sc.IsNodeAlive(handle.nodeId))
                            result["root"] = {{"id", MakeNodeId(handle.nodeId)}, {"name", sc.GetNodeName(handle.nodeId)}};
                        result["model_roots"] = std::move(roots);
                        state->result = result.dump();
                    }
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(30), [&]
                                { return state->done; }))
            return R"({"error":"timeout"})";
        return state->result;
    }

    std::string EditorToolRuntime::FindLoadableModel(const std::string &query) const
    {
        if (query.empty())
            return "{\"error\":\"missing query\"}";

        std::string queryLower = ToLower(query);
        std::filesystem::path objectsDir(Path::Assets + "Objects");
        if (!std::filesystem::exists(objectsDir))
            return "{\"error\":\"Objects directory not found\"}";

        std::string arr = "[";
        bool first = true;
        int count = 0;
        try
        {
            for (const auto &entry : std::filesystem::recursive_directory_iterator(
                     objectsDir, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;

                std::string ext = ToLower(entry.path().extension().string());
                if (ext != ".glb" && ext != ".gltf" && ext != ".obj" && ext != ".fbx")
                    continue;

                std::string relPath = std::filesystem::relative(entry.path(), Path::Assets + "Objects").string();
                std::replace(relPath.begin(), relPath.end(), '\\', '/');

                if (ToLower(relPath).find(queryLower) == std::string::npos)
                    continue;

                if (!first)
                    arr += ",";
                arr += JsonStr(relPath);
                first = false;
                if (++count >= 20)
                    break;
            }
        }
        catch (const std::filesystem::filesystem_error &)
        {
            // Ignore filesystem errors; return whatever was found so far
        }
        arr += "]";
        return JsonObj({{"count", std::to_string(count)}, {"models", arr}});
    }

    std::string EditorToolRuntime::ReadAgentFile(const std::string &path) const
    {
        if (path.empty())
            return "{\"error\":\"missing path\"}";

        const std::string agentWorkspace = Path::Assets + "Agent/";
        std::filesystem::path fpath(path);
        if (fpath.is_relative())
            fpath = std::filesystem::path(agentWorkspace) / fpath;

        if (!IsPathSafe(fpath.string(), agentWorkspace))
            return "{\"error\":\"path outside workspace directory\"}";
        if (!std::filesystem::exists(fpath))
            return JsonObj({{"error", JsonStr("file not found: " + fpath.string())}});

        std::ifstream file(fpath, std::ios::in);
        if (!file.is_open())
            return JsonObj({{"error", JsonStr("cannot open: " + fpath.string())}});

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // Return the path relative to the workspace to avoid leaking host filesystem layout
        std::string displayPath = path.empty() ? fpath.filename().string() : path;
        return JsonObj({{"path", JsonStr(displayPath)}, {"content", JsonStr(content)}});
    }

    std::string EditorToolRuntime::WriteAgentFile(const std::string &path, const std::string &content, bool append) const
    {
        if (path.empty())
            return "{\"error\":\"missing path\"}";

        const std::string agentWorkspace = Path::Assets + "Agent/";
        std::filesystem::path fpath(path);
        if (fpath.is_relative())
            fpath = std::filesystem::path(agentWorkspace) / fpath;

        if (!IsPathSafe(fpath.string(), agentWorkspace))
            return "{\"error\":\"path outside workspace directory\"}";

        std::filesystem::path parentDir = fpath.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir))
        {
            std::error_code ec;
            std::filesystem::create_directories(parentDir, ec);
            if (ec)
                return JsonObj({{"error", JsonStr("cannot create directory: " + ec.message())}});
        }

        auto flags = std::ios::out;
        flags |= append ? std::ios::app : std::ios::trunc;

        std::ofstream file(fpath, flags);
        if (!file.is_open())
            return JsonObj({{"error", JsonStr("cannot write: " + fpath.string())}});
        file << content;
        file.close();

        return JsonObj({{"status", JsonStr("ok")}, {"path", JsonStr(fpath.string())}});
    }

    std::string EditorToolRuntime::TakeScreenshot() const
    {
        auto *renderer = GetGlobalSystem<RendererSystem>();
        if (!renderer)
            return "{\"error\":\"RendererSystem not available\"}";

        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
        const std::string agentDir = Path::Assets + "Agent/";
        {
            std::error_code ec;
            std::filesystem::create_directories(agentDir, ec);
        }
        std::string screenshotPath = agentDir + "ss_" + std::to_string(ts) + ".png";
        std::filesystem::remove(screenshotPath);

        int mouseX = 0, mouseY = 0;
        SDL_GetMouseState(&mouseX, &mouseY);

        // Push the event from the main thread (via QueueAction) so it lands AFTER
        // Window::ProcessEvents() has already called ClearPushedEvents() for this frame.
        // If pushed directly from the HTTP thread, it gets wiped before RecordPasses() sees it.
        QueueAction([screenshotPath]()
                    { EventSystem::PushEvent(EventType::Screenshot, screenshotPath); });

        // Poll until the file exists AND its size has been stable for two consecutive checks,
        // ensuring the renderer has finished writing before we read it.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        uintmax_t prevSize = 0;
        int stableCount = 0;
        while (true)
        {
            if (std::chrono::steady_clock::now() > deadline)
                return "{\"error\":\"screenshot timeout\"}";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::error_code ec;
            const uintmax_t sz = std::filesystem::file_size(screenshotPath, ec);
            if (!ec && sz > 0)
            {
                if (sz == prevSize)
                {
                    if (++stableCount >= 2)
                        break;
                }
                else
                {
                    prevSize = sz;
                    stableCount = 0;
                }
            }
        }

        int w = 0, h = 0, c = 0;
        uint8_t *pixels = stbi_load(screenshotPath.c_str(), &w, &h, &c, 4);
        std::filesystem::remove(screenshotPath);
        if (!pixels)
            return "{\"error\":\"failed to load screenshot\"}";

        auto drawCursorOverlay = [](uint8_t *px, int imgW, int imgH, int cx, int cy)
        {
            if (cx < 0 || cy < 0 || cx >= imgW || cy >= imgH)
                return;
            constexpr int kRadius = 10;
            constexpr int kThick = 2;
            auto setPixel = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b)
            {
                if (x < 0 || y < 0 || x >= imgW || y >= imgH)
                    return;
                uint8_t *p = px + (y * imgW + x) * 4;
                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = 255;
            };
            for (int d = -kRadius; d <= kRadius; ++d)
            {
                for (int t = -kThick; t <= kThick; ++t)
                {
                    setPixel(cx + d, cy + t, 0, 0, 0);
                    setPixel(cx + t, cy + d, 0, 0, 0);
                }
            }
            for (int d = -kRadius; d <= kRadius; ++d)
            {
                setPixel(cx + d, cy, 255, 255, 255);
                setPixel(cx, cy + d, 255, 255, 255);
            }
        };
        drawCursorOverlay(pixels, w, h, mouseX, mouseY);

        int rw = w, rh = h;
        std::vector<uint8_t> resizedPixels;
        const int kMaxDim = 1024;
        if (w > kMaxDim || h > kMaxDim)
        {
            resizedPixels = ResizeRGBA(pixels, w, h, rw, rh, kMaxDim);
            stbi_image_free(pixels);
            pixels = nullptr;
        }

        constexpr size_t kMaxBytes = 5 * 1024 * 1024;
        std::vector<uint8_t> pngData;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint8_t *src = pixels ? pixels : resizedPixels.data();
            pngData = pmcp::EncodeRGBA_PNG(src, rw, rh);
            if (pngData.empty() || pngData.size() <= kMaxBytes)
                break;

            int nw = rw * 3 / 4, nh = rh * 3 / 4;
            resizedPixels = ResizeRGBA(pixels ? pixels : resizedPixels.data(), rw, rh, nw, nh, std::max(nw, nh));
            if (pixels)
            {
                stbi_image_free(pixels);
                pixels = nullptr;
            }
            rw = nw;
            rh = nh;
        }
        if (pixels)
            stbi_image_free(pixels);

        if (pngData.empty())
            return "{\"error\":\"failed to encode screenshot as PNG\"}";

        std::ofstream out(screenshotPath, std::ios::binary | std::ios::trunc);
        if (!out)
            return "{\"error\":\"failed to open screenshot path for write\"}";
        out.write(reinterpret_cast<const char *>(pngData.data()), static_cast<std::streamsize>(pngData.size()));
        out.close();
        if (!out)
            return "{\"error\":\"failed to write screenshot bytes\"}";

        return nlohmann::json{
            {"path", screenshotPath},
            {"mime_type", "image/png"},
            {"width", rw},
            {"height", rh},
            {"bytes", pngData.size()},
            {"cursor_x", mouseX},
            {"cursor_y", mouseY},
        }
            .dump();
    }

    std::string EditorToolRuntime::QueryImGuiWindows() const
    {
        // Heap-allocate shared state so the lambda is safe even if wait_for times out first.
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            nlohmann::json result;
        };
        auto state = std::make_shared<State>();
        state->result["windows"] = nlohmann::json::array();
        state->result["tab_bars"] = nlohmann::json::array();

        QueueAction([state]()
                    {
            ImGuiContext *ctx = ImGui::GetCurrentContext();
            if (ctx)
            {
                for (ImGuiWindow *win : ctx->Windows)
                {
                    if (!win->WasActive || win->Size.x <= 0 || win->Size.y <= 0)
                        continue;
                    state->result["windows"].push_back({
                        {"name", win->Name},
                        {"x", static_cast<int>(win->Pos.x)},
                        {"y", static_cast<int>(win->Pos.y)},
                        {"width", static_cast<int>(win->Size.x)},
                        {"height", static_cast<int>(win->Size.y)},
                        {"collapsed", win->Collapsed},
                        {"focused", ctx->NavWindow == win},
                    });
                }

                auto emitTabBar = [&](ImGuiTabBar *tb)
                {
                    if (!tb || tb->Tabs.Size == 0)
                        return;
                    int barY = static_cast<int>((tb->BarRect.Min.y + tb->BarRect.Max.y) / 2.0f);
                    nlohmann::json tabsArr = nlohmann::json::array();
                    for (int t = 0; t < tb->Tabs.Size; ++t)
                    {
                        ImGuiTabItem *tab = &tb->Tabs[t];
                        const char *name = ImGui::TabBarGetTabName(tb, tab);
                        std::string tabName = name ? name : "";
                        if (auto pos = tabName.find("##"); pos != std::string::npos)
                            tabName = tabName.substr(0, pos);
                        int tabCenterX = static_cast<int>(tb->BarRect.Min.x + tab->Offset + tab->Width / 2.0f);
                        tabsArr.push_back({
                            {"name", tabName},
                            {"click_x", tabCenterX},
                            {"click_y", barY},
                            {"selected", tab->ID == tb->SelectedTabId},
                        });
                    }
                    state->result["tab_bars"].push_back({
                        {"bar_x", static_cast<int>(tb->BarRect.Min.x)},
                        {"bar_y", static_cast<int>(tb->BarRect.Min.y)},
                        {"tabs", tabsArr},
                    });
                };

                for (auto &kv : ctx->DockContext.Nodes.Data)
                {
                    auto *node = static_cast<ImGuiDockNode *>(kv.val_p);
                    if (node)
                        emitTabBar(node->TabBar);
                }
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                { return state->done; }))
            return "{\"error\":\"timeout\"}";

        return state->result.dump();
    }

    std::string EditorToolRuntime::InjectMouseInput(const std::string &args) const
    {
        std::string action = ExtractArgStr(args, "action");
        if (action.empty())
            return "{\"error\":\"missing action\"}";

        int scrollX = static_cast<int>(ExtractArgInt(args, "scroll_x", 0));
        int scrollY = static_cast<int>(ExtractArgInt(args, "scroll_y", 0));

        // Parse coordinates — u/v conversion is deferred to the main thread (QueueAction).
        // Store as float u/v or int x/y.
        bool useUV = false;
        float u = 0.0f, v = 0.0f;
        int x = -1, y = -1;
        {
            auto parsedArgs = ParseArgs(args);
            if (parsedArgs.contains("u") || parsedArgs.contains("v"))
            {
                u = ExtractArgNum(args, "u");
                v = ExtractArgNum(args, "v");
                useUV = true;
            }
            else
            {
                x = static_cast<int>(ExtractArgInt(args, "x", -1));
                y = static_cast<int>(ExtractArgInt(args, "y", -1));
            }
        }

        if (!useUV && (x < 0 || y < 0))
            return "{\"error\":\"provide u/v (normalized) or x/y (real pixels)\"}";

        // Heap-allocate shared state so the lambda is safe even if wait_for times out first.
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string tabHit;
            int resolvedX = 0, resolvedY = 0;
            float resolvedU = 0.0f, resolvedV = 0.0f;
        };
        auto state = std::make_shared<State>();
        state->resolvedX = x;
        state->resolvedY = y;
        state->resolvedU = useUV ? u : -1.0f;
        state->resolvedV = useUV ? v : -1.0f;

        QueueAction([this, state, useUV, u, v, x, y, action, scrollX, scrollY]()
                    {
            // Resolve coordinates using the stored SDL_Window* (safe on any thread).
            int rx = x, ry = y;
            {
                auto *sdlWin = static_cast<SDL_Window *>(m_sdlWindow);
                int winW = 0, winH = 0;
                if (sdlWin)
                    SDL_GetWindowSize(sdlWin, &winW, &winH);
                if (useUV)
                {
                    rx = static_cast<int>(u * winW);
                    ry = static_cast<int>(v * winH);
                    state->resolvedX = rx;
                    state->resolvedY = ry;
                    state->resolvedU = (winW > 0) ? static_cast<float>(rx) / winW : u;
                    state->resolvedV = (winH > 0) ? static_cast<float>(ry) / winH : v;
                }
                else
                {
                    state->resolvedU = (winW > 0) ? static_cast<float>(rx) / winW : 0.0f;
                    state->resolvedV = (winH > 0) ? static_cast<float>(ry) / winH : 0.0f;
                }
            }

            ImGuiContext *ctx = ImGui::GetCurrentContext();
            ImGuiIO &io = ImGui::GetIO();
            ImVec2 pos{static_cast<float>(rx), static_cast<float>(ry)};

            SDL_Window *focusWin = SDL_GetMouseFocus();
            SDL_Window *mainWin = static_cast<SDL_Window *>(m_sdlWindow);
            if (SDL_Window *win = focusWin ? focusWin : mainWin)
                SDL_WarpMouseInWindow(win, rx, ry);

            bool handledByTabBar = false;
            if ((action == "click" || action == "double_click") && ctx)
            {
                for (auto &kv : ctx->DockContext.Nodes.Data)
                {
                    auto *node = static_cast<ImGuiDockNode *>(kv.val_p);
                    if (!node || !node->TabBar)
                        continue;
                    ImGuiTabBar *tb = node->TabBar;
                    if (!tb->BarRect.Contains(pos))
                        continue;
                    for (int t = 0; t < tb->Tabs.Size; ++t)
                    {
                        ImGuiTabItem *tab = &tb->Tabs[t];
                        ImRect tabRect{
                            {tb->BarRect.Min.x + tab->Offset, tb->BarRect.Min.y},
                            {tb->BarRect.Min.x + tab->Offset + tab->Width, tb->BarRect.Max.y}};
                        if (!tabRect.Contains(pos))
                            continue;
                        ImGui::TabBarQueueFocus(tb, tab);
                        const char *name = ImGui::TabBarGetTabName(tb, tab);
                        state->tabHit = name ? name : "";
                        if (auto p = state->tabHit.find("##"); p != std::string::npos)
                            state->tabHit = state->tabHit.substr(0, p);
                        handledByTabBar = true;
                        break;
                    }
                    if (handledByTabBar)
                        break;
                }
            }

            if (!handledByTabBar)
            {
                io.AddMousePosEvent(pos.x, pos.y);
                if (action == "scroll")
                {
                    io.AddMouseWheelEvent(static_cast<float>(scrollX), static_cast<float>(scrollY));
                }
                else
                {
                    int btn = (action == "right_click") ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
                    io.AddMouseButtonEvent(btn, true);
                    io.AddMouseButtonEvent(btn, false); // release in the same frame → clean click, no stuck-button risk
                }
            }

            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        std::unique_lock lock(state->mtx);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                { return state->done; }))
            return "{\"error\":\"timeout waiting for main thread\"}";

        nlohmann::json res{{"ok", true}, {"x", state->resolvedX}, {"y", state->resolvedY}, {"u", state->resolvedU}, {"v", state->resolvedV}, {"action", action}};
        if (!state->tabHit.empty())
            res["tab_focused"] = state->tabHit;
        return res.dump();
    }

    std::string EditorToolRuntime::TakeProfilerSnapshot() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            auto *profiler = renderer ? renderer->GetGUI().GetWidget<ProfilerWidget>() : nullptr;
            if (!profiler)
                state->result = "{\"error\":\"ProfilerWidget not available\"}";
            else
            {
                const std::string path = profiler->TakeSnapshot();
                if (path.empty())
                    state->result = "{\"error\":\"snapshot failed\"}";
                else
                    state->result = JsonObj({{"path", JsonStr(path)}});
            }
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for profiler snapshot\"}";
        }
        return state->result;
    }

    std::string EditorToolRuntime::QueryEditorActions() const
    {
        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
                state->result = "{\"error\":\"renderer not available\"}";
            else
                state->result = renderer->GetGUI().QueryEditorActions();
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for editor actions\"}";
        }
        return state->result;
    }

    std::string EditorToolRuntime::SetEditorWindowOpen(const std::string &windowName, const std::string &args) const
    {
        if (windowName.empty())
            return "{\"error\":\"missing window\"}";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, windowName, args]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
                state->result = "{\"error\":\"renderer not available\"}";
            else
                state->result = renderer->GetGUI().SetEditorWindowOpen(windowName, args);
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for editor window action\"}";
        }
        return state->result;
    }

    std::string EditorToolRuntime::InvokeEditorAction(const std::string &actionId, const std::string &args) const
    {
        if (actionId.empty())
            return "{\"error\":\"missing action\"}";

        struct State
        {
            std::mutex mtx;
            std::condition_variable cv;
            bool done = false;
            std::string result;
        };
        auto state = std::make_shared<State>();

        QueueAction([state, actionId, args]()
                    {
            auto *renderer = GetGlobalSystem<RendererSystem>();
            if (!renderer)
                state->result = "{\"error\":\"renderer not available\"}";
            else
                state->result = renderer->GetGUI().InvokeEditorAction(actionId, args);
            {
                std::lock_guard lock(state->mtx);
                state->done = true;
            }
            state->cv.notify_one(); });

        {
            std::unique_lock lock(state->mtx);
            if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&state]
                                    { return state->done; }))
                return "{\"error\":\"timeout waiting for editor action\"}";
        }
        return state->result;
    }

    std::string EditorToolRuntime::ReloadModule() const
    {
        QueueAction([]()
                    { EventSystem::PushEvent(EventType::ReloadModule); });
        return "{\"ok\":true}";
    }
} // namespace pe
