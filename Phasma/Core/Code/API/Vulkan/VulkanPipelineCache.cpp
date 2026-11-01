#include "API/Vulkan/VulkanPipelineCache.h"
#include "Base/Hash.h"
#include "Base/Path.h"
#include "Base/Process.h"
#include <fstream>

namespace pe
{
    namespace
    {
        // The engine's own header before the driver's data: a magic, the data size and its FNV-1a hash, so a truncated
        // or partly written file is caught before the driver reads it (some drivers trust their header and crash on the
        // rest).
        struct FileHeader
        {
            char magic[8];
            uint64_t size;
            uint64_t hash;
        };

        constexpr char kMagic[8] = {'P', 'E', 'V', 'K', 'P', 'C', '0', '1'};

        // A cache only grows (stale pipelines are never evicted), so a file past this starts over.
        constexpr uint64_t kMaxBytes = 256ull * 1024ull * 1024ull;

        std::filesystem::path CachePath()
        {
            return PathFromUtf8(Path::RootPath() + "ShaderCache/_pipelines/vulkan.bin");
        }

        uint64_t HashBytes(const std::vector<char> &data)
        {
            return Fnv1a64(std::string_view(data.data(), data.size()));
        }

        // VkPipelineCacheHeaderVersionOne: the data must come from this device and driver.
        bool MatchesDevice(const std::vector<char> &data, vk::PhysicalDevice gpu)
        {
            VkPipelineCacheHeaderVersionOne header{};
            if (data.size() < sizeof(header))
                return false;

            std::memcpy(&header, data.data(), sizeof(header));
            const vk::PhysicalDeviceProperties props = gpu.getProperties();
            return header.headerSize >= sizeof(header) &&
                   header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                   header.vendorID == props.vendorID &&
                   header.deviceID == props.deviceID &&
                   std::memcmp(header.pipelineCacheUUID, props.pipelineCacheUUID.data(), VK_UUID_SIZE) == 0;
        }

        // The driver's data from the file, or empty when there is none this device can use.
        std::vector<char> ReadCacheFile(vk::PhysicalDevice gpu)
        {
            std::ifstream file(CachePath(), std::ios::binary);
            FileHeader header{};
            if (!file.read(reinterpret_cast<char *>(&header), sizeof(header)))
                return {};

            if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0 || header.size > kMaxBytes)
                return {};

            std::vector<char> data(header.size);
            if (!file.read(data.data(), static_cast<std::streamsize>(data.size())))
                return {};

            if (HashBytes(data) != header.hash || !MatchesDevice(data, gpu))
                return {};

            return data;
        }

        void WriteCacheFile(const std::vector<char> &data)
        {
            const std::filesystem::path path = CachePath();
            std::filesystem::path temp = path;
            temp += ".tmp";

            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            {
                std::ofstream file(temp, std::ios::binary | std::ios::trunc);
                FileHeader header{};
                std::memcpy(header.magic, kMagic, sizeof(kMagic));
                header.size = data.size();
                header.hash = HashBytes(data);
                file.write(reinterpret_cast<const char *>(&header), sizeof(header));
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!file)
                {
                    PE_WARN("[Vulkan] pipeline cache: cannot write %s", PathUtf8(temp).c_str());
                    return;
                }
            }

            // Replaces the old file in one step: another engine process saving at the same time leaves either file whole.
            std::filesystem::rename(temp, path, ec);
            if (ec)
                PE_WARN("[Vulkan] pipeline cache: cannot replace %s (%s)", PathUtf8(path).c_str(), ec.message().c_str());
        }
    } // namespace

    vk::PipelineCache LoadVulkanPipelineCache(vk::Device device, vk::PhysicalDevice gpu)
    {
        std::vector<char> data;
        try
        {
            data = ReadCacheFile(gpu);
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Vulkan] pipeline cache: cannot read %s (%s)", PathUtf8(CachePath()).c_str(), e.what());
        }

        vk::PipelineCacheCreateInfo info{};
        info.initialDataSize = data.size();
        info.pInitialData = data.empty() ? nullptr : data.data();
        vk::PipelineCache cache = device.createPipelineCache(info);
        PE_INFO("[Vulkan] pipeline cache: %zu KB loaded", data.size() / 1024);
        return cache;
    }

    void SaveAndDestroyVulkanPipelineCache(vk::Device device, vk::PipelineCache cache)
    {
        if (!cache)
            return;

        try
        {
            size_t size = 0;
            const vk::Result sized = device.getPipelineCacheData(cache, &size, nullptr);
            std::vector<char> data(sized == vk::Result::eSuccess ? size : 0);
            const bool read = !data.empty() &&
                              device.getPipelineCacheData(cache, &size, data.data()) == vk::Result::eSuccess;
            if (read && size <= kMaxBytes)
            {
                data.resize(size);
                WriteCacheFile(data);
                PE_INFO("[Vulkan] pipeline cache: %zu KB saved", size / 1024);
            }
        }
        catch (const std::exception &e)
        {
            PE_WARN("[Vulkan] pipeline cache: not saved (%s)", e.what());
        }

        device.destroyPipelineCache(cache);
    }
} // namespace pe
