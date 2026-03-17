#include "PhasmaAgent/CodebaseIndexer.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <functional>
#include <set>
#include <regex>

namespace pagent
{
    namespace fs = std::filesystem;

    CodebaseIndexer::CodebaseIndexer(IEmbeddingProvider *embedding,
                                     VectorStore *store,
                                     IndexProgressCallback progressCb,
                                     std::function<void(const std::string &)> logCb)
        : m_embedding(embedding), m_store(store), m_progressCb(std::move(progressCb)), m_logCb(std::move(logCb))
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

        // Build skip sets — normalize paths with forward slashes, lowercase
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

        std::set<std::string> skipExts;
        for (const auto &e : config.skip_extensions)
            skipExts.insert(toLower(e));

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

                // Skip by extension (completely skip, not even path-only)
                if (!skipExts.empty() && skipExts.count(ext))
                    continue;

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

        std::sort(files.begin(), files.end());
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
                        // Found a blank line — truncate chunk here
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

    // Replace invalid UTF-8 bytes with '?' so nlohmann::json doesn't throw
    static std::string sanitizeUtf8(const std::string &input)
    {
        std::string out;
        out.reserve(input.size());
        size_t i = 0;
        while (i < input.size())
        {
            unsigned char c = static_cast<unsigned char>(input[i]);
            int len = 0;
            if (c < 0x80)
                len = 1;
            else if ((c & 0xE0) == 0xC0)
                len = 2;
            else if ((c & 0xF0) == 0xE0)
                len = 3;
            else if ((c & 0xF8) == 0xF0)
                len = 4;

            if (len == 0 || i + len > input.size())
            {
                out += '?';
                ++i;
                continue;
            }

            bool valid = true;
            for (int j = 1; j < len; ++j)
            {
                if ((static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80)
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
                out.append(input, i, len);
            else
                out += '?';
            i += valid ? len : 1;
        }
        return out;
    }

    static std::string getFileTimestamp(const std::string &filePath)
    {
        std::error_code ec;
        auto ftime = fs::last_write_time(fs::path(std::u8string(filePath.begin(), filePath.end())), ec);
        if (ec)
            return "";
        // Use file_time_type duration directly — we only need consistency, not wall-clock time
        auto ticks = ftime.time_since_epoch().count();
        return std::to_string(ticks);
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

        // Determine a common root for relative paths
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

        // Build binary extension set for quick lookup
        std::set<std::string> binaryExts;
        for (const auto &ext : config.skip_extensions)
            binaryExts.insert(toLower(ext));

        // Precompute relative paths and timestamps, filter out unchanged files
        std::vector<std::string> filesToIndex;
        std::vector<std::string> relToIndex;
        std::vector<std::string> tsToIndex;

        for (size_t i = 0; i < files.size(); ++i)
        {
            std::string rel = files[i];
            for (auto &ch : rel)
                if (ch == '\\')
                    ch = '/';
            if (!commonRoot.empty() && rel.find(commonRoot) == 0)
                rel = rel.substr(commonRoot.size());

            std::string ts = getFileTimestamp(files[i]);

            // Skip files that already exist in the store with the same timestamp
            if (!ts.empty() && m_store->HasFileWithTimestamp(rel, ts))
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
        {
            int cores = static_cast<int>(std::thread::hardware_concurrency());
            numThreads = std::max(2, cores - 4);
        }

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
        shared->progressCb = m_progressCb;
        shared->logCb = m_logCb;

        // Link our cancel flag to embedding provider and shared state
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

                // Remove old entries for this file (it changed or is new)
                shared->store->RemoveByFile(rel);

                auto extPos = filePath.rfind('.');
                std::string ext = extPos != std::string::npos ? toLower(filePath.substr(extPos)) : "";
                bool isBinary = shared->binaryExts.count(ext) > 0;

                if (isBinary)
                {
                    std::string desc = "File: " + rel;
                    auto vec = shared->embedding->Embed(desc);
                    if (!vec.empty())
                    {
                        size_t h = std::hash<std::string>{}(rel);
                        VectorEntry entry;
                        entry.id = "codebase_" + std::to_string(h);
                        entry.content = desc;
                        entry.metadata = "{\"type\":\"codebase\",\"file\":\"" + rel +
                                         "\",\"binary\":true,\"last_modified\":\"" + ts + "\"}";
                        entry.embedding = std::move(vec);
                        shared->store->Add(std::move(entry));
                        shared->totalChunks.fetch_add(1);
                    }
                }
                else
                {
                    // Inline chunking — read file and split into chunks
                    std::ifstream f(fs::path(std::u8string(filePath.begin(), filePath.end())));
                    if (!f.is_open())
                        continue;

                    std::vector<std::string> lines;
                    std::string line;
                    while (std::getline(f, line))
                        lines.push_back(line);

                    if (!lines.empty())
                    {
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
                            std::string content = prefix + std::to_string(endLine) + ")\n" + body;
                            std::string sanitized = sanitizeUtf8(content);

                            auto vec = shared->embedding->Embed(sanitized);
                            if (vec.empty())
                                continue;

                            size_t h = std::hash<std::string>{}(rel + ":" + std::to_string(chunkStart + 1));
                            VectorEntry entry;
                            entry.id = "codebase_" + std::to_string(h);
                            entry.content = sanitized;
                            entry.metadata = "{\"type\":\"codebase\",\"file\":\"" + rel +
                                             "\",\"lines\":\"" + std::to_string(chunkStart + 1) +
                                             "-" + std::to_string(endLine) +
                                             "\",\"last_modified\":\"" + ts + "\"}";
                            entry.embedding = std::move(vec);
                            shared->store->Add(std::move(entry));
                            shared->totalChunks.fetch_add(1);

                            if (shared->delayMs > 0)
                                std::this_thread::sleep_for(std::chrono::milliseconds(shared->delayMs));

                            if (lineIdx < totalLines && shared->chunkOverlapLines > 0)
                                lineIdx = std::max(chunkStart + 1, lineIdx - shared->chunkOverlapLines);
                        }
                    }
                }

                int done = shared->filesProcessed.fetch_add(1) + 1;
                if (!shared->cancel.load() && shared->progressCb)
                    shared->progressCb(done, shared->totalFiles, rel);
            }
            shared->activeThreads.fetch_sub(1);
        };

        // Launch worker threads — detach so Cancel returns immediately
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

        m_activeThreads.store(0);

        // Clear cancel pointer so the embedding provider is usable for RAG queries
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

        // Determine common root for relative paths
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

        for (size_t i = 0; i < files.size(); ++i)
        {
            std::string rel = files[i];
            for (auto &ch : rel)
                if (ch == '\\')
                    ch = '/';
            if (!commonRoot.empty() && rel.find(commonRoot) == 0)
                rel = rel.substr(commonRoot.size());

            std::string ts = getFileTimestamp(files[i]);

            if (!ts.empty() && m_store->HasFileWithTimestamp(rel, ts))
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
