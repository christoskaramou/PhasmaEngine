#if defined(PE_WIN32)
#include <windows.h>
#endif

namespace pe
{
    namespace
    {
#if defined(PE_WIN32)
        std::wstring Utf8ToWide(const std::string &utf8)
        {
            if (utf8.empty())
                return {};

            const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
            if (size <= 0)
                return {};

            std::wstring wide(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), wide.data(), size);
            return wide;
        }

        void CloseDirNotify(uintptr_t &handle)
        {
            if (!handle)
                return;
            FindCloseChangeNotification(reinterpret_cast<HANDLE>(handle));
            handle = 0;
        }

        uintptr_t OpenDirNotify(const std::string &file)
        {
            std::filesystem::path parent = std::filesystem::path(file).parent_path();
            if (parent.empty())
                parent = ".";

            std::error_code ec;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(parent, ec);
            const std::wstring dir = Utf8ToWide(ec ? parent.string() : canonical.string());
            if (dir.empty())
                return 0;

            HANDLE notify = FindFirstChangeNotificationW(
                dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME);
            return notify == INVALID_HANDLE_VALUE ? 0 : reinterpret_cast<uintptr_t>(notify);
        }
#endif
    } // namespace

    void FileWatcher::Add(const std::string &file, Func &&callback)
    {
        PE_ERROR_IF(file.empty() || callback == nullptr, "FileWatcher: Invalid parameters");
        if (!s_enabled)
            return;

        PE_ERROR_IF(!std::filesystem::exists(file), "FileWatcher: File does not exist");

        std::error_code ec;
        std::string filePath = std::filesystem::weakly_canonical(file, ec).generic_string();

        StringHash hash(filePath);
        if (FileWatcher::Get(hash))
            return;

        std::lock_guard<std::mutex> guard(s_mutex);
        if (s_watchers.find(hash) != s_watchers.end())
            return;

        EventSystem::RegisterEvent(static_cast<size_t>(hash));
        s_watchers[hash] = FileWatcher::Create(filePath, std::forward<Func>(callback));
    }

    const FileWatcher *FileWatcher::Get(size_t hash)
    {
        std::lock_guard<std::mutex> guard(s_mutex);
        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
            return it->second.get();

        return nullptr;
    }

    const FileWatcher *FileWatcher::Get(const std::string &file)
    {
        std::error_code ec;
        return FileWatcher::Get(StringHash(std::filesystem::weakly_canonical(file, ec).generic_string()));
    }

    void FileWatcher::Erase(const std::string &file)
    {
        std::error_code ec;
        FileWatcher::Erase(StringHash(std::filesystem::weakly_canonical(file, ec).generic_string()));
    }

    void FileWatcher::Erase(size_t hash)
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        s_watchers.erase(hash);
    }

    void FileWatcher::Clear()
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        s_watchers.clear();
    }

    void FileWatcher::SetEnabled(bool enabled)
    {
        const bool wasEnabled = s_enabled.exchange(enabled);
        if (wasEnabled && !enabled)
        {
            StopAndJoin();
            Clear();
        }
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
        if (!s_enabled)
            return;

        if (s_running)
            return;

        s_running = true;
        FileWatcher::WatchFiles();

        auto callback = [interval]()
        {
            Timer timer;
            while (s_running)
            {
#if defined(PE_WIN32)
                std::vector<HANDLE> handles;
                std::vector<std::shared_ptr<FileWatcher>> watchers;
                {
                    std::lock_guard<std::mutex> guard(s_mutex);
                    handles.reserve(s_watchers.size());
                    watchers.reserve(s_watchers.size());
                    for (auto &entry : s_watchers)
                    {
                        if (!entry.second->m_dirNotify)
                            continue;
                        handles.push_back(reinterpret_cast<HANDLE>(entry.second->m_dirNotify));
                        watchers.push_back(entry.second);
                    }
                }

                if (!handles.empty())
                {
                    const DWORD timeoutMs = interval <= 0.0 ? INFINITE : static_cast<DWORD>(interval * 1000.0);
                    const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE,
                                                              timeoutMs);
                    if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + handles.size())
                    {
                        const std::shared_ptr<FileWatcher> &watcher = watchers[wait - WAIT_OBJECT_0];
                        FindNextChangeNotification(reinterpret_cast<HANDLE>(watcher->m_dirNotify));
                        watcher->Watch();
                        continue;
                    }
                }
#endif
                FileWatcher::WatchFiles();
                timer.ThreadSleep(interval);
            }
        };

        ThreadPool::FW.Enqueue(callback);
    }

    void FileWatcher::StopAndJoin()
    {
        s_running = false;
        ThreadPool::FW.WaitIdle();
    }

    std::shared_ptr<FileWatcher> FileWatcher::Create(const std::string &file, Func &&callback)
    {
        return std::shared_ptr<FileWatcher>(new FileWatcher(file, std::forward<Func>(callback)));
    }

    FileWatcher::FileWatcher(const std::string &file, Func &&callback)
        : m_hash{StringHash{file}}, m_file{file}, m_time{GetFileTime()}, m_callback{callback}
    {
#if defined(PE_WIN32)
        m_dirNotify = OpenDirNotify(file);
#endif
    }

    FileWatcher::~FileWatcher()
    {
#if defined(PE_WIN32)
        CloseDirNotify(m_dirNotify);
#endif
    }

    void FileWatcher::Watch()
    {
        std::time_t time = GetFileTime();
        if (m_time != time)
        {
            m_time = time;
            m_callback(m_hash);
        }
    }

    std::time_t FileWatcher::GetFileTime()
    {
        if (m_file.empty())
            return 0;

        std::error_code ec;
        return std::filesystem::last_write_time(m_file, ec).time_since_epoch().count();
    }
} // namespace pe
