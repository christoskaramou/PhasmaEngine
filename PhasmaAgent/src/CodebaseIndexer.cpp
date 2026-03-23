#include "PhasmaAgent/CodebaseIndexer.h"
#include "PhasmaAgent/ASTChunker.h"
#include "PhasmaAgent/AgentUtils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <functional>
#include <set>
#include <sstream>
#include <regex>
#include <unordered_map>

namespace pagent
{
    namespace fs = std::filesystem;

    CodebaseIndexer::CodebaseIndexer(IEmbeddingProvider *embedding,
                                     VectorStore *store,
                                     BM25Index *bm25,
                                     IndexProgressCallback progressCb,
                                     std::function<void(const std::string &)> logCb)
        : m_embedding(embedding), m_store(store), m_bm25(bm25), m_progressCb(std::move(progressCb)), m_logCb(std::move(logCb))
    {
    }

    void CodebaseIndexer::Cancel()
    {
        m_cancel.store(true);
    }

    bool CodebaseIndexer::IsCancelled() const
    {
        return m_cancel.load();
    }

    static std::string toLower(std::string s)
    {
        for (auto &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::vector<std::string> CodebaseIndexer::ScanFiles(const IndexerConfig &config) const
    {
        std::vector<std::string> files;

        // Build whitelist set (lowercase), empty = accept all
        std::set<std::string> extSet;
        for (const auto &ext : config.extensions)
            extSet.insert(toLower(ext));

        // Build skip sets - normalize paths with forward slashes, lowercase
        std::set<std::string> skipDirs;
        for (auto d : config.skip_directories)
        {
            for (auto &ch : d)
                if (ch == '\\')
                    ch = '/';
            // Remove trailing slash
            while (!d.empty() && d.back() == '/')
                d.pop_back();
            skipDirs.insert(toLower(d));
        }

        std::set<std::string> skipFiles;
        for (const auto &f : config.skip_files)
            skipFiles.insert(toLower(f));

        // Compile regex patterns
        std::vector<std::regex> skipRegex;
        for (const auto &pat : config.skip_regex)
        {
            try
            {
                skipRegex.emplace_back(pat, std::regex::ECMAScript | std::regex::icase);
            }
            catch (...)
            {
            } // skip invalid patterns
        }

        for (const auto &dir : config.directories)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                continue;

            auto it = fs::recursive_directory_iterator(dir, ec);
            for (auto end = fs::recursive_directory_iterator(); it != end; ++it)
            {
                const auto &entry = *it;

                // Skip blacklisted directories (full path match)
                if (entry.is_directory() && !skipDirs.empty())
                {
                    auto u8dir = entry.path().u8string();
                    std::string dirPath(u8dir.begin(), u8dir.end());
                    for (auto &ch : dirPath)
                        if (ch == '\\')
                            ch = '/';
                    while (!dirPath.empty() && dirPath.back() == '/')
                        dirPath.pop_back();
                    if (skipDirs.count(toLower(dirPath)))
                    {
                        it.disable_recursion_pending();
                        continue;
                    }
                }

                if (!entry.is_regular_file())
                    continue;

                const auto &p = entry.path();

                // Skip blacklisted file names
                if (!skipFiles.empty())
                {
                    auto u8name = p.filename().u8string();
                    std::string fileName(u8name.begin(), u8name.end());
                    if (skipFiles.count(toLower(fileName)))
                        continue;
                }

                auto u8path = p.u8string();
                std::string filePath(u8path.begin(), u8path.end());

                auto u8ext = p.extension().u8string();
                std::string ext = toLower(std::string(u8ext.begin(), u8ext.end()));

                // If whitelist is specified, only include those
                if (!extSet.empty() && !extSet.count(ext))
                    continue;

                // Skip by regex pattern match on relative path
                if (!skipRegex.empty())
                {
                    bool matched = false;
                    for (const auto &re : skipRegex)
                    {
                        if (std::regex_search(filePath, re))
                        {
                            matched = true;
                            break;
                        }
                    }
                    if (matched)
                        continue;
                }

                files.push_back(std::move(filePath));
            }
        }

        // Add explicitly included files (bypass all skip rules)
        for (const auto &f : config.include_files)
        {
            std::error_code ec;
            if (fs::is_regular_file(f, ec))
            {
                auto u8 = fs::path(f).u8string();
                files.emplace_back(u8.begin(), u8.end());
            }
        }

        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        return files;
    }

    bool CodebaseIndexer::IsBinaryFile(const std::string &filePath, const std::set<std::string> &binaryExts) const
    {
        auto ext = toLower(fs::path(filePath).extension().string());
        return binaryExts.count(ext) > 0;
    }

    std::vector<CodebaseIndexer::Chunk> CodebaseIndexer::ChunkFile(
        const std::string &filePath, const std::string &relativePath,
        int maxChars, int overlapLines) const
    {
        std::ifstream f(fs::path(std::u8string(filePath.begin(), filePath.end())));
        if (!f.is_open())
            return {};

        // Read all lines
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line))
            lines.push_back(line);

        if (lines.empty())
            return {};

        std::vector<Chunk> chunks;
        int lineIdx = 0;
        const int totalLines = static_cast<int>(lines.size());

        while (lineIdx < totalLines)
        {
            // Build chunk content with file path prefix
            std::string prefix = "// File: " + relativePath + " (lines " +
                                 std::to_string(lineIdx + 1) + "-";

            std::string body;
            int chunkStart = lineIdx;
            int charsUsed = static_cast<int>(prefix.size()) + 10; // reserve for closing ")\n"

            // Add lines until we hit the char limit
            while (lineIdx < totalLines)
            {
                int lineLen = static_cast<int>(lines[lineIdx].size()) + 1; // +1 for newline
                if (charsUsed + lineLen > maxChars && lineIdx > chunkStart)
                    break;
                body += lines[lineIdx] + "\n";
                charsUsed += lineLen;
                ++lineIdx;
            }

            // Try to break at a blank line boundary (look back a few lines)
            if (lineIdx < totalLines && lineIdx > chunkStart + 1)
            {
                for (int look = lineIdx - 1; look > lineIdx - 10 && look > chunkStart; --look)
                {
                    if (lines[look].find_first_not_of(" \t\r") == std::string::npos)
                    {
                        // Found a blank line - truncate chunk here
                        body.clear();
                        for (int i = chunkStart; i <= look; ++i)
                            body += lines[i] + "\n";
                        lineIdx = look + 1;
                        break;
                    }
                }
            }

            Chunk c;
            c.file = relativePath;
            c.startLine = chunkStart + 1;
            c.endLine = lineIdx;
            c.content = prefix + std::to_string(c.endLine) + ")\n" + body;
            chunks.push_back(std::move(c));

            // Overlap: back up by overlapLines for next chunk
            if (lineIdx < totalLines && overlapLines > 0)
            {
                lineIdx = std::max(chunkStart + 1, lineIdx - overlapLines);
            }
        }

        return chunks;
    }

    static std::string getFileTimestamp(const std::string &filePath)
    {
        std::error_code ec;
        auto ftime = fs::last_write_time(fs::path(std::u8string(filePath.begin(), filePath.end())), ec);
        if (ec)
            return "";
        // Use file_time_type duration directly - we only need consistency, not wall-clock time
        auto ticks = ftime.time_since_epoch().count();
        return std::to_string(ticks);
    }

    // Compute the common root prefix for relative path calculation.
    // Returns a forward-slash-terminated path, or empty if not determinable.
    static std::string ComputeCommonRoot(const std::vector<std::string> &directories)
    {
        if (directories.empty())
            return {};
        fs::path root = fs::path(directories[0]).parent_path();
        std::string commonRoot = root.string();
        for (auto &ch : commonRoot)
            if (ch == '\\')
                ch = '/';
        if (!commonRoot.empty() && commonRoot.back() != '/')
            commonRoot += '/';
        return commonRoot;
    }

    // Make a path relative to commonRoot, normalizing to forward slashes.
    static std::string MakeRelative(const std::string &filePath, const std::string &commonRoot)
    {
        std::string rel = filePath;
        for (auto &ch : rel)
            if (ch == '\\')
                ch = '/';
        if (!commonRoot.empty() && rel.find(commonRoot) == 0)
            rel = rel.substr(commonRoot.size());
        return rel;
    }

    int CodebaseIndexer::Index(const IndexerConfig &config)
    {
        m_cancel.store(false);

        if (m_logCb)
            m_logCb("Scanning files...");
        auto files = ScanFiles(config);

        if (files.empty())
        {
            if (m_logCb)
                m_logCb("No files found to index.");
            return 0;
        }

        std::string commonRoot = ComputeCommonRoot(config.directories);

        // Build binary extension set for quick lookup
        std::set<std::string> binaryExts;
        for (const auto &ext : config.skip_extensions)
            binaryExts.insert(toLower(ext));

        // Precompute relative paths and timestamps, filter out unchanged files
        std::vector<std::string> filesToIndex;
        std::vector<std::string> relToIndex;
        std::vector<std::string> tsToIndex;
        const auto knownTimestamps = m_store ? m_store->BuildFileTimestampMap() : std::unordered_map<std::string, std::string>{};

        for (size_t i = 0; i < files.size(); ++i)
        {
            std::string rel = MakeRelative(files[i], commonRoot);

            std::string ts = getFileTimestamp(files[i]);

            // Skip files that already exist in the store with the same timestamp
            auto knownIt = knownTimestamps.find(rel);
            if (!ts.empty() && knownIt != knownTimestamps.end() && knownIt->second == ts)
                continue;

            filesToIndex.push_back(files[i]);
            relToIndex.push_back(std::move(rel));
            tsToIndex.push_back(std::move(ts));
        }

        if (m_logCb)
            m_logCb("Found " + std::to_string(files.size()) + " files, " +
                    std::to_string(filesToIndex.size()) + " need (re)indexing.");

        if (filesToIndex.empty())
            return 0;

        const int totalFiles = static_cast<int>(filesToIndex.size());

        int numThreads = config.num_threads;
        if (numThreads <= 0)
            numThreads = m_embedding ? m_embedding->RecommendedConcurrency() : 1;

        m_totalThreads = numThreads;
        m_activeThreads.store(0);

        // Heap-allocate shared state so detached threads survive past Index() return
        struct SharedState
        {
            std::vector<std::string> files;
            std::vector<std::string> rels;
            std::vector<std::string> timestamps;
            std::set<std::string> binaryExts;
            int maxChunkChars;
            int chunkOverlapLines;
            int delayMs;
            int totalFiles;
            std::atomic<int> nextFile{0};
            std::atomic<int> totalChunks{0};
            std::atomic<int> filesProcessed{0};
            std::atomic<int> activeThreads{0};
            std::atomic<bool> cancel{false};
            IEmbeddingProvider *embedding = nullptr;
            VectorStore *store = nullptr;
            BM25Index *bm25 = nullptr;
            IndexProgressCallback progressCb;
            std::function<void(const std::string &)> logCb;
        };

        auto shared = std::make_shared<SharedState>();
        shared->files = std::move(filesToIndex);
        shared->rels = std::move(relToIndex);
        shared->timestamps = std::move(tsToIndex);
        shared->binaryExts = std::move(binaryExts);
        shared->maxChunkChars = config.max_chunk_chars;
        shared->chunkOverlapLines = config.chunk_overlap_lines;
        shared->delayMs = config.delay_ms_between_chunks;
        shared->totalFiles = totalFiles;
        shared->embedding = m_embedding;
        shared->store = m_store;
        shared->bm25 = m_bm25;
        shared->progressCb = m_progressCb;
        shared->logCb = m_logCb;

        // Link our cancel flag to embedding provider and shared state when embeddings are available.
        if (m_embedding)
            m_embedding->SetCancel(&shared->cancel);

        auto workerFn = [shared]()
        {
            shared->activeThreads.fetch_add(1);
            while (!shared->cancel.load())
            {
                int idx = shared->nextFile.fetch_add(1);
                if (idx >= shared->totalFiles)
                    break;

                const auto &filePath = shared->files[idx];
                const auto &rel = shared->rels[idx];
                const auto &ts = shared->timestamps[idx];

                int chunksBeforeFile = shared->totalChunks.load();

                // Extract old entries for this file (preserving embeddings for hash reuse)
                auto oldEntries = shared->store->ExtractByFile(rel);
                if (shared->bm25)
                    shared->bm25->RemoveByFile(rel);

                // Build content hash -> old embedding map for reuse
                std::unordered_map<size_t, std::vector<float>> oldHashToEmbedding;
                for (auto &old : oldEntries)
                {
                    size_t contentHash = std::hash<std::string>{}(old.content);
                    oldHashToEmbedding[contentHash] = std::move(old.embedding);
                }

                auto extPos = filePath.rfind('.');
                std::string ext = extPos != std::string::npos ? toLower(filePath.substr(extPos)) : "";
                bool isBinary = shared->binaryExts.count(ext) > 0;
                // PendingChunk: a fully-prepared chunk waiting for an embedding vector.
                struct PendingChunk
                {
                    std::string sanitized;
                    std::string metadataJson;
                    std::string entryId;
                    int startLine = 0;
                    int endLine = 0;
                    std::vector<float> embedding; // pre-filled if reused from cache
                };

                std::vector<PendingChunk> pending;
                bool anyEmbeddingFailed = false;

                if (isBinary)
                {
                    std::string desc = "File: " + rel;
                    PendingChunk pc;
                    pc.sanitized = SanitizeUTF8(desc);
                    pc.metadataJson = nlohmann::json{{"type", "codebase"}, {"file", rel}, {"binary", true}, {"last_modified", ts}}.dump();
                    size_t h = std::hash<std::string>{}(rel);
                    pc.entryId = "codebase_" + std::to_string(h);

                    if (shared->embedding)
                    {
                        size_t contentHash = std::hash<std::string>{}(pc.sanitized);
                        auto hashIt = oldHashToEmbedding.find(contentHash);
                        if (hashIt != oldHashToEmbedding.end())
                            pc.embedding = std::move(hashIt->second);
                        // else: left empty, will be batch-embedded below
                    }
                    else
                    {
                        pc.embedding.assign(1, 0.0f);
                    }
                    pending.push_back(std::move(pc));
                }
                else
                {
                    // Read file content
                    std::ifstream f(fs::path(std::u8string(filePath.begin(), filePath.end())));
                    if (!f.is_open())
                        continue;

                    std::string source((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
                    if (source.empty())
                        continue;

                    // Helper: build a PendingChunk, reusing cached embedding when content is unchanged.
                    auto makeChunk = [&](const std::string &sanitized, int startLine, int endLine,
                                         const std::string &metadataJson) -> PendingChunk
                    {
                        PendingChunk pc;
                        pc.sanitized = sanitized;
                        pc.metadataJson = metadataJson;
                        pc.startLine = startLine;
                        pc.endLine = endLine;
                        size_t h = std::hash<std::string>{}(rel + ":" + std::to_string(startLine));
                        pc.entryId = "codebase_" + std::to_string(h);

                        if (!shared->embedding)
                        {
                            pc.embedding = {0.0f};
                        }
                        else
                        {
                            size_t contentHash = std::hash<std::string>{}(sanitized);
                            auto hashIt = oldHashToEmbedding.find(contentHash);
                            if (hashIt != oldHashToEmbedding.end())
                                pc.embedding = std::move(hashIt->second); // reuse
                        }
                        return pc;
                    };

                    // Try AST-aware chunking for C/C++ files
                    bool useAST = ASTChunker::IsSupported(ext);

                    if (useAST)
                    {
                        auto astChunks = ASTChunker::ChunkCpp(source, shared->maxChunkChars);
                        if (astChunks.empty())
                            useAST = false;

                        for (size_t ci = 0; ci < astChunks.size() && !shared->cancel.load(); ++ci)
                        {
                            auto &ac = astChunks[ci];
                            std::string header = ASTChunker::BuildMetadataHeader(rel, ac);
                            std::string sanitized = SanitizeUTF8(header + "\n" + ac.content);

                            nlohmann::json meta{
                                {"type", "codebase"},
                                {"file", rel},
                                {"lines", std::to_string(ac.startLine) + "-" + std::to_string(ac.endLine)},
                                {"last_modified", ts}};
                            if (!ac.namespaceName.empty())
                                meta["namespace"] = ac.namespaceName;
                            if (!ac.symbols.empty())
                            {
                                auto arr = nlohmann::json::array();
                                for (const auto &sym : ac.symbols)
                                    if (!sym.name.empty())
                                        arr.push_back({{"type", sym.type}, {"name", sym.name}});
                                if (!arr.empty())
                                    meta["symbols"] = std::move(arr);
                            }
                            pending.push_back(makeChunk(sanitized, ac.startLine, ac.endLine, meta.dump()));
                        }
                    }

                    if (!useAST)
                    {
                        std::vector<std::string> lines;
                        std::istringstream iss(source);
                        std::string line;
                        while (std::getline(iss, line))
                            lines.push_back(line);

                        int lineIdx = 0;
                        const int totalLines = static_cast<int>(lines.size());

                        while (lineIdx < totalLines && !shared->cancel.load())
                        {
                            std::string prefix = "// File: " + rel + " (lines " +
                                                 std::to_string(lineIdx + 1) + "-";
                            std::string body;
                            int chunkStart = lineIdx;
                            int charsUsed = static_cast<int>(prefix.size()) + 10;

                            while (lineIdx < totalLines)
                            {
                                int lineLen = static_cast<int>(lines[lineIdx].size()) + 1;
                                if (charsUsed + lineLen > shared->maxChunkChars && lineIdx > chunkStart)
                                    break;
                                body += lines[lineIdx] + "\n";
                                charsUsed += lineLen;
                                ++lineIdx;
                            }

                            if (lineIdx < totalLines && lineIdx > chunkStart + 1)
                            {
                                for (int look = lineIdx - 1; look > lineIdx - 10 && look > chunkStart; --look)
                                {
                                    if (lines[look].find_first_not_of(" \t\r") == std::string::npos)
                                    {
                                        body.clear();
                                        for (int i = chunkStart; i <= look; ++i)
                                            body += lines[i] + "\n";
                                        lineIdx = look + 1;
                                        break;
                                    }
                                }
                            }

                            int endLine = lineIdx;
                            std::string sanitized = SanitizeUTF8(prefix + std::to_string(endLine) + ")\n" + body);
                            pending.push_back(makeChunk(sanitized, chunkStart + 1, endLine,
                                                        nlohmann::json{{"type", "codebase"}, {"file", rel}, {"lines", std::to_string(chunkStart + 1) + "-" + std::to_string(endLine)}, {"last_modified", ts}}.dump()));

                            if (lineIdx < totalLines && shared->chunkOverlapLines > 0)
                                lineIdx = std::max(chunkStart + 1, lineIdx - shared->chunkOverlapLines);
                        }
                    }
                }

                // --- Batch-embed all chunks that don't have a cached embedding ---
                if (shared->embedding && !shared->cancel.load())
                {
                    // Collect texts needing new embeddings and their indices into pending[]
                    std::vector<std::string> toEmbed;
                    std::vector<size_t> toEmbedIdx;
                    for (size_t i = 0; i < pending.size(); ++i)
                    {
                        if (pending[i].embedding.empty())
                        {
                            toEmbed.push_back(pending[i].sanitized);
                            toEmbedIdx.push_back(i);
                        }
                    }

                    if (!toEmbed.empty())
                    {
                        // One HTTP request for the whole file
                        constexpr int kMaxRetries = 3;
                        std::vector<std::vector<float>> batchResult;
                        for (int attempt = 0; attempt < kMaxRetries && batchResult.empty() && !shared->cancel.load(); ++attempt)
                        {
                            if (attempt > 0)
                                std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                            batchResult = shared->embedding->EmbedBatch(toEmbed);
                        }

                        // If full batch failed, try each chunk individually with truncation fallback
                        if (batchResult.empty())
                        {
                            batchResult.resize(toEmbed.size());
                            for (size_t i = 0; i < toEmbed.size() && !shared->cancel.load(); ++i)
                            {
                                auto &text = toEmbed[i];
                                for (int attempt = 0; attempt < kMaxRetries && batchResult[i].empty(); ++attempt)
                                {
                                    if (attempt > 0)
                                        std::this_thread::sleep_for(std::chrono::milliseconds(200 * attempt));
                                    batchResult[i] = shared->embedding->Embed(text);
                                }
                                if (batchResult[i].empty() && text.size() > 512)
                                {
                                    for (size_t tl = text.size() / 2; tl >= 512 && batchResult[i].empty(); tl /= 2)
                                        batchResult[i] = shared->embedding->Embed(text.substr(0, tl));
                                }
                            }
                        }

                        for (size_t i = 0; i < toEmbedIdx.size(); ++i)
                            pending[toEmbedIdx[i]].embedding = std::move(batchResult[i]);

                        if (shared->delayMs > 0)
                            std::this_thread::sleep_for(std::chrono::milliseconds(shared->delayMs));
                    }
                }

                // --- Commit all chunks to the store ---
                for (auto &pc : pending)
                {
                    if (pc.embedding.empty())
                    {
                        anyEmbeddingFailed = true;
                        continue;
                    }
                    VectorEntry entry;
                    entry.id = pc.entryId;
                    entry.content = pc.sanitized;
                    entry.metadata = pc.metadataJson;
                    entry.embedding = std::move(pc.embedding);
                    if (shared->bm25)
                        shared->bm25->Add(entry.id, entry.content);
                    shared->store->Add(std::move(entry));
                    shared->totalChunks.fetch_add(1);
                }

                // If no chunks were added for this file, store a tombstone so CheckStatus won't keep
                // marking it as outdated. This covers both genuinely empty/binary files and files that
                // failed embedding even after retries and truncation (content won't be searchable but
                // the file is acknowledged so indexing can complete).
                // Zero vectors produce cosine similarity 0.0 -> filtered by min_score -> invisible to search.
                if (!shared->cancel.load() && shared->totalChunks.load() == chunksBeforeFile)
                {
                    size_t h = std::hash<std::string>{}(rel + ":tombstone");
                    VectorEntry tombstone;
                    tombstone.id = "codebase_tombstone_" + std::to_string(h);
                    tombstone.content = "";
                    tombstone.metadata = nlohmann::json{{"type", "codebase_tombstone"}, {"file", rel}, {"last_modified", ts}}.dump();
                    tombstone.embedding.assign(shared->embedding ? shared->embedding->Dimensions() : 1, 0.0f);
                    shared->store->Add(std::move(tombstone));
                }

                int done = shared->filesProcessed.fetch_add(1) + 1;
                if (!shared->cancel.load() && shared->progressCb)
                    shared->progressCb(done, shared->totalFiles, rel);
            }
            shared->activeThreads.fetch_sub(1);
        };

        // Launch worker threads - detach so Cancel returns immediately
        for (int t = 0; t < numThreads; ++t)
            std::thread(workerFn).detach();

        // Wait until done or cancelled, updating thread counts for GUI
        while (!m_cancel.load())
        {
            m_activeThreads.store(shared->activeThreads.load());
            if (shared->filesProcessed.load() >= totalFiles)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Propagate cancel to shared state so detached threads stop
        if (m_cancel.load())
            shared->cancel.store(true);

        // Workers do fetch_add then progressCb then activeThreads.fetch_sub — wait for
        // all threads to finish their callbacks before returning, so callers can safely
        // release shared resources (e.g. VectorStore) after Index() returns.
        while (shared->activeThreads.load() > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        m_activeThreads.store(0);

        // Clear cancel pointer so the embedding provider is usable for RAG queries
        if (m_embedding)
            m_embedding->SetCancel(nullptr);

        if (m_logCb)
            m_logCb("Indexing " + std::string(m_cancel.load() ? "cancelled" : "complete") + ": " +
                    std::to_string(shared->totalChunks.load()) + " chunks from " +
                    std::to_string(shared->filesProcessed.load()) + " files.");

        return shared->totalChunks.load();
    }
    CodebaseIndexer::IndexStatus CodebaseIndexer::CheckStatus(const IndexerConfig &config)
    {
        IndexStatus status;
        auto files = ScanFiles(config);
        status.totalFiles = static_cast<int>(files.size());

        if (files.empty())
            return status;

        std::string commonRoot = ComputeCommonRoot(config.directories);
        const auto knownTimestamps = m_store ? m_store->BuildFileTimestampMap() : std::unordered_map<std::string, std::string>{};

        for (size_t i = 0; i < files.size(); ++i)
        {
            std::string rel = MakeRelative(files[i], commonRoot);

            std::string ts = getFileTimestamp(files[i]);

            auto knownIt = knownTimestamps.find(rel);
            if (!ts.empty() && knownIt != knownTimestamps.end() && knownIt->second == ts)
            {
                status.upToDate++;
            }
            else
            {
                status.needsIndexing++;
                status.outdatedFiles.push_back(rel);
            }
        }

        return status;
    }
} // namespace pagent
