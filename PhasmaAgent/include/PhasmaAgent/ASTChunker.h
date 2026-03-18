#pragma once

#include <string>
#include <vector>

namespace pagent
{
    // AST-aware code chunker using tree-sitter.
    // Splits C/C++ files at function, class, struct, enum, and namespace boundaries.
    // Falls back to line-based chunking for unsupported languages or parse failures.
    class ASTChunker
    {
    public:
        struct Symbol
        {
            std::string type; // "function", "class", "struct", "enum", "namespace", "other"
            std::string name; // symbol name (e.g. "CreateBuffer", "RHI")
        };

        struct Chunk
        {
            std::string content;         // The chunk text (no file prefix - caller adds it)
            int startLine = 0;           // 1-based start line
            int endLine = 0;             // 1-based end line (inclusive)
            std::string namespaceName;   // enclosing namespace (e.g. "pe")
            std::vector<Symbol> symbols; // symbols defined in this chunk
        };

        // Build a metadata header line for a chunk, e.g.:
        // "// File: API/RHI.h | Namespace: pe | Class: RHI | Method: CreateBuffer"
        static std::string BuildMetadataHeader(const std::string &relativePath, const Chunk &chunk);

        // Chunk a file's source code into AST-aware pieces.
        // relativePath is used only for the "// File:" prefix in content.
        // maxChars: soft limit per chunk (will not split a single definition).
        // Returns empty if the file can't be parsed or is empty.
        static std::vector<Chunk> ChunkCpp(const std::string &source, int maxChars = 4000);

        // Check if a file extension is supported for AST chunking
        static bool IsSupported(const std::string &extension);
    };
} // namespace pagent
