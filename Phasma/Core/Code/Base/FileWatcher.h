#pragma once

namespace pe
{
    class FileWatcher
    {
    public:
        using Func = std::function<void(size_t fileEvent)>;

        static size_t GetFileEvent(const std::string &file);
        static void Add(const std::string &file, Func &&callback);
        static const FileWatcher *Get(size_t hash);
        static const FileWatcher *Get(const std::string &file);
        static void Erase(size_t hash);
        static void Erase(const std::string &file);
        static void Clear();
        static void SetEnabled(bool enabled);
        inline static bool IsEnabled() { return s_enabled; }
        static void Start(double interval = 1.0);
        inline static void Stop() { s_running = false; }
        static void StopAndJoin();
        inline static bool IsRunning() { return s_running; }

        static std::shared_ptr<FileWatcher> Create(const std::string &file, Func &&callback);

        void Watch();
        std::time_t GetFileTime();
        inline std::string GetFile() const { return m_file; }
        inline Func GetCallback() const { return m_callback; }
        inline size_t GetHash() const { return m_hash; }
        ~FileWatcher();

    private:
        FileWatcher() = default;
        FileWatcher(const std::string &file, Func &&callback);

        inline static void UpdateFolders();
        inline static void WatchFiles();

        size_t m_hash;
        std::string m_file;
        std::time_t m_time;
        Func m_callback;
        uintptr_t m_dirNotify = 0;

        inline static std::vector<std::string> s_files{};
        inline static std::vector<std::string> s_folders{};
        inline static std::unordered_map<size_t, std::shared_ptr<FileWatcher>> s_watchers{};
        inline static std::atomic_bool s_enabled{true};
        inline static std::atomic_bool s_running{false};
        inline static std::mutex s_mutex{};
    };
} // namespace pe
