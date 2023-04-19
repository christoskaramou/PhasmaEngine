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

        if (Get(hash) != nullptr)
            return;

        std::lock_guard<std::mutex> guard(s_mutex);
        s_watchers[hash] = new FileWatcher(file, std::forward<Func>(callback));
    }

    const FileWatcher *FileWatcher::Get(StringHash hash)
    {
        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
            return it->second;

        return nullptr;
    }

    const FileWatcher *FileWatcher::Get(const std::string &file)
    {
        return Get(StringHash(file));
    }

    void FileWatcher::Erase(const std::string &file)
    {
        Erase(StringHash(file));
    }

    void FileWatcher::Erase(StringHash hash)
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

    void FileWatcher::Start(double interval)
    {
        if (s_running)
            return;

        auto watchLambda = [interval]()
        {
            Timer timer;
            s_running = true;
            while (s_running)
            {
                timer.ThreadSleep(interval);

                std::lock_guard<std::mutex> guard(s_mutex);
                for (auto &watcher : s_watchers)
                    watcher.second->Watch();
            }
        };

        e_FW_ThreadPool.Enqueue(watchLambda);

        // Watch Shaders folder files
        for (auto &file : std::filesystem::recursive_directory_iterator(Path::Assets + "Shaders"))
        {
            std::string filePath = file.path().string();
            std::replace(filePath.begin(), filePath.end(), '\\', '/');

            // The event id will match with the path id from the shader when created
            EventID event(filePath);
            EventSystem::RegisterEvent(event);

            auto callback = [event]()
            {
                // Single event to notify shader changed events
                // It will trigger Renderer::PollShaders
                // Polled in Window::ProcessEvents
                EventSystem::PushEvent(EventCompileShaders);

                // After Window::ProcessEvents, the specific shader event accurs
                // Easy to poll, we have PipelineCreateInfo::Shader::GetPathID() == EventID
                // Polled in Renderer::PollShaders
                EventSystem::PushEvent(event);
            };

            // Add the callback for this shader file
            Add(filePath, callback);
        }
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

        std::filesystem::file_time_type lastTime = std::filesystem::last_write_time(m_file);
        return std::filesystem::file_time_type::time_point(lastTime).time_since_epoch().count();
    }
}
