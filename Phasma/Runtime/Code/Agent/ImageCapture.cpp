#include "Agent/ImageCapture.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Phasma/MCP/Utils.h"
#include <fstream>

namespace pe::imagecapture
{
    std::vector<uint8_t> ResizeRGBA(const uint8_t *src, int srcW, int srcH, int &outW, int &outH, int maxDim)
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

    size_t FormatBytesPerPixel(::PeFormat format)
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

    namespace
    {
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
                        if (velocityView) // velocity_format rgba16f / rgba32f: xy is the motion, as in RG
                        {
                            d[0] = ToByte(r * 0.5f + 0.5f);
                            d[1] = ToByte(g * 0.5f + 0.5f);
                            d[2] = ToByte(glm::length(vec2(r, g)));
                            d[3] = 255;
                            break;
                        }

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
                        if (velocityView) // velocity_format rgba16f / rgba32f: xy is the motion, as in RG
                        {
                            d[0] = ToByte(r * 0.5f + 0.5f);
                            d[1] = ToByte(g * 0.5f + 0.5f);
                            d[2] = ToByte(glm::length(vec2(r, g)));
                            d[3] = 255;
                            break;
                        }

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

        std::string ValidateImageCapture(Image *image, uint32_t mip, uint32_t layer, const std::string &aspect)
        {
            if (mip >= image->GetMipLevels())
                return "mip out of range";
            if (layer >= image->GetArrayLayers())
                return "array_index out of range";
            if (image->GetSamples() != PE_SAMPLE_COUNT_1)
                return "multisampled image readback is not supported";

            const bool validAspect = aspect == "auto" || aspect == "color" || aspect == "depth" || aspect == "stencil";
            if (!validAspect)
                return "aspect must be auto, color, depth, or stencil";
            if (aspect == "stencil")
                return "stencil image capture is not supported yet";
            if (aspect == "depth" && !PeFormatHasDepth(image->GetFormat()))
                return "requested depth aspect for an image without depth";
            if (aspect == "color" && PeFormatHasDepthOrStencil(image->GetFormat()))
                return "requested color aspect for a depth/stencil image";
            if (PeFormatHasDepthOrStencil(image->GetFormat()) && RHII.GetApi() == PE_GRAPHICS_API_DX12)
                return "DX12 depth/stencil image readback is not supported by the current RHI copy path";
            if (PeFormatHasStencil(image->GetFormat()))
                return "stencil image capture is not supported yet";
            if (FormatBytesPerPixel(image->GetFormat()) == 0)
                return std::string("unsupported image format: ") + PeFormatName(image->GetFormat());
            if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN && !(image->GetUsage() & PE_IMAGE_USAGE_TRANSFER_SRC))
                return "image is not transfer-src; cannot stage readback on Vulkan";

            return {};
        }

        std::string Visualization(Image *image, const nlohmann::json &args)
        {
            std::string visualize = ToLower(args.value("visualize", "auto"));
            if (visualize != "auto")
                return visualize;

            const std::string name = ToLower(image->GetName());
            if (PeFormatHasDepth(image->GetFormat()))
                return "depth";
            if (name.find("normal") != std::string::npos)
                return "normal";
            if (name.find("velocity") != std::string::npos)
                return "velocity";

            return "linear";
        }

        bool ReadImagePixels(Image *image, uint32_t mip, uint32_t layer, uint32_t width, uint32_t height,
                             size_t rowPitch, const std::string &visualize, std::vector<uint8_t> &rgba,
                             std::string &error)
        {
            Buffer *staging = Buffer::Create({
                .size = rowPitch * height,
                .usage = PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
                .name = "McpImageReadback",
            });

            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->CopyImageToBuffer(image, staging, mip, layer, 1);
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);

            staging->Map();
            const uint8_t *src = static_cast<const uint8_t *>(staging->Data());
            const bool converted = ConvertImageRowsToRGBA(
                src, width, height, rowPitch, image->GetFormat(), visualize, rgba, error);
            staging->Unmap();
            Buffer::Destroy(staging);
            return converted;
        }

        std::string WriteCapturePng(Image *image, uint32_t mip, uint32_t layer, const std::vector<uint8_t> &png)
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

            const std::string outPath = outDir + "image_" + safeName + "_mip" + std::to_string(mip) +
                                        "_layer" + std::to_string(layer) + "_" + std::to_string(ts) + ".png";
            std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
            out.close();
            return outPath;
        }

        nlohmann::json EncodeCapture(Image *image, const nlohmann::json &args,
                                     const std::vector<uint8_t> &rgba, nlohmann::json result)
        {
            int outW = result["original_width"].get<int>();
            int outH = result["original_height"].get<int>();
            int maxDim = args.value("max_width", args.value("max_dimension", 1024));
            if (maxDim <= 0)
                maxDim = std::numeric_limits<int>::max();

            std::vector<uint8_t> resized;
            const uint8_t *pngSrc = rgba.data();
            if (std::max(outW, outH) > maxDim)
            {
                resized = ResizeRGBA(rgba.data(), outW, outH, outW, outH, maxDim);
                pngSrc = resized.data();
            }
            std::vector<uint8_t> png = pmcp::EncodeRGBA_PNG(pngSrc, outW, outH);
            if (png.empty())
                return {{"error", "failed to encode PNG"}};

            result["path"] = WriteCapturePng(image, result["mip"], result["array_index"], png);
            result["id"] = args.value("id", "");
            result["source"] = "gpu_image";
            result["name"] = image->GetName();
            result["mime_type"] = "image/png";
            result["format"] = PeFormatName(image->GetFormat());
            result["width"] = outW;
            result["height"] = outH;
            result["bytes"] = png.size();
            if (args.value("return_base64", false))
                result["image_base64"] = pmcp::Base64Encode(png.data(), png.size());

            return result;
        }
    } // namespace

    nlohmann::json CaptureGpuImage(Image *image, const nlohmann::json &args)
    {
        const int mip = args.value("mip", 0);
        const int layer = args.value("array_index", args.value("layer", 0));
        if (ToLower(args.value("format", "png")) != "png")
            return {{"error", "only png output is supported"}};
        if (mip < 0 || layer < 0)
            return {{"error", "mip and array_index must be non-negative"}};
        if (!image)
            return {{"error", "image resource not found"}};

        std::string error = ValidateImageCapture(image, mip, layer, ToLower(args.value("aspect", "auto")));
        if (!error.empty())
            return {{"error", error}};
        if (!RHII.GetMainQueue())
            return {{"error", "main queue not available"}};

        const uint32_t width = std::max(image->GetWidth() >> mip, 1u);
        const uint32_t height = std::max(image->GetHeight() >> mip, 1u);
        const size_t rowBytes = static_cast<size_t>(width) * FormatBytesPerPixel(image->GetFormat());
        const size_t rowPitch = RHII.GetApi() == PE_GRAPHICS_API_DX12 ? RHII.AlignTextureRowPitch(rowBytes) : rowBytes;
        const size_t bufferSize = rowPitch * height;
        const size_t cap = args.value("allow_large", false) ? 512 * 1024 * 1024 : 64 * 1024 * 1024;
        if (bufferSize > cap)
        {
            return {{"error", "image readback exceeds staging allocation cap; pass allow_large=true"},
                    {"readback_size", bufferSize},
                    {"cap", cap}};
        }

        const std::string visualize = Visualization(image, args);
        std::vector<uint8_t> rgba;
        if (!ReadImagePixels(image, mip, layer, width, height, rowPitch, visualize, rgba, error))
            return {{"error", error}};

        nlohmann::json result = {
            {"mip", mip},
            {"array_index", layer},
            {"visualize", visualize},
            {"original_width", width},
            {"original_height", height},
            {"readback_size", bufferSize},
        };
        return EncodeCapture(image, args, rgba, std::move(result));
    }
} // namespace pe::imagecapture
