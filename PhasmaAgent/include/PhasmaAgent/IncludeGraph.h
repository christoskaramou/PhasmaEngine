#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace pagent
{
    // Lightweight bidirectional include-graph for C/C++ files.
    // Parses #include "..." directives and builds an adjacency list.
    // At query time, returns 1-hop neighbors for context expansion.
    class IncludeGraph
    {
    public:
        // Build the graph from a list of absolute file paths.
        // commonRoot is stripped from paths to produce relative keys.
        void Build(const std::vector<std::string> &files, const std::string &commonRoot);

        // Build the graph by scanning directories for C/C++ files.
        void BuildFromDirectories(const std::vector<std::string> &directories,
                                  const std::vector<std::string> &skipDirectories = {},
                                  const std::vector<std::string> &skipExtensions = {});

        // Get 1-hop neighbors of a file (both includes and included-by).
        // filePath should be a relative path matching the keys used during Build().
        std::vector<std::string> GetNeighbors(const std::string &filePath) const;

        // Number of files in the graph.
        int Size() const { return static_cast<int>(m_adj.size()); }

    private:
        // Bidirectional adjacency: file -> set of neighbor files
        std::unordered_map<std::string, std::unordered_set<std::string>> m_adj;

        // Map from filename (e.g. "RHI.h") to all relative paths containing that filename
        std::unordered_map<std::string, std::vector<std::string>> m_nameToPath;
    };
} // namespace pagent
