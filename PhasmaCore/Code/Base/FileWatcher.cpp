#include "Base/FileWatcher.h"

namespace pe
{
    void FileWatcher::Add(const std::string &file, Func &&callback)
    {
        PE_ERROR_IF(file.empty() || callback == nullptr, "FileWatcher: Invalid parameters");
        PE_ERROR_IF(!std::filesystem::exists(file), "FileWatcher: File does not exist");

        std::string filePath = file;
        std::replace(filePath.begin(), filePath.end(), '\\', '/');

        StringHash hash(filePath);
        if (FileWatcher::Get(hash))
            return;

        std::lock_guard<std::mutex> guard(s_mutex);
        EventSystem::RegisterEvent(static_cast<size_t>(hash));
        s_watchers[hash] = new FileWatcher(filePath, std::forward<Func>(callback));
    }

    const FileWatcher *FileWatcher::Get(size_t hash)
    {
        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
            return it->second;

        return nullptr;
    }

    const FileWatcher *FileWatcher::Get(const std::string &file)
    {
        return FileWatcher::Get(StringHash(file));
    }

    void FileWatcher::Erase(const std::string &file)
    {
        FileWatcher::Erase(StringHash(file));
    }

    void FileWatcher::Erase(size_t hash)
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
        {
            delete it->second;
            s_watchers.erase(it);
        }
    }

    void FileWatcher::Clear()
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        for (auto &it : s_watchers)
            delete it.second;
        s_watchers.clear();
    }

    size_t FileWatcher::GetFileEvent(const std::string &file)
    {
        const FileWatcher *watcher = FileWatcher::Get(file);
        if (!watcher)
            return 0;

        return watcher->GetHash();
    }

    void FileWatcher::WatchFiles()
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        for (auto &watcher : s_watchers)
            watcher.second->Watch();
    }

    void FileWatcher::Start(double interval)
    {
        if (s_running)
            return;

        s_running = true;
        FileWatcher::WatchFiles();

        auto callback = [interval]()
        {
            Timer timer;
            while (s_running)
            {
                FileWatcher::WatchFiles();
                timer.ThreadSleep(interval);
            }
        };

        ThreadPool::FW.Enqueue(callback);
    }

    FileWatcher::FileWatcher(const std::string &file, Func &&callback)
        : m_hash{StringHash{file}}, m_file{file}, m_time{GetFileTime()}, m_callback{callback} {}

    void FileWatcher::Watch()
    {
        std::time_t time = GetFileTime();
        if (m_time != time)
        {
            m_time = time;
            m_callback(m_hash);
        }
    }

    // returns the last time the file was modified
    std::time_t FileWatcher::GetFileTime()
    {
        if (m_file.empty())
            return 0;

        return std::filesystem::last_write_time(m_file).time_since_epoch().count();
    }
} // namespace pe
