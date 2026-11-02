#include "API/Vulkan/VulkanPipelineCache.h"
#include "Base/Hash.h"
#include "Base/Path.h"
#include "Base/Process.h"
#include "Base/ThreadPool.h"

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
            // Unique per process and save: two processes, or two saves of one, never write the same temporary file.
            static std::atomic<uint32_t> nextSave{0};
#if defined(PE_WIN32)
            const unsigned long processId = GetCurrentProcessId();
#else
            const unsigned long processId = static_cast<unsigned long>(getpid());
#endif
            const std::filesystem::path path = CachePath();
            std::filesystem::path temp = path;
            temp += "." + std::to_string(processId) + "_" + std::to_string(nextSave.fetch_add(1)) + ".tmp";

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
            {
                PE_WARN("[Vulkan] pipeline cache: cannot replace %s (%s)", PathUtf8(path).c_str(), ec.message().c_str());
                std::filesystem::remove(temp, ec);
            }
        }

        // A process killed mid-save leaves its temporary file (up to the size cap); one saving now is seconds old.
        void RemoveStaleTemporaries()
        {
            const std::filesystem::path path = CachePath();
            const std::string prefix = PathUtf8(path.filename()) + ".";
            const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::minutes(1);
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(path.parent_path(), ec))
            {
                const std::string name = PathUtf8(entry.path().filename());
                const bool temporary = name.starts_with(prefix) && name.ends_with(".tmp");
                if (temporary && entry.last_write_time(ec) < cutoff)
                    std::filesystem::remove(entry.path(), ec);
            }
        }

        // The driver's data, or empty when it has none or more than the cap.
        std::vector<char> ReadCacheData(vk::Device device, vk::PipelineCache cache)
        {
            size_t size = 0;
            if (device.getPipelineCacheData(cache, &size, nullptr) != vk::Result::eSuccess || size == 0 || size > kMaxBytes)
                return {};

            std::vector<char> data(size);
            if (device.getPipelineCacheData(cache, &size, data.data()) != vk::Result::eSuccess)
                return {};

            data.resize(size);
            return data;
        }

        void SaveNow(vk::Device device, vk::PipelineCache cache)
        {
            try
            {
                const std::vector<char> data = ReadCacheData(device, cache);
                if (!data.empty())
                    WriteCacheFile(data);
            }
            catch (const std::exception &e)
            {
                PE_WARN("[Vulkan] pipeline cache: not saved (%s)", e.what());
            }
        }

        // Async saves run on one worker at a time. A request while it saves, or in the 5 s after, folds into one more
        // save at the end of those 5 s: a cold open's dozens of adoptions write the file every 5 s at most, and the last
        // request is always written. Shutdown cuts the wait short and saves on its own.
        struct SaveQueue
        {
            std::mutex mutex;
            std::condition_variable wake;
            bool running = false; // a worker owns the saves
            bool pending = false; // requested since that worker's last save
            bool closing = false; // the cache is about to go
        };

        SaveQueue &Saves()
        {
            static SaveQueue saves;
            return saves;
        }

        void SaveWorker(vk::Device device, vk::PipelineCache cache)
        {
            SaveQueue &saves = Saves();
            for (;;)
            {
                SaveNow(device, cache);

                std::unique_lock<std::mutex> lock(saves.mutex);
                saves.wake.wait_for(lock, std::chrono::seconds(5), [&]
                                    { return saves.closing; });
                if (!saves.pending || saves.closing)
                {
                    saves.running = false;
                    saves.wake.notify_all();
                    return;
                }

                saves.pending = false;
            }
        }
    } // namespace

    vk::PipelineCache LoadVulkanPipelineCache(vk::Device device, vk::PhysicalDevice gpu)
    {
        std::vector<char> data;
        try
        {
            RemoveStaleTemporaries();
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

        std::lock_guard<std::mutex> lock(Saves().mutex);
        Saves().closing = false;
        return cache;
    }

    void SaveAndDestroyVulkanPipelineCache(vk::Device device, vk::PipelineCache cache)
    {
        if (!cache)
            return;

        // A worker's save still reads the cache: let it finish, without its pending repeat.
        {
            SaveQueue &saves = Saves();
            std::unique_lock<std::mutex> lock(saves.mutex);
            saves.closing = true;
            saves.wake.notify_all();
            saves.wake.wait(lock, [&]
                            { return !saves.running; });
        }

        SaveNow(device, cache);
        device.destroyPipelineCache(cache);
    }

    void SaveVulkanPipelineCacheAsync(vk::Device device, vk::PipelineCache cache)
    {
        if (!cache)
            return;

        SaveQueue &saves = Saves();
        {
            std::lock_guard<std::mutex> lock(saves.mutex);
            if (saves.closing)
                return;

            if (saves.running)
            {
                saves.pending = true;
                return;
            }

            saves.running = true;
        }

        // The worker copies the cache too: vkGetPipelineCacheData needs no external sync on a cache created without
        // VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT, so builds keep running meanwhile.
        ThreadPool::General.Enqueue([device, cache]
                                    { SaveWorker(device, cache); });
    }
} // namespace pe
