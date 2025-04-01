#include "Base/FileSystem.h"

namespace pe
{
    FileSystem::FileSystem(const std::string &file, std::ios_base::openmode mode)
        : m_file(file), m_mode(mode | std::ios_base::binary) // Add binary mode
    {
        // One of these modes must be set
        PE_ERROR_IF(!(m_mode & std::ios_base::in) && !(m_mode & std::ios_base::out), "FileSystem: No mode set");

        m_fstream.open(m_file, m_mode);

        if (m_fstream.is_open())
        {
            if (m_mode & std::ios_base::in)
            {
                auto currentPos = m_fstream.tellg();
                m_fstream.seekg(0, std::ios_base::end);
                m_size = static_cast<size_t>(m_fstream.tellg());
                m_fstream.seekg(currentPos);
            }
            else
            {
                auto currentPos = m_fstream.tellp();
                m_fstream.seekp(0, std::ios_base::end);
                m_size = static_cast<size_t>(m_fstream.tellp());
                m_fstream.seekp(currentPos);
            }
        }
    }

    FileSystem::~FileSystem()
    {
        Close();
    }

    std::string FileSystem::ReadAll()
    {
        SetReadCursor(0);
        std::string content;
        content.reserve(m_size); // Reserve memory for performance improvement
        content.assign(std::istreambuf_iterator<char>(m_fstream), std::istreambuf_iterator<char>());
        return content;
    }

    std::string FileSystem::ReadLine()
    {
        std::string line;
        std::getline(m_fstream, line);
        return line;
    }

    void FileSystem::Write(const std::string &data)
    {
        m_fstream.write(data.data(), data.size());
    }

    void FileSystem::Write(const char *data, size_t size)
    {
        m_fstream.write(data, size);
    }

    void FileSystem::Close()
    {
        m_fstream.close();
    }
}
