#pragma once

namespace pe
{
    class FileSystem
    {
    public:
        FileSystem(const std::string &file, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::ate);

        ~FileSystem();

        inline size_t Size() { return m_size; }

        inline bool IsOpen() { return m_fstream.is_open(); }

        inline void SetReadCursor(size_t index) { m_fstream.seekg(index); }

        inline void SetWriteCursor(size_t index) { m_fstream.seekp(index); }

        inline size_t GetReadCursor() { return m_fstream.tellg(); }

        inline size_t GetWriteCursor() { return m_fstream.tellp(); }

        inline bool EndOfFile() { return m_fstream.eof(); }

        std::string ReadAll();

        std::string ReadLine();

        void Write(const std::string &data);

        void Write(const char *data, size_t size);

        void Close();

    private:
        std::string m_file;
        std::fstream m_fstream;
        std::ios_base::openmode m_mode;
        size_t m_size;
    };

    class FileWatcher
    {
    public:
        using Func = std::function<void()>;

        static void Add(const std::string &file, Func &&callback);

        static const FileWatcher *Get(StringHash hash);

        static const FileWatcher *Get(const std::string &file);

        static void Erase(StringHash hash);

        static void Erase(const std::string &file);

        static void Clear();

        static void Start(double interval);

        inline static void Stop() { s_running = false; }

        inline static bool IsRunning() { return s_running; }

        void Watch();

        std::time_t GetFileTime();

        inline std::string GetFile() { return m_file; };

        inline Func GetCallback() { return m_callback; }

    private:
        FileWatcher() = default;

        FileWatcher(const std::string &file, Func &&callback);

        std::string m_file;
        std::time_t m_time;
        Func m_callback;

        inline static std::unordered_map<size_t, FileWatcher *> s_watchers{};
        inline static std::atomic_bool s_running{false};
        inline static std::mutex s_mutex{};
    };
}
