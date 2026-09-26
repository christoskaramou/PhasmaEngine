#pragma once
#include <filesystem>
#include <string>

// Engine-independent helpers for C++ script references ("cpp:Name" or a .cpp source path).
namespace pe
{
    inline bool IsCppScriptPath(const std::string &path)
    {
        return path.rfind("cpp:", 0) == 0 || std::filesystem::path(path).extension() == ".cpp";
    }

    // Filename of a source path; splits on both separators so Windows paths resolve on any host.
    inline std::string CppSourceName(const std::string &path)
    {
        const auto slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Lookup key: the script name of "cpp:Name", otherwise the source filename.
    inline std::string CppScriptKey(const std::string &path)
    {
        return path.rfind("cpp:", 0) == 0 ? path.substr(4) : CppSourceName(path);
    }

    // What scenes store: a .cpp path becomes its bare filename; "cpp:" and non-C++ paths are unchanged.
    inline std::string CppScriptReference(const std::string &path)
    {
        return path.rfind("cpp:", 0) != 0 && IsCppScriptPath(path) ? CppSourceName(path) : path;
    }

    inline bool MatchesCppScript(const std::string &path, const char *name, const char *sourceFile)
    {
        return (name && path == std::string("cpp:") + name) ||
               (sourceFile && CppScriptKey(path) == CppSourceName(sourceFile));
    }

    // Descriptors carry only a filename: find it under the sources directory (recursive; basenames
    // are unique by contract). Empty when missing or not a .cpp reference.
    inline std::string FindCppSource(const std::filesystem::path &directory, const std::string &reference)
    {
        const std::filesystem::path name = CppSourceName(reference);
        if (reference.rfind("cpp:", 0) == 0 || name.extension() != ".cpp")
            return {};
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
            if (it->path().filename() == name && it->is_regular_file(ec))
                return it->path().string();
        return {};
    }
} // namespace pe
