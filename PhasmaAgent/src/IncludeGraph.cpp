#include "PhasmaAgent/IncludeGraph.h"
#include <fstream>
#include <filesystem>
#include <regex>

namespace pagent
{
    namespace fs = std::filesystem;

    static std::string toLowerIG(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static std::string normalizePath(std::string p)
    {
        for (auto &c : p)
            if (c == '\\')
                c = '/';
        return p;
    }

    void IncludeGraph::Build(const std::vector<std::string> &files, const std::string &commonRoot)
    {
        m_adj.clear();
        m_nameToPath.clear();

        std::string root = normalizePath(commonRoot);
        if (!root.empty() && root.back() != '/')
            root += '/';

        // First pass: build name-to-relative-path map
        std::vector<std::string> relPaths;
        relPaths.reserve(files.size());

        for (const auto &f : files)
        {
            std::string norm = normalizePath(f);
            std::string rel = norm;
            if (!root.empty() && rel.find(root) == 0)
                rel = rel.substr(root.size());

            relPaths.push_back(rel);

            // Extract filename for fuzzy resolution
            auto slashPos = rel.rfind('/');
            std::string name = (slashPos != std::string::npos) ? rel.substr(slashPos + 1) : rel;
            m_nameToPath[toLowerIG(name)].push_back(rel);

            // Ensure every file has an adjacency entry
            m_adj[rel];
        }

        // Second pass: parse #include directives and build edges
        static const std::regex includeRe(R"re(^\s*#\s*include\s*"([^"]+)")re");

        for (size_t i = 0; i < files.size(); ++i)
        {
            const auto &filePath = files[i];
            const auto &rel = relPaths[i];

            std::ifstream f(fs::path(std::u8string(filePath.begin(), filePath.end())));
            if (!f.is_open())
                continue;

            // Only scan first 200 lines for includes (they're always at the top)
            std::string line;
            int lineCount = 0;
            while (std::getline(f, line) && lineCount < 200)
            {
                ++lineCount;
                std::smatch match;
                if (!std::regex_search(line, match, includeRe))
                    continue;

                std::string includePath = normalizePath(match[1].str());

                // Try to resolve the include to a known file:
                // 1. Try relative to the including file's directory
                std::string resolved;
                auto dirSlash = rel.rfind('/');
                if (dirSlash != std::string::npos)
                {
                    std::string candidate = rel.substr(0, dirSlash + 1) + includePath;
                    if (m_adj.count(candidate))
                        resolved = candidate;
                }

                // 2. Try as-is (relative to root)
                if (resolved.empty() && m_adj.count(includePath))
                    resolved = includePath;

                // 3. Fuzzy: match by filename only
                if (resolved.empty())
                {
                    auto nameSlash = includePath.rfind('/');
                    std::string incName = (nameSlash != std::string::npos) ? includePath.substr(nameSlash + 1) : includePath;
                    auto it = m_nameToPath.find(toLowerIG(incName));
                    if (it != m_nameToPath.end() && it->second.size() == 1)
                        resolved = it->second[0];
                }

                if (resolved.empty() || resolved == rel)
                    continue;

                // Add bidirectional edge
                m_adj[rel].insert(resolved);
                m_adj[resolved].insert(rel);
            }
        }
    }

    static bool isCppExtension(const std::string &ext)
    {
        static const std::unordered_set<std::string> exts = {
            ".h", ".hpp", ".hxx", ".c", ".cpp", ".cxx", ".cc", ".inl", ".hlsl", ".hlsli"};
        return exts.count(toLowerIG(ext)) > 0;
    }

    void IncludeGraph::BuildFromDirectories(const std::vector<std::string> &directories,
                                            const std::vector<std::string> &skipDirectories,
                                            const std::vector<std::string> &skipExtensions)
    {
        // Build skip sets
        std::unordered_set<std::string> skipDirs;
        for (auto d : skipDirectories)
        {
            d = normalizePath(d);
            while (!d.empty() && d.back() == '/')
                d.pop_back();
            skipDirs.insert(toLowerIG(d));
        }

        std::unordered_set<std::string> skipExts;
        for (const auto &e : skipExtensions)
            skipExts.insert(toLowerIG(e));

        // Determine common root
        std::string commonRoot;
        if (!directories.empty())
        {
            auto u8root = fs::path(directories[0]).parent_path().u8string();
            commonRoot = normalizePath(std::string(u8root.begin(), u8root.end()));
            if (!commonRoot.empty() && commonRoot.back() != '/')
                commonRoot += '/';
        }

        // Scan for C/C++ files
        std::vector<std::string> files;
        for (const auto &dir : directories)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                continue;

            for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); ++it)
            {
                if (it->is_directory() && !skipDirs.empty())
                {
                    auto u8dp = it->path().u8string();
                    std::string dp = normalizePath(std::string(u8dp.begin(), u8dp.end()));
                    while (!dp.empty() && dp.back() == '/')
                        dp.pop_back();
                    if (skipDirs.count(toLowerIG(dp)))
                    {
                        it.disable_recursion_pending();
                        continue;
                    }
                }

                if (!it->is_regular_file())
                    continue;

                auto u8ext = it->path().extension().u8string();
                std::string ext(u8ext.begin(), u8ext.end());
                if (skipExts.count(toLowerIG(ext)))
                    continue;
                if (!isCppExtension(ext))
                    continue;

                auto u8 = it->path().u8string();
                files.emplace_back(u8.begin(), u8.end());
            }
        }

        Build(files, commonRoot);
    }

    std::vector<std::string> IncludeGraph::GetNeighbors(const std::string &filePath) const
    {
        auto it = m_adj.find(filePath);
        if (it == m_adj.end())
            return {};
        return {it->second.begin(), it->second.end()};
    }
} // namespace pagent
