#pragma once

namespace pe
{
    // UTF-8 bytes of a path, for argv and log strings. u8string() always succeeds; string() on Windows
    // goes through the active code page and can fail on names outside it.
    inline std::string PathUtf8(const std::filesystem::path &path)
    {
        const auto u8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(u8.c_str()), u8.size());
    }

    // Quote one argv element by the MSVC CRT rules, so the child's argv reproduces it exactly.
    std::wstring QuoteWindowsArg(const std::wstring &arg);

    struct ProcessOptions
    {
        std::filesystem::path workingDirectory; // empty = inherit
        uint32_t timeoutMs = 0;                 // 0 = wait forever; on expiry the child is killed
        bool captureOutput = false;             // stdout + stderr into ProcessResult::output
        bool detach = false;                    // return as soon as the child is running
    };

    struct ProcessResult
    {
        bool started = false;
        bool timedOut = false;
        int exitCode = -1;
        std::string output;
    };

    // The one place the engine spawns a child. Never a shell: `exe` is an executable path, or a bare
    // name resolved on PATH, and `args` (UTF-8) reaches the child as a real argv - on Windows as a
    // CRT-quoted command line behind an explicit application path.
    ProcessResult RunProcess(const std::filesystem::path &exe,
                             const std::vector<std::string> &args,
                             const ProcessOptions &options = {});
} // namespace pe
