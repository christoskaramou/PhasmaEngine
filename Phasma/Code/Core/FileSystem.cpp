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

#include "Core/FileSystem.h"
#include "Systems/EventSystem.h"

namespace pe
{
    FileSystem::FileSystem(const std::string& file, std::ios_base::openmode mode)
        : m_file(file), m_mode(mode)
    {
        // One of these modes must be set
        if (m_mode & std::ios_base::in || m_mode & std::ios_base::out)
        {
            m_mode |= std::ios_base::ate;
            m_fstream.open(m_file, m_mode);

            if (m_fstream.is_open())
            {
                if (m_mode & std::ios_base::in)
                    m_size = static_cast<size_t>(m_fstream.tellg());
                else
                    m_size = static_cast<size_t>(m_fstream.tellp());
            }
        }
        else
        {
            PE_ERROR("FileSystem: No mode set");
        }
    }

    FileSystem::~FileSystem()
    {
        Close();
    }

    std::string FileSystem::ReadAll()
    {
        SetIndexRead(0);
        std::stringstream ss;
        ss << m_fstream.rdbuf();
        return ss.str();
    }

    std::string FileSystem::ReadLine()
    {
        std::string line;
        std::getline(m_fstream, line);
        return line;
    }

    void FileSystem::Write(const std::string& data)
    {
        m_fstream << data;
    }

    void FileSystem::Write(const char* data, size_t size)
    {
        m_fstream.write(data, size);
    }

    void FileSystem::Close()
    {
        m_fstream.close();
    }

    void FileWatcher::AddWatcher(const std::string& file, Func&& callback)
    {
        if (file.empty() || callback == nullptr)
            PE_ERROR("FileWatcher: Invalid parameters");

        if (!std::filesystem::exists(file))
            PE_ERROR("FileWatcher: File does not exist");

        StringHash hash(file);

        if (GetWatcher(hash) != nullptr)
            return;
        
        std::lock_guard<std::mutex> guard(s_mutex);
        s_watchers[hash] = new FileWatcher(file, std::forward<Func>(callback));
    }

    const FileWatcher* FileWatcher::GetWatcher(StringHash hash)
    {
        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
            return it->second;

        return nullptr;
    }
    
    const FileWatcher* FileWatcher::GetWatcher(const std::string& file)
    {
        StringHash hash(file);
        return GetWatcher(hash);
    }

    void FileWatcher::RemoveWatcher(const std::string& file)
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        StringHash hash(file);
        auto it = s_watchers.find(hash);
        if (it != s_watchers.end())
        {
            delete it->second;
            s_watchers.erase(it);
        }
    }

    void FileWatcher::RemoveWatchers()
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        StopWatching();
        
        for (auto& it : s_watchers)
            delete it.second;
        s_watchers.clear();
    }

    void FileWatcher::Watch()
    {
        std::lock_guard<std::mutex> guard(s_mutex);

        for (auto& watcher : s_watchers)
            watcher.second->WatchFile();
    }

    void FileWatcher::StartWatching(double interval)
    {
        if (s_watching)
            return;

        s_watching = true;
        auto watchLambda = [interval]()
        {
            Timer timer;
            while (s_watching)
            {
                timer.ThreadSleep(interval);
                Watch();
            }
        };

        Queue<Launch::AsyncNoWait>::Request(watchLambda);
    }

    FileWatcher::FileWatcher(const std::string& file, Func&& callback)
     : m_file{file}, m_time{GetFileTime()}, m_callback{callback} {}

    void FileWatcher::WatchFile()
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
