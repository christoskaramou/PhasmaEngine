#include "FileSystem.h"
#include "GamePack.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace pe
{
    FileSystem::FileSystem(const std::string &file, std::ios_base::openmode mode)
        : m_file(file), m_mode(mode | std::ios_base::binary) // Add binary mode
    {
        // One of these modes must be set
        PE_ERROR_IF(!(m_mode & std::ios_base::in) && !(m_mode & std::ios_base::out), "FileSystem: No mode set");

        // Read-only opens in a pack-managed namespace are served from the game pack; a managed
        // path missing from the pack stays closed instead of falling back to loose files.
        if (!(m_mode & std::ios_base::out) && IsGamePackManagedAsset(m_file))
        {
            if (std::optional<std::string> data = ReadGamePackAsset(m_file))
            {
                m_size = data->size();
                m_memStream.str(std::move(*data));
                m_fromPack = true;
            }
            return;
        }

#ifdef _WIN32
        // On Windows, use wide-string path to support unicode filenames
        // The input string is expected to be UTF-8 encoded
        std::filesystem::path filePath(reinterpret_cast<const char8_t *>(m_file.c_str()));
        m_fstream.open(filePath.wstring(), m_mode);
#else
        m_fstream.open(m_file, m_mode);
#endif

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
        content.assign(std::istreambuf_iterator<char>(Stream()), std::istreambuf_iterator<char>());
        return content;
    }

    std::vector<uint8_t> FileSystem::ReadAllBytes()
    {
        SetReadCursor(0);
        std::vector<uint8_t> bytes(m_size);
        Stream().read(reinterpret_cast<char *>(bytes.data()), m_size);
        return bytes;
    }

    std::string FileSystem::ReadLine()
    {
        std::string line;
        std::getline(Stream(), line);
        return line;
    }

    bool FileSystem::Write(const std::string &data)
    {
        m_fstream.write(data.data(), data.size());
        return static_cast<bool>(m_fstream);
    }

    bool FileSystem::Write(const char *data, size_t size)
    {
        m_fstream.write(data, size);
        return static_cast<bool>(m_fstream);
    }

    bool FileSystem::Flush()
    {
        if (!m_fstream.is_open())
            return false;
        m_fstream.flush();
        return static_cast<bool>(m_fstream);
    }

    void FileSystem::Close()
    {
        if (m_fstream.is_open())
            m_fstream.close();
    }

    bool FileSystem::ReplaceFile(const std::filesystem::path &source, const std::filesystem::path &target)
    {
#ifdef _WIN32
        return MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
        std::error_code ec;
        std::filesystem::rename(source, target, ec);
        return !ec;
#endif
    }
} // namespace pe
