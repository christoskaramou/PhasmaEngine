#pragma once

namespace pe
{
    class FileWatcher
    {
    public:
        using Func = std::function<void()>;

        static void Add(const std::string &file, Func &&callback);
        static const FileWatcher *Get(size_t hash);
        static const FileWatcher *Get(const std::string &file);
        static void Erase(size_t hash);
        static void Erase(const std::string &file);
        static void Clear();
        static void Start();
        inline static void Stop() { s_running = false; }
        inline static bool IsRunning() { return s_running; }

        void Watch();
        std::time_t GetFileTime();
        inline std::string GetFile() { return m_file; };
        inline Func GetCallback() { return m_callback; }

    private:
        FileWatcher() = default;
        FileWatcher(const std::string &file, Func &&callback);
        
        inline static void CheckFiles();
        inline static void WatchFiles();

        std::string m_file;
        std::time_t m_time;
        Func m_callback;

        inline static std::unordered_map<size_t, FileWatcher *> s_watchers{};
        inline static std::atomic_bool s_running{false};
        inline static std::mutex s_mutex{};
    };
}
