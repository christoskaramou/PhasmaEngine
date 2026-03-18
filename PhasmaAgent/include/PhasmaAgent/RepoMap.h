#pragma once

#include <string>
#include <vector>

namespace pagent
{
    // Generates a compact overview of a codebase (~1K tokens) by extracting
    // class, struct, and function signatures from C/C++ files using tree-sitter.
    // Inspired by Aider's repository map approach.
    class RepoMap
    {
    public:
        struct Config
        {
            std::vector<std::string> directories;
            std::vector<std::string> skip_directories;
            std::vector<std::string> skip_files;
            std::vector<std::string> skip_extensions;
            int max_chars = 4000; // ~1K tokens
        };

        // Generate the repo map string. Scans files, extracts signatures, truncates to max_chars.
        static std::string Generate(const Config &config);
    };
} // namespace pagent
