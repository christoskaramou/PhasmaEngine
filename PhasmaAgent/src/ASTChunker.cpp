#include "PhasmaAgent/ASTChunker.h"
#include <tree_sitter/api.h>
#include <algorithm>
#include <cstring>
#include <set>
#include <string>

// tree-sitter-cpp language (defined in tree_sitter_cpp grammar library)
extern "C" const TSLanguage *tree_sitter_cpp();

namespace pagent
{
    // Extract text from source between byte offsets
    static std::string ExtractText(const std::string &source, uint32_t startByte, uint32_t endByte)
    {
        if (startByte >= source.size())
            return "";
        uint32_t end = std::min(endByte, static_cast<uint32_t>(source.size()));
        return source.substr(startByte, end - startByte);
    }

    // Get the text of a named field child, or empty string
    static std::string GetFieldText(TSNode node, const char *fieldName, const std::string &source)
    {
        TSNode field = ts_node_child_by_field_name(node, fieldName, static_cast<uint32_t>(strlen(fieldName)));
        if (ts_node_is_null(field))
            return "";
        return ExtractText(source, ts_node_start_byte(field), ts_node_end_byte(field));
    }

    // Recursively find the first node of a given type
    static TSNode FindChildByType(TSNode node, const char *type)
    {
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i)
        {
            TSNode child = ts_node_child(node, i);
            if (strcmp(ts_node_type(child), type) == 0)
                return child;
        }
        return ts_node_child(node, count); // null node
    }

    // Extract the function name from a function_definition node.
    // Handles: void Foo(), void Ns::Foo(), Type operator+(), etc.
    static std::string GetFunctionName(TSNode node, const std::string &source)
    {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        if (ts_node_is_null(declarator))
            return "";

        // function_declarator -> declarator field contains the name
        TSNode nameNode = ts_node_child_by_field_name(declarator, "declarator", 10);
        if (ts_node_is_null(nameNode))
            nameNode = declarator;

        // For qualified names like Foo::Bar, get the full text
        return ExtractText(source, ts_node_start_byte(nameNode), ts_node_end_byte(nameNode));
    }

    // Extract symbol info from a top-level AST node
    static ASTChunker::Symbol ExtractSymbol(TSNode node, const std::string &source)
    {
        ASTChunker::Symbol sym;
        const char *type = ts_node_type(node);

        if (strcmp(type, "function_definition") == 0)
        {
            sym.type = "function";
            sym.name = GetFunctionName(node, source);
        }
        else if (strcmp(type, "class_specifier") == 0)
        {
            sym.type = "class";
            sym.name = GetFieldText(node, "name", source);
        }
        else if (strcmp(type, "struct_specifier") == 0)
        {
            sym.type = "struct";
            sym.name = GetFieldText(node, "name", source);
        }
        else if (strcmp(type, "enum_specifier") == 0)
        {
            sym.type = "enum";
            sym.name = GetFieldText(node, "name", source);
        }
        else if (strcmp(type, "namespace_definition") == 0)
        {
            sym.type = "namespace";
            sym.name = GetFieldText(node, "name", source);
        }
        else if (strcmp(type, "template_declaration") == 0)
        {
            // Look inside the template for the actual declaration
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i)
            {
                TSNode child = ts_node_child(node, i);
                const char *childType = ts_node_type(child);
                if (strcmp(childType, "function_definition") == 0 ||
                    strcmp(childType, "class_specifier") == 0 ||
                    strcmp(childType, "struct_specifier") == 0)
                {
                    sym = ExtractSymbol(child, source);
                    if (sym.type == "function")
                        sym.type = "template_function";
                    else if (sym.type == "class")
                        sym.type = "template_class";
                    else if (sym.type == "struct")
                        sym.type = "template_struct";
                    return sym;
                }
            }
            sym.type = "template";
        }

        return sym;
    }

    bool ASTChunker::IsSupported(const std::string &extension)
    {
        static const std::set<std::string> supported = {
            ".cpp", ".cxx", ".cc", ".c", ".h", ".hpp", ".hxx", ".inl"};
        std::string ext = extension;
        for (auto &c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return supported.count(ext) > 0;
    }

    std::string ASTChunker::BuildMetadataHeader(const std::string &relativePath, const Chunk &chunk)
    {
        std::string header = "// File: " + relativePath;
        header += " (lines " + std::to_string(chunk.startLine) + "-" + std::to_string(chunk.endLine) + ")";

        if (!chunk.namespaceName.empty())
            header += " | Namespace: " + chunk.namespaceName;

        for (const auto &sym : chunk.symbols)
        {
            if (sym.name.empty())
                continue;

            // Capitalize first letter of type for display
            std::string displayType = sym.type;
            if (!displayType.empty())
                displayType[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(displayType[0])));

            header += " | " + displayType + ": " + sym.name;
        }

        return header;
    }

    std::vector<ASTChunker::Chunk> ASTChunker::ChunkCpp(const std::string &source, int maxChars)
    {
        if (source.empty())
            return {};

        // Parse with tree-sitter
        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_cpp());

        TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(),
                                              static_cast<uint32_t>(source.size()));
        if (!tree)
        {
            ts_parser_delete(parser);
            return {};
        }

        TSNode root = ts_tree_root_node(tree);
        uint32_t childCount = ts_node_child_count(root);

        // Collect top-level node info
        struct NodeInfo
        {
            uint32_t startByte;
            uint32_t endByte;
            uint32_t startRow; // 0-based
            uint32_t endRow;   // 0-based
            Symbol symbol;
            std::string namespaceName;
        };

        std::vector<NodeInfo> nodes;
        std::string currentNamespace;

        for (uint32_t i = 0; i < childCount; ++i)
        {
            TSNode child = ts_node_child(root, i);
            const char *nodeType = ts_node_type(child);

            NodeInfo info;
            info.startByte = ts_node_start_byte(child);
            info.endByte = ts_node_end_byte(child);
            info.startRow = ts_node_start_point(child).row;
            info.endRow = ts_node_end_point(child).row;
            info.symbol = ExtractSymbol(child, source);

            // Track namespace context
            if (strcmp(nodeType, "namespace_definition") == 0)
                currentNamespace = info.symbol.name;

            info.namespaceName = currentNamespace;
            nodes.push_back(std::move(info));
        }

        ts_tree_delete(tree);
        ts_parser_delete(parser);

        if (nodes.empty())
            return {};

        // Merge nodes into chunks respecting maxChars
        std::vector<Chunk> chunks;
        uint32_t chunkStartByte = nodes[0].startByte;
        uint32_t chunkEndByte = nodes[0].endByte;
        uint32_t chunkStartRow = nodes[0].startRow;
        uint32_t chunkEndRow = nodes[0].endRow;
        std::string chunkNamespace = nodes[0].namespaceName;
        std::vector<Symbol> chunkSymbols;
        if (!nodes[0].symbol.name.empty())
            chunkSymbols.push_back(nodes[0].symbol);

        for (size_t i = 1; i < nodes.size(); ++i)
        {
            uint32_t mergedSize = nodes[i].endByte - chunkStartByte;

            if (static_cast<int>(mergedSize) > maxChars)
            {
                // Flush current chunk
                Chunk c;
                c.content = ExtractText(source, chunkStartByte, chunkEndByte);
                c.startLine = static_cast<int>(chunkStartRow) + 1;
                c.endLine = static_cast<int>(chunkEndRow) + 1;
                c.namespaceName = chunkNamespace;
                c.symbols = std::move(chunkSymbols);
                chunks.push_back(std::move(c));

                // Start new chunk
                chunkStartByte = nodes[i].startByte;
                chunkEndByte = nodes[i].endByte;
                chunkStartRow = nodes[i].startRow;
                chunkEndRow = nodes[i].endRow;
                chunkNamespace = nodes[i].namespaceName;
                chunkSymbols.clear();
                if (!nodes[i].symbol.name.empty())
                    chunkSymbols.push_back(nodes[i].symbol);
            }
            else
            {
                // Merge into current chunk
                chunkEndByte = nodes[i].endByte;
                chunkEndRow = nodes[i].endRow;
                if (!nodes[i].symbol.name.empty())
                    chunkSymbols.push_back(nodes[i].symbol);
                // Use first non-empty namespace
                if (chunkNamespace.empty())
                    chunkNamespace = nodes[i].namespaceName;
            }
        }

        // Flush last chunk
        Chunk c;
        c.content = ExtractText(source, chunkStartByte, chunkEndByte);
        c.startLine = static_cast<int>(chunkStartRow) + 1;
        c.endLine = static_cast<int>(chunkEndRow) + 1;
        c.namespaceName = chunkNamespace;
        c.symbols = std::move(chunkSymbols);
        chunks.push_back(std::move(c));

        return chunks;
    }
} // namespace pagent
