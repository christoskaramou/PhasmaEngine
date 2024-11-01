#include "Core/FileWatcher.h"

namespace pe
{
    void FileWatcher::Add(const std::string &file, Func &&callback)
    {
        if (file.empty() || callback == nullptr)
            PE_ERROR("FileWatcher: Invalid parameters");

        if (!std::filesystem::exists(file))
            PE_ERROR("FileWatcher: File does not exist");

        StringHash hash(file);

        if (FileWatcher::Get(hash) != nullptr)
            return;

        std::lock_guard<std::mutex> guard(s_mutex);
        s_watchers[hash] = new FileWatcher(file, std::forward<Func>(callback));
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

    void FileWatcher::CheckFiles()
    {
        // Add "Shaders" folder files
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Shaders"))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            StringHash filePathHash(filePath);
            if (!FileWatcher::Get(filePathHash))
            {
                EventSystem::RegisterEvent(filePathHash);

                auto callback = [filePathHash]()
                {
                    EventSystem::PushEvent(filePathHash);
                    EventSystem::PushEvent(EventCompileShaders);
                };

                FileWatcher::Add(filePath, callback);
            }
        }

        // Add "Scripts" folder files
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Scripts"))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            StringHash filePathHash(filePath);

            if (!FileWatcher::Get(filePathHash))
            {
                EventSystem::RegisterEvent(filePathHash);

                auto callback = [filePathHash]()
                {
                    EventSystem::PushEvent(filePathHash);
                    EventSystem::PushEvent(EventCompileScripts);
                };

                FileWatcher::Add(filePath, callback);
            }
        }
    }

    void FileWatcher::WatchFiles()
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        for (auto &watcher : s_watchers)
            watcher.second->Watch();
    }

    void FileWatcher::Start()
    {
        if (s_running)
            return;

        s_running = true;
        FileWatcher::CheckFiles();
        FileWatcher::WatchFiles();

        auto callback = []()
        {
            Timer timer;
            while (s_running)
            {
                FileWatcher::CheckFiles();
                FileWatcher::WatchFiles();
                timer.ThreadSleep(1.0);
            }
        };

        e_FW_ThreadPool.Enqueue(callback);
    }

    FileWatcher::FileWatcher(const std::string &file, Func &&callback)
        : m_file{file}, m_time{GetFileTime()}, m_callback{callback} {}

    void FileWatcher::Watch()
    {
        std::time_t time = GetFileTime();
        if (m_time != time)
        {
            m_time = time;
            m_callback();
        }
    }

    // returns the last time the file was modified
    std::time_t FileWatcher::GetFileTime()
    {
        if (m_file.empty())
            return 0;

        return std::filesystem::last_write_time(m_file).time_since_epoch().count();
    }
}
