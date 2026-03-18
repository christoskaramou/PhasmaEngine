#pragma once

#include "PhasmaAgent/Agent.h"
#include "PhasmaAgent/VectorStore.h"
#include "PhasmaAgent/BM25Index.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <set>

namespace pagent
{
    struct IndexerConfig
    {
        std::vector<std::string> directories;
        std::vector<std::string> include_files;      // Individual files to index (bypass skip rules)
        std::vector<std::string> skip_directories;   // Directory names to skip during scanning
        std::vector<std::string> skip_files;         // File names to skip (e.g. "package-lock.json")
        std::vector<std::string> extensions;         // Empty = index all text files
        std::vector<std::string> skip_extensions = { // Binary formats — path-only entry, no content
            ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".exr", ".ktx", ".ktx2", ".dds",
            ".obj", ".fbx", ".gltf", ".glb", ".stl", ".ply", ".dae",
            ".ttf", ".otf", ".woff", ".woff2",
            ".wav", ".mp3", ".ogg", ".flac",
            ".zip", ".tar", ".gz", ".7z", ".rar",
            ".exe", ".dll", ".so", ".dylib", ".lib", ".a", ".o", ".pdb",
            ".spv", ".dxo", ".cso"};
        std::vector<std::string> skip_regex; // Regex patterns to skip paths (e.g. ".*\\.generated\\..*")
        int max_chunk_chars = 4000;
        int chunk_overlap_lines = 3;
        int num_threads = 0;              // Parallel embedding threads (0 = auto: cores - 4, min 2)
        int delay_ms_between_chunks = 50; // Rate-limit delay between embedding API calls
    };

    // Progress callback: (filesProcessed, totalFiles, currentFile)
    using IndexProgressCallback = std::function<void(int, int, const std::string &)>;

    class CodebaseIndexer
    {
    public:
        CodebaseIndexer(IEmbeddingProvider *embedding,
                        VectorStore *store,
                        BM25Index *bm25 = nullptr,
                        IndexProgressCallback progressCb = nullptr,
                        std::function<void(const std::string &)> logCb = nullptr);

        // Run indexing synchronously. Call from a background thread.
        // Returns number of chunks indexed.
        int Index(const IndexerConfig &config);

        // Check which files need (re)indexing without actually indexing.
        // Returns list of relative paths that are new or modified.
        struct IndexStatus
        {
            int totalFiles = 0;
            int upToDate = 0;
            int needsIndexing = 0;
            std::vector<std::string> outdatedFiles; // relative paths
        };
        IndexStatus CheckStatus(const IndexerConfig &config);

        // Request cancellation (thread-safe).
        void Cancel();
        bool IsCancelled() const;
        int GetActiveThreads() const { return m_activeThreads.load(); }
        int GetTotalThreads() const { return m_totalThreads; }

    private:
        struct Chunk
        {
            std::string content; // Text with file path prefix
            std::string file;    // Source file path (relative)
            int startLine = 0;
            int endLine = 0;
        };

        std::vector<std::string> ScanFiles(const IndexerConfig &config) const;
        std::vector<Chunk> ChunkFile(const std::string &filePath, const std::string &relativePath,
                                     int maxChars, int overlapLines) const;

        bool IsBinaryFile(const std::string &filePath, const std::set<std::string> &binaryExts) const;

        IEmbeddingProvider *m_embedding;
        VectorStore *m_store;
        BM25Index *m_bm25;
        IndexProgressCallback m_progressCb;
        std::function<void(const std::string &)> m_logCb;
        std::atomic<bool> m_cancel{false};
        std::atomic<int> m_activeThreads{0};
        int m_totalThreads = 0;
    };
} // namespace pagent
