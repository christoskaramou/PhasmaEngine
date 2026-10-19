#include "Render/ScreenshotWriter.h"

// The stb_image_write PhasmaMCP already vendors, compiled privately here: PhasmaMCP is desktop-only
// and links its own extern "C" copy into the same binaries.
#define STB_IMAGE_WRITE_STATIC
#define STBIWDEF static inline
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../../MCP/src/stb_image_write.h"

namespace pe
{
    namespace
    {
        void AppendPngBytes(void *context, void *data, int size)
        {
            auto *out = static_cast<std::vector<uint8_t> *>(context);
            const auto *bytes = static_cast<const uint8_t *>(data);
            out->insert(out->end(), bytes, bytes + size);
        }

        std::vector<uint8_t> EncodeRgbaPng(const uint8_t *rgba, uint32_t width, uint32_t height)
        {
            std::vector<uint8_t> png;
            const int w = static_cast<int>(width);
            const int h = static_cast<int>(height);
            if (!stbi_write_png_to_func(AppendPngBytes, &png, w, h, 4, rgba, w * 4))
                return {};

            return png;
        }

        bool IsBgra8Format(::PeFormat format)
        {
            return format == PE_FORMAT_B8G8R8A8_UNORM || format == PE_FORMAT_B8G8R8A8_SRGB;
        }

        std::string MakeDefaultScreenshotPath()
        {
            std::string dir = Path::Executable + "Screenshots/";
            std::filesystem::create_directories(dir);

            const auto now = std::chrono::system_clock::now();
            const auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#if defined(PE_WIN32)
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char buffer[64];
            std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
            return dir + "screenshot_" + buffer + ".png";
        }
    } // namespace

    bool WriteScreenshotPng(const ScreenshotWriteDesc &desc, std::string *resolvedPath)
    {
        if (!desc.pixels || desc.width == 0 || desc.height == 0)
            return false;

        const size_t rowPitch = desc.rowPitch ? desc.rowPitch : static_cast<size_t>(desc.width) * 4;
        const bool isBgra = IsBgra8Format(desc.format);
        const size_t pixelCount = static_cast<size_t>(desc.width) * desc.height;
        std::vector<uint8_t> rgba(pixelCount * 4);

        for (uint32_t y = 0; y < desc.height; ++y)
        {
            const uint8_t *srcRow = desc.pixels + static_cast<size_t>(y) * rowPitch;
            uint8_t *dstRow = rgba.data() + static_cast<size_t>(y) * desc.width * 4;
            for (uint32_t x = 0; x < desc.width; ++x)
            {
                const uint8_t *src = srcRow + static_cast<size_t>(x) * 4;
                uint8_t *dst = dstRow + static_cast<size_t>(x) * 4;
                dst[0] = isBgra ? src[2] : src[0];
                dst[1] = src[1];
                dst[2] = isBgra ? src[0] : src[2];
                dst[3] = src[3];
            }
        }

        const std::vector<uint8_t> png = EncodeRgbaPng(rgba.data(), desc.width, desc.height);
        if (png.empty())
            return false;

        const std::string path = desc.path.empty() ? MakeDefaultScreenshotPath() : desc.path;
        const std::filesystem::path outputPath(path);
        if (outputPath.has_parent_path())
            std::filesystem::create_directories(outputPath.parent_path());

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            PE_ERROR("[Renderer] Failed to save screenshot: %s", path.c_str());
            return false;
        }

        file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!file.good())
        {
            PE_ERROR("[Renderer] Failed to write screenshot: %s", path.c_str());
            return false;
        }

        if (resolvedPath)
            *resolvedPath = path;
        return true;
    }
} // namespace pe
