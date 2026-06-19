#include "Render/ScreenshotWriter.h"

#include <ctime>
#include <cstring>

namespace pe
{
    namespace
    {
        uint32_t Crc32(const uint8_t *data, size_t length)
        {
            uint32_t crc = 0xFFFFFFFF;
            for (size_t i = 0; i < length; ++i)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
            return crc ^ 0xFFFFFFFF;
        }

        void Put32be(std::vector<uint8_t> &out, uint32_t value)
        {
            out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(value & 0xFF));
        }

        void WriteChunk(std::vector<uint8_t> &out, const char type[4], const uint8_t *data, size_t length)
        {
            Put32be(out, static_cast<uint32_t>(length));
            const size_t typeStart = out.size();
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<uint8_t>(type[i]));
            if (data && length > 0)
                out.insert(out.end(), data, data + length);
            Put32be(out, Crc32(&out[typeStart], 4 + length));
        }

        std::vector<uint8_t> EncodeRgbaPng(const uint8_t *rgba, uint32_t width, uint32_t height)
        {
            if (!rgba || width == 0 || height == 0)
                return {};

            std::vector<uint8_t> png;
            const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
            png.insert(png.end(), signature, signature + sizeof(signature));

            std::vector<uint8_t> ihdr;
            Put32be(ihdr, width);
            Put32be(ihdr, height);
            ihdr.push_back(8);
            ihdr.push_back(6);
            ihdr.push_back(0);
            ihdr.push_back(0);
            ihdr.push_back(0);
            WriteChunk(png, "IHDR", ihdr.data(), ihdr.size());

            const size_t rowBytes = static_cast<size_t>(width) * 4;
            const size_t filteredRowBytes = rowBytes + 1;
            std::vector<uint8_t> raw(static_cast<size_t>(height) * filteredRowBytes);
            for (uint32_t y = 0; y < height; ++y)
            {
                const size_t rowOffset = static_cast<size_t>(y) * filteredRowBytes;
                raw[rowOffset] = 0;
                std::memcpy(&raw[rowOffset + 1], rgba + static_cast<size_t>(y) * rowBytes, rowBytes);
            }

            uint32_t adlerA = 1;
            uint32_t adlerB = 0;
            for (uint8_t byte : raw)
            {
                adlerA = (adlerA + byte) % 65521;
                adlerB = (adlerB + adlerA) % 65521;
            }

            std::vector<uint8_t> zlib;
            zlib.push_back(0x78);
            zlib.push_back(0x01);

            size_t offset = 0;
            while (offset < raw.size())
            {
                const size_t blockLength = std::min<size_t>(raw.size() - offset, 65535);
                const bool last = offset + blockLength >= raw.size();
                zlib.push_back(last ? 0x01 : 0x00);
                const uint16_t len = static_cast<uint16_t>(blockLength);
                const uint16_t nlen = static_cast<uint16_t>(~len);
                zlib.push_back(static_cast<uint8_t>(len & 0xFF));
                zlib.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
                zlib.push_back(static_cast<uint8_t>(nlen & 0xFF));
                zlib.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
                zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLength);
                offset += blockLength;
            }

            Put32be(zlib, (adlerB << 16) | adlerA);
            WriteChunk(png, "IDAT", zlib.data(), zlib.size());
            WriteChunk(png, "IEND", nullptr, 0);
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
