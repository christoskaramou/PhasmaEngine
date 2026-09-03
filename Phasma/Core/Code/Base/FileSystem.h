#pragma once

namespace pe
{
    class FileSystem
    {
    public:
        FileSystem(const std::string &file, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::ate);
        ~FileSystem();

        inline size_t Size() { return m_size; }
        inline bool IsOpen() { return m_fromPack || m_fstream.is_open(); }
        inline void SetReadCursor(size_t index) { Stream().seekg(index); }
        inline void SetWriteCursor(size_t index) { Stream().seekp(index); }
        inline size_t GetReadCursor() { return Stream().tellg(); }
        inline size_t GetWriteCursor() { return Stream().tellp(); }
        inline bool EndOfFile() { return Stream().eof(); }
        std::string ReadAll();
        std::vector<uint8_t> ReadAllBytes();
        std::string ReadLine();
        bool Write(const std::string &data);
        bool Write(const char *data, size_t size);
        bool Flush();
        void Close();
        static bool ReplaceFile(const std::filesystem::path &source, const std::filesystem::path &target);

    private:
        inline std::iostream &Stream() { return m_fromPack ? static_cast<std::iostream &>(m_memStream) : m_fstream; }

        std::string m_file;
        std::fstream m_fstream;
        std::stringstream m_memStream; // read-only view served from the game pack
        bool m_fromPack = false;
        std::ios_base::openmode m_mode;
        size_t m_size = 0;
    };
} // namespace pe
