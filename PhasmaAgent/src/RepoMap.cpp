#include "PhasmaAgent/RepoMap.h"
#include "PhasmaAgent/ASTChunker.h"
#include <tree_sitter/api.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <set>
#include <map>
#include <string>

extern "C" const TSLanguage *tree_sitter_cpp();

namespace pagent
{
    namespace fs = std::filesystem;

    static std::string toLowerStr(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // Extract the signature (first line up to '{' or ';') from a source range
    static std::string ExtractSignature(const std::string &source, uint32_t startByte, uint32_t endByte)
    {
        std::string sig;
        for (uint32_t i = startByte; i < endByte && i < source.size(); ++i)
        {
            char c = source[i];
            if (c == '{')
                break;
            if (c == '\n')
            {
                // For multi-line signatures (e.g. template + function), keep going
                // but stop at blank lines
                if (!sig.empty() && sig.back() == '\n')
                    break;
            }
            sig += c;
        }
        // Trim trailing whitespace
        while (!sig.empty() && (sig.back() == ' ' || sig.back() == '\t' || sig.back() == '\n' || sig.back() == '\r'))
            sig.pop_back();
        return sig;
    }

    // Extract signatures from a single file
    static void ExtractFileSignatures(const std::string &source, const std::string &relPath,
                                      std::map<std::string, std::vector<std::string>> &fileSignatures)
    {
        if (source.empty())
            return;

        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_cpp());

        TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(),
                                              static_cast<uint32_t>(source.size()));
        if (!tree)
        {
            ts_parser_delete(parser);
            return;
        }

        TSNode root = ts_tree_root_node(tree);
        uint32_t childCount = ts_node_child_count(root);

        // Types we want signatures for
        static const std::set<std::string> sigTypes = {
            "function_definition",
            "class_specifier",
            "struct_specifier",
            "enum_specifier",
            "namespace_definition",
            "template_declaration",
        };

        std::vector<std::string> sigs;

        // Walk top-level nodes, and one level into namespaces
        auto extractNode = [&](TSNode node, const std::string &nsPrefix)
        {
            const char *type = ts_node_type(node);
            if (sigTypes.count(type) == 0)
                return;

            // For namespaces, recurse into children
            if (strcmp(type, "namespace_definition") == 0)
            {
                TSNode nameNode = ts_node_child_by_field_name(node, "name", 4);
                std::string nsName = nsPrefix;
                if (!ts_node_is_null(nameNode))
                {
                    uint32_t s = ts_node_start_byte(nameNode);
                    uint32_t e = ts_node_end_byte(nameNode);
                    nsName = source.substr(s, e - s);
                }

                // Walk namespace body children
                TSNode body = ts_node_child_by_field_name(node, "body", 4);
                if (!ts_node_is_null(body))
                {
                    uint32_t bodyCount = ts_node_child_count(body);
                    for (uint32_t i = 0; i < bodyCount; ++i)
                    {
                        TSNode child = ts_node_child(body, i);
                        const char *childType = ts_node_type(child);
                        if (sigTypes.count(childType) == 0)
                            continue;

                        std::string sig = ExtractSignature(source, ts_node_start_byte(child), ts_node_end_byte(child));
                        if (!sig.empty() && sig.size() > 3)
                        {
                            if (!nsName.empty())
                                sigs.push_back("  [" + nsName + "] " + sig);
                            else
                                sigs.push_back("  " + sig);
                        }
                    }
                }
                return;
            }

            std::string sig = ExtractSignature(source, ts_node_start_byte(node), ts_node_end_byte(node));
            if (!sig.empty() && sig.size() > 3)
                sigs.push_back("  " + sig);
        };

        for (uint32_t i = 0; i < childCount; ++i)
            extractNode(ts_node_child(root, i), "");

        ts_tree_delete(tree);
        ts_parser_delete(parser);

        if (!sigs.empty())
            fileSignatures[relPath] = std::move(sigs);
    }

    std::string RepoMap::Generate(const Config &config)
    {
        // Build skip sets
        std::set<std::string> skipDirs;
        for (auto d : config.skip_directories)
        {
            for (auto &ch : d)
                if (ch == '\\')
                    ch = '/';
            while (!d.empty() && d.back() == '/')
                d.pop_back();
            skipDirs.insert(toLowerStr(d));
        }

        std::set<std::string> skipFiles;
        for (const auto &f : config.skip_files)
            skipFiles.insert(toLowerStr(f));

        std::set<std::string> skipExts;
        for (const auto &e : config.skip_extensions)
            skipExts.insert(toLowerStr(e));

        // Determine common root
        std::string commonRoot;
        if (!config.directories.empty())
        {
            fs::path root = fs::path(config.directories[0]).parent_path();
            commonRoot = root.string();
            for (auto &ch : commonRoot)
                if (ch == '\\')
                    ch = '/';
            if (!commonRoot.empty() && commonRoot.back() != '/')
                commonRoot += '/';
        }

        // Scan and extract signatures
        std::map<std::string, std::vector<std::string>> fileSignatures;

        for (const auto &dir : config.directories)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                continue;

            for (auto it = fs::recursive_directory_iterator(dir, ec);
                 it != fs::recursive_directory_iterator(); ++it)
            {
                if (it->is_directory())
                {
                    auto u8dir = it->path().u8string();
                    std::string dirPath(u8dir.begin(), u8dir.end());
                    for (auto &ch : dirPath)
                        if (ch == '\\')
                            ch = '/';
                    while (!dirPath.empty() && dirPath.back() == '/')
                        dirPath.pop_back();
                    if (skipDirs.count(toLowerStr(dirPath)))
                    {
                        it.disable_recursion_pending();
                        continue;
                    }
                }

                if (!it->is_regular_file())
                    continue;

                const auto &p = it->path();
                auto u8ext = p.extension().u8string();
                std::string ext = toLowerStr(std::string(u8ext.begin(), u8ext.end()));

                if (!skipExts.empty() && skipExts.count(ext))
                    continue;

                // Only process C/C++ files
                if (!ASTChunker::IsSupported(ext))
                    continue;

                auto u8name = p.filename().u8string();
                std::string fileName(u8name.begin(), u8name.end());
                if (skipFiles.count(toLowerStr(fileName)))
                    continue;

                auto u8path = p.u8string();
                std::string filePath(u8path.begin(), u8path.end());

                // Compute relative path
                std::string rel = filePath;
                for (auto &ch : rel)
                    if (ch == '\\')
                        ch = '/';
                if (!commonRoot.empty() && rel.find(commonRoot) == 0)
                    rel = rel.substr(commonRoot.size());

                // Read and extract
                std::ifstream f(p);
                if (!f.is_open())
                    continue;
                std::string source((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());

                ExtractFileSignatures(source, rel, fileSignatures);
            }
        }

        // Build the map string, sorted by file path
        std::string result;
        result.reserve(config.max_chars + 256);
        result += "# Codebase Overview\n";

        for (const auto &[file, sigs] : fileSignatures)
        {
            if (static_cast<int>(result.size()) >= config.max_chars)
                break;

            std::string fileBlock = file + ":\n";
            for (const auto &sig : sigs)
                fileBlock += sig + "\n";

            // Skip if adding this file would exceed limit
            if (static_cast<int>(result.size() + fileBlock.size()) > config.max_chars)
                continue;

            result += fileBlock;
        }

        return result;
    }
} // namespace pagent
