/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    class FileSystem
    {
    public:
        FileSystem(const std::string& file, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::ate);

        ~FileSystem();

        inline size_t Size() { return m_size; }

        inline bool IsOpen() { return m_fstream.is_open(); }

        void SetIndexRead(size_t index) { m_fstream.seekg(index); }

        void SetIndexWrite(size_t index) { m_fstream.seekp(index); }

        std::string ReadAll();

        std::string ReadLine();

        void Write(const std::string& data);

        void Write(const char* data, size_t size);

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

        static void AddWatcher(const std::string& file, Func&& callback);

        static const FileWatcher* GetWatcher(StringHash hash);

        static const FileWatcher* GetWatcher(const std::string& file);

        static void RemoveWatcher(const std::string& file);

        static void RemoveWatchers();

        static void Watch();

        static void StartWatching(double interval = 0.25);

        inline static void StopWatching() { s_watching = false; }

        inline static bool Watching() { return s_watching; }

        std::time_t GetFileTime();
        
        void WatchFile();

    private:
        FileWatcher() = default;

        FileWatcher(const std::string& file, Func&& callback);

        std::string m_file;
        std::time_t m_time;
        Func m_callback;

        inline static std::unordered_map<size_t, FileWatcher*> s_watchers{};
        inline static std::atomic_bool s_watching{false};
        inline static std::mutex s_mutex{};
    };
}
