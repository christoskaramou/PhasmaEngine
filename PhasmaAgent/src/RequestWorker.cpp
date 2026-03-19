#include "RequestWorker.h"
#include "EventQueue.h"
#include "ConversationHistory.h"
#include "ToolRegistry.h"
#include "ImageDescriber.h"
#include "GoogleBackend.h"
#include "PhasmaAgent/VectorStore.h"
#include "PhasmaAgent/BM25Index.h"
#include "PhasmaAgent/IncludeGraph.h"
#include "PhasmaAgent/AgentUtils.h"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

namespace pagent
{
    // Extract a human-readable message from a JSON HTTP error body.
    // Falls back to the raw body (truncated) if parsing fails.
    static std::string FormatHttpError(int status, const std::string &body)
    {
        try
        {
            auto j = nlohmann::json::parse(body);
            // Standard pattern: {"error": {"message": "..."}}
            if (j.contains("error") && j["error"].is_object() && j["error"].contains("message"))
                return "HTTP " + std::to_string(status) + ": " + j["error"]["message"].get<std::string>();
            // Gemini quota pattern: {"error": {"status": "...", "message": "..."}}
            if (j.contains("message"))
                return "HTTP " + std::to_string(status) + ": " + j["message"].get<std::string>();
        }
        catch (...)
        {
        }
        return "HTTP " + std::to_string(status) + ": " + body.substr(0, 200);
    }
    // Resolve the API host from config, with provider-specific defaults
    static std::string ResolveHost(const AgentConfig &config)
    {
        if (!config.base_url.empty())
            return config.base_url;
        switch (config.provider)
        {
        case Provider::Anthropic:
            return "https://api.anthropic.com";
        case Provider::OpenAI:
            return "https://api.openai.com";
        case Provider::Google:
            return "https://generativelanguage.googleapis.com";
        case Provider::Ollama:
            return "http://localhost:11434";
        }
        return {};
    }

    // Build auth + provider-specific headers
    static std::map<std::string, std::string> BuildHeaders(const AgentConfig &config, IProviderBackend *backend)
    {
        std::map<std::string, std::string> headers;
        if (!config.api_key.empty())
        {
            const auto [authKey, authVal] = backend->GetAuthHeader(config.api_key);
            headers[authKey] = authVal;
        }
        if (config.provider == Provider::Anthropic)
        {
            headers["anthropic-version"] = "2023-06-01";
            headers["anthropic-beta"] = "prompt-caching-2024-07-31";
        }
        return headers;
    }

    RequestWorker::RequestWorker(
        const AgentConfig &config,
        IProviderBackend *backend,
        ConversationHistory &history,
        ToolRegistry &toolRegistry,
        EventQueue &eventQueue)
        : m_config(config), m_backend(backend), m_history(history), m_toolRegistry(toolRegistry), m_eventQueue(eventQueue)
    {
        m_thread = std::thread([this]
                               { ThreadFunc(); });
    }

    RequestWorker::~RequestWorker()
    {
        // Abort any in-flight HTTP request so the thread unblocks immediately.
        Cancel();
        {
            std::lock_guard lock(m_mutex);
            m_stop.store(true, std::memory_order_relaxed);
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    void RequestWorker::Cancel()
    {
        m_cancel.store(true, std::memory_order_relaxed);
        std::lock_guard lock(m_clientMutex);
        if (m_stopActiveClient)
            m_stopActiveClient();
    }

    bool RequestWorker::Submit(const std::string &user_message)
    {
        return Submit(user_message, {});
    }

    bool RequestWorker::Submit(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        if (m_busy.load(std::memory_order_relaxed))
            return false;

        {
            std::lock_guard lock(m_mutex);
            m_pendingMessage = user_message;
            m_pendingAttachments = attachments;
            m_cancel.store(false, std::memory_order_relaxed);
            m_busy.store(true, std::memory_order_relaxed);
        }
        m_cv.notify_one();
        return true;
    }

    void RequestWorker::ThreadFunc()
    {
        while (true)
        {
            std::string message;
            std::vector<ContentPart> attachments;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this]
                          { return m_pendingMessage.has_value() || m_stop.load(std::memory_order_relaxed); });

                if (m_stop.load(std::memory_order_relaxed))
                    return;

                message = std::move(*m_pendingMessage);
                m_pendingMessage.reset();
                attachments.swap(m_pendingAttachments);
            }

            try
            {
                RunAgenticLoop(message, attachments);
            }
            catch (const std::exception &ex)
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = std::string("Unhandled exception in agent loop: ") + ex.what();
                PushEvent(std::move(ev));
            }
            catch (...)
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = "Unknown exception in agent loop";
                PushEvent(std::move(ev));
            }

            m_busy.store(false, std::memory_order_release);
        }
    }

    // Tools whose raw output must always reach the model untouched (code content, not lists).
    static bool ShouldSummarizeTool(const std::string &toolName)
    {
        // Never summarize tools that return raw source code — the model needs the actual content.
        static const std::initializer_list<const char *> kRawContentTools = {
            "read_project_file",
            "read_agent_file",
            "execute_lua",
            "write_project_file",
            "patch_project_file",
            "take_screenshot", // image data must arrive intact
        };
        for (const char *name : kRawContentTools)
            if (toolName == name)
                return false;
        return true;
    }

    // Returns true for tools whose results must never be size-limited (e.g. image data).
    static bool ShouldSkipSizeLimiting(const std::string &toolName)
    {
        return toolName == "take_screenshot";
    }

    // Builds a summarization request for a single large tool result.
    static std::string BuildToolSummaryBody(IProviderBackend *backend,
                                            const AgentConfig &config,
                                            const std::string &toolName,
                                            const std::string &toolResult)
    {
        constexpr const char *kSystemPrompt =
            "You are a tool output summarizer for a coding AI agent. Output only the summary — no preamble.";

        const std::string instruction =
            "Summarize this tool output so a coding AI agent can continue its task without reading the full result.\n"
            "Rules:\n"
            "- PRESERVE: every file path, symbol name, function name, line number, and error message.\n"
            "- PRESERVE: anything the agent needs to decide its next action.\n"
            "- DROP: repetitive entries, duplicate paths, boilerplate, raw code bodies (keep signatures only).\n"
            "- FORMAT: dense bullet list or key-value pairs. No markdown headers.\n"
            "- LENGTH: 5-20 bullets maximum.\n\n"
            "Tool: " +
            toolName + "\nFull output (" + std::to_string(toolResult.size()) + " chars):\n" +
            toolResult.substr(0, 12000); // cap input to the summarizer itself

        std::vector<NeutralMessage> msgs;
        NeutralMessage userMsg;
        userMsg.role = NeutralMessage::Role::User;
        userMsg.content = instruction;
        msgs.push_back(std::move(userMsg));

        // Use cheap/simple model if routing is configured, otherwise fall back to default
        const std::string &model = (!config.routing.simple_model.empty() && config.routing.enabled)
                                       ? config.routing.simple_model
                                       : config.model;
        return backend->BuildRequestJson(model, kSystemPrompt, 400, 0.0f, msgs, "");
    }

    // Builds the summarization request body for a block of old conversation text.
    // Uses a code-aware prompt that preserves file paths, symbols, and the user's goal.
    static std::string BuildSummarizationBody(IProviderBackend *backend,
                                              const AgentConfig &config,
                                              const std::string &oldText)
    {
        constexpr const char *kSystemPrompt =
            "You are a coding session summarizer. Output only the summary - no preamble, no explanation.";

        constexpr const char *kInstructionPrefix =
            "Summarize this coding session segment into a dense, bulleted list of facts.\n"
            "Rules:\n"
            "- PRESERVE: every file path, class name, function name, variable name, and the user's stated goal.\n"
            "- PRESERVE: conclusions, decisions made, and any unresolved issues.\n"
            "- DROP: raw code blocks, raw tool JSON output, pleasantries, and debugging steps that were already resolved.\n"
            "- FORMAT: one bullet per fact, no sub-bullets, no markdown headers.\n"
            "- LENGTH: 5-15 bullets maximum.\n\n"
            "Session segment:\n";

        std::vector<NeutralMessage> msgs;
        NeutralMessage userMsg;
        userMsg.role = NeutralMessage::Role::User;
        userMsg.content = std::string(kInstructionPrefix) + oldText;
        msgs.push_back(std::move(userMsg));

        return backend->BuildRequestJson(config.model, kSystemPrompt, 512, 0.0f, msgs, "");
    }

    void RequestWorker::MaybeSummarize()
    {
        if (m_config.summarize_after_messages <= 0)
            return;

        const size_t count = m_history.EntryCount();
        if (static_cast<int>(count) <= m_config.summarize_after_messages)
            return;

        // Keep the most recent half of the threshold as intact messages
        const size_t keepRecent = static_cast<size_t>(m_config.summarize_after_messages / 2);
        const auto oldText = m_history.BuildOldMessagesText(keepRecent);
        if (oldText.empty())
            return;

        Log("Summarizing conversation (" + std::to_string(count) + " msgs, keeping last " + std::to_string(keepRecent) + ")");

        const std::string body = BuildSummarizationBody(m_backend, m_config, oldText);

        std::vector<AgentEvent> events;
        const auto err = HttpPost(ResolveHost(m_config), m_backend->GetEndpointPath(),
                                  BuildHeaders(m_config, m_backend), body, events);

        if (!err.empty())
        {
            Log("Summarization failed: " + err + ", falling back to compaction");
            return; // Fall back to normal compaction
        }

        // Extract text from response events
        std::string summary;
        for (const auto &ev : events)
        {
            if (ev.type == AgentEventType::TextComplete)
                summary = ev.text;
        }

        if (!summary.empty())
        {
            m_history.ReplaceOldWithSummary(summary, keepRecent);
            Log("Summarized to: " + std::to_string(summary.size()) + " chars");
        }
    }

    bool RequestWorker::ForceCompact(size_t keepRecent)
    {
        const auto oldText = m_history.BuildOldMessagesText(keepRecent);
        if (oldText.empty())
            return false;

        Log("ForceCompact: summarizing (" + std::to_string(m_history.EntryCount()) +
            " msgs, keeping last " + std::to_string(keepRecent) + ")");

        const std::string body = BuildSummarizationBody(m_backend, m_config, oldText);

        std::vector<AgentEvent> events;
        const auto err = HttpPost(ResolveHost(m_config), m_backend->GetEndpointPath(),
                                  BuildHeaders(m_config, m_backend), body, events);

        if (!err.empty())
        {
            Log("ForceCompact failed: " + err);
            return false;
        }

        std::string summary;
        for (const auto &ev : events)
        {
            if (ev.type == AgentEventType::TextComplete)
                summary = ev.text;
        }

        if (summary.empty())
            return false;

        m_history.ReplaceOldWithSummary(summary, keepRecent);
        Log("ForceCompact done: summary=" + std::to_string(summary.size()) + " chars");
        return true;
    }

    // Strip C/C++ comments and blank lines from code to reduce token count.
    // Keeps the "// File:" header line intact since it provides context.
    std::string RequestWorker::StripCommentsAndBlanks(const std::string &code)
    {
        std::string result;
        result.reserve(code.size());

        bool inBlockComment = false;
        size_t i = 0;
        const size_t len = code.size();

        while (i < len)
        {
            if (inBlockComment)
            {
                if (i + 1 < len && code[i] == '*' && code[i + 1] == '/')
                {
                    inBlockComment = false;
                    i += 2;
                }
                else
                {
                    ++i;
                }
                continue;
            }

            // Block comment start
            if (i + 1 < len && code[i] == '/' && code[i + 1] == '*')
            {
                inBlockComment = true;
                i += 2;
                continue;
            }

            // Line comment - but keep "// File:" lines (our chunk header)
            if (i + 1 < len && code[i] == '/' && code[i + 1] == '/')
            {
                // Check if this is a "// File:" header
                bool isHeader = (i + 8 < len) &&
                                code[i + 2] == ' ' && code[i + 3] == 'F' &&
                                code[i + 4] == 'i' && code[i + 5] == 'l' &&
                                code[i + 6] == 'e' && code[i + 7] == ':';
                if (isHeader)
                {
                    // Keep the whole line
                    while (i < len && code[i] != '\n')
                        result += code[i++];
                    if (i < len)
                        result += code[i++]; // newline
                }
                else
                {
                    // Skip the comment line
                    while (i < len && code[i] != '\n')
                        ++i;
                    if (i < len)
                        ++i; // skip newline
                }
                continue;
            }

            // String literals - don't strip // inside strings
            if (code[i] == '"' || code[i] == '\'')
            {
                char quote = code[i];
                result += code[i++];
                while (i < len && code[i] != quote)
                {
                    if (code[i] == '\\' && i + 1 < len)
                    {
                        result += code[i++];
                        result += code[i++];
                    }
                    else
                    {
                        result += code[i++];
                    }
                }
                if (i < len)
                    result += code[i++]; // closing quote
                continue;
            }

            result += code[i++];
        }

        // Remove consecutive blank lines (keep at most one)
        std::string cleaned;
        cleaned.reserve(result.size());
        bool lastWasBlank = false;

        size_t pos = 0;
        while (pos < result.size())
        {
            // Find end of line
            size_t eol = result.find('\n', pos);
            if (eol == std::string::npos)
                eol = result.size();

            std::string_view line(result.data() + pos, eol - pos);

            // Check if line is blank (only whitespace)
            bool isBlank = true;
            for (char c : line)
            {
                if (c != ' ' && c != '\t' && c != '\r')
                {
                    isBlank = false;
                    break;
                }
            }

            if (isBlank)
            {
                if (!lastWasBlank)
                    cleaned += '\n';
                lastWasBlank = true;
            }
            else
            {
                cleaned.append(result, pos, eol - pos);
                cleaned += '\n';
                lastWasBlank = false;
            }

            pos = eol + 1;
        }

        return cleaned;
    }

    void RequestWorker::RunAgenticLoop(const std::string &user_message, const std::vector<ContentPart> &attachments)
    {
        // append the user turn to history.
        {
            NeutralMessage msg;
            msg.role = NeutralMessage::Role::User;
            msg.content = user_message;
            if (!attachments.empty())
            {
                // Build multimodal parts: text first, then attachments
                if (!user_message.empty())
                    msg.parts.push_back({ContentPart::Type::Text, user_message, ""});
                for (const auto &att : attachments)
                    msg.parts.push_back(att);
            }
            m_history.Append(std::move(msg));
        }

        // Summarize old messages if history is getting large
        MaybeSummarize();

        const int maxRounds = m_config.max_tool_rounds > 0 ? m_config.max_tool_rounds : 10;

        // RAG: hybrid search (vector + BM25) with Reciprocal Rank Fusion
        std::string ragContext;
        {
            bool hasVectorStore = m_codebaseStore && m_codebaseStore->Size() > 0 && m_config.embedding_provider;
            bool hasBM25 = m_codebaseBM25 && m_codebaseBM25->Size() > 0;

            if (hasVectorStore || hasBM25)
            {
                std::string queryText;
                auto messages = m_history.GetMessages(m_config.max_history_messages);
                for (auto it = messages.rbegin(); it != messages.rend(); ++it)
                {
                    if (it->role == NeutralMessage::Role::User)
                    {
                        queryText = it->content;
                        break;
                    }
                }

                if (!queryText.empty())
                {
                    // Retrieve broader candidate pools, fuse later
                    const int retrieveK = std::max(m_config.rag_top_k * 5, 30);
                    const float rrfK = 60.0f; // RRF constant

                    // Collect scores per content string: id -> {content, rrf_score}
                    struct FusedEntry
                    {
                        std::string content;
                        float score = 0.0f;
                    };
                    std::unordered_map<std::string, FusedEntry> fused;

                    // Vector search
                    if (hasVectorStore)
                    {
                        auto queryVec = m_config.embedding_provider->Embed(queryText);
                        if (queryVec.empty())
                            Log("RAG: embedding query failed (empty result)");
                        else
                        {
                            auto vecResults = m_codebaseStore->Search(queryVec, retrieveK, m_config.rag_min_score);
                            for (int rank = 0; rank < static_cast<int>(vecResults.size()); ++rank)
                            {
                                const auto &r = vecResults[rank];
                                auto &entry = fused[r.entry->id];
                                entry.content = r.entry->content;
                                entry.score += 1.0f / (rrfK + rank + 1);
                            }
                        }
                    }

                    // BM25 keyword search
                    if (hasBM25)
                    {
                        auto bm25Results = m_codebaseBM25->Search(queryText, retrieveK);
                        for (int rank = 0; rank < static_cast<int>(bm25Results.size()); ++rank)
                        {
                            const auto &r = bm25Results[rank];
                            auto &entry = fused[r.id];
                            if (entry.content.empty())
                                entry.content = r.content;
                            entry.score += 1.0f / (rrfK + rank + 1);
                        }
                    }

                    // Sort fused results by RRF score
                    std::vector<std::pair<std::string, FusedEntry>> sorted(fused.begin(), fused.end());
                    std::sort(sorted.begin(), sorted.end(),
                              [](const auto &a, const auto &b)
                              { return a.second.score > b.second.score; });

                    // Re-rank: boost entries that contain query terms or symbol names
                    {
                        // Tokenize query into lowercase terms
                        std::vector<std::string> queryTerms;
                        {
                            std::string term;
                            for (char c : queryText)
                            {
                                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                                    term += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                                else if (!term.empty())
                                {
                                    if (term.size() >= 3) // skip short words
                                        queryTerms.push_back(std::move(term));
                                    term.clear();
                                }
                            }
                            if (term.size() >= 3)
                                queryTerms.push_back(std::move(term));
                        }

                        if (!queryTerms.empty())
                        {
                            for (auto &[id, entry] : sorted)
                            {
                                // Build lowercase content for matching
                                std::string lower;
                                lower.resize(entry.content.size());
                                std::transform(entry.content.begin(), entry.content.end(), lower.begin(),
                                               [](unsigned char c)
                                               { return static_cast<char>(std::tolower(c)); });

                                int termHits = 0;
                                for (const auto &t : queryTerms)
                                {
                                    if (lower.find(t) != std::string::npos)
                                        ++termHits;
                                }

                                // Boost: fraction of query terms found in the chunk
                                float termBoost = static_cast<float>(termHits) / static_cast<float>(queryTerms.size());
                                entry.score += termBoost * 0.01f; // small boost to preserve RRF ordering mostly
                            }

                            // Re-sort after boosting
                            std::sort(sorted.begin(), sorted.end(),
                                      [](const auto &a, const auto &b)
                                      { return a.second.score > b.second.score; });
                        }
                    }

                    // Take top_k and build context
                    int count = std::min(m_config.rag_top_k, static_cast<int>(sorted.size()));
                    if (count > 0)
                    {
                        // Collect primary results
                        std::vector<std::string> selectedEntries;
                        std::unordered_set<std::string> selectedIds;

                        for (int i = 0; i < static_cast<int>(sorted.size()) && static_cast<int>(selectedEntries.size()) < count; ++i)
                        {
                            std::string cleaned = StripCommentsAndBlanks(sorted[i].second.content);
                            selectedEntries.push_back("- " + SanitizeUTF8(cleaned) + "\n");
                            selectedIds.insert(sorted[i].first);
                        }

                        // Include-graph expansion: add neighbor chunks from related files
                        int neighborSlots = std::max(1, count / 3); // reserve ~1/3 of slots for neighbors
                        if (m_includeGraph && m_includeGraph->Size() > 0 && hasVectorStore)
                        {
                            // Extract file paths from top results (parse "// File: path" prefix)
                            std::unordered_set<std::string> topFiles;
                            for (int i = 0; i < count && i < static_cast<int>(sorted.size()); ++i)
                            {
                                const auto &content = sorted[i].second.content;
                                if (content.compare(0, 9, "// File: ") == 0)
                                {
                                    auto endPos = content.find_first_of(" (\n", 9);
                                    if (endPos != std::string::npos)
                                        topFiles.insert(content.substr(9, endPos - 9));
                                }
                            }

                            // Get neighbor files
                            std::unordered_set<std::string> neighborFiles;
                            for (const auto &f : topFiles)
                            {
                                auto neighbors = m_includeGraph->GetNeighbors(f);
                                for (const auto &n : neighbors)
                                    if (!topFiles.count(n))
                                        neighborFiles.insert(n);
                            }

                            // Find best candidates from neighbor files among remaining sorted results
                            if (!neighborFiles.empty())
                            {
                                int added = 0;
                                for (int i = 0; i < static_cast<int>(sorted.size()) && added < neighborSlots; ++i)
                                {
                                    if (selectedIds.count(sorted[i].first))
                                        continue;
                                    const auto &content = sorted[i].second.content;
                                    if (content.compare(0, 9, "// File: ") != 0)
                                        continue;
                                    auto endPos = content.find_first_of(" (\n", 9);
                                    if (endPos == std::string::npos)
                                        continue;
                                    std::string file = content.substr(9, endPos - 9);
                                    if (!neighborFiles.count(file))
                                        continue;

                                    std::string cleaned = StripCommentsAndBlanks(content);
                                    selectedEntries.push_back("- " + SanitizeUTF8(cleaned) + "\n");
                                    selectedIds.insert(sorted[i].first);
                                    ++added;
                                }
                                if (added > 0)
                                    Log("RAG: added " + std::to_string(added) + " neighbor chunks via include-graph");
                            }
                        }

                        // Build final context string with char limit
                        ragContext = "\n\n[Relevant context:\n";
                        int totalChars = 0;
                        int injected = 0;
                        for (const auto &entry : selectedEntries)
                        {
                            if (totalChars + static_cast<int>(entry.size()) > m_config.rag_max_context_chars)
                                break;
                            ragContext += entry;
                            totalChars += static_cast<int>(entry.size());
                            ++injected;
                        }
                        ragContext += "]";

                        std::string searchType = (hasVectorStore && hasBM25) ? "hybrid" : (hasVectorStore ? "vector" : "bm25");
                        Log("RAG: injected " + std::to_string(injected) + " entries (" + searchType + " search, " + std::to_string(fused.size()) + " candidates)");

                        AgentEvent ragEv;
                        ragEv.type = AgentEventType::Info;
                        ragEv.text = "RAG: " + std::to_string(injected) + " entries (" + searchType + ")";
                        PushEvent(std::move(ragEv));
                    }
                }
            }
        }

        // Model routing: select model based on query complexity
        std::string activeModel = m_config.model;
        if (m_config.routing.enabled)
        {
            auto complexity = ClassifyQuery(user_message);
            if (complexity == QueryComplexity::Simple && !m_config.routing.simple_model.empty())
                activeModel = m_config.routing.simple_model;
            else if (complexity == QueryComplexity::Complex && !m_config.routing.complex_model.empty())
                activeModel = m_config.routing.complex_model;

            if (m_config.log_callback)
            {
                const char *tier = complexity == QueryComplexity::Simple ? "simple" : complexity == QueryComplexity::Complex ? "complex"
                                                                                                                             : "medium";
                m_config.log_callback("[PAgent] Model routing: " + std::string(tier) + " -> " + activeModel);
            }
        }

        // Static context (stable for the whole session): repo_map only.
        // ragContext is dynamic (changes per query) - kept separate and injected per-turn.
        std::string staticContext;
        if (!m_config.repo_map.empty())
            staticContext = "\n\n" + m_config.repo_map;

        // Gemini context caching: cache system_prompt + repo_map + tools.
        // Persisted across Submit() calls and only rebuilt when static content changes.
        // RAG context is intentionally excluded from the cache so it can vary per turn.
        auto *googleBackend = dynamic_cast<GoogleBackend *>(m_backend);
        if (googleBackend && m_config.provider == Provider::Google)
        {
            const auto toolsSchemaJson = m_toolRegistry.GenerateSchemaJson(*m_backend);
            const std::string staticPrompt = SanitizeUTF8(m_config.system_prompt + staticContext);
            const std::string cacheKey = staticPrompt + toolsSchemaJson;

            if (m_geminiCacheName.empty() || m_geminiCacheKey != cacheKey)
            {
                std::string cacheBody = googleBackend->BuildCacheRequestJson(
                    activeModel, staticPrompt, toolsSchemaJson, 3600); // 1-hour TTL

                const std::string host = ResolveHost(m_config);
                auto headers = BuildHeaders(m_config, m_backend);
                headers["Content-Type"] = "application/json";

                auto [status, response] = SimplePost(host, "/v1beta/cachedContents", headers, cacheBody);
                if (status == 200)
                {
                    try
                    {
                        auto j = nlohmann::json::parse(response);
                        m_geminiCacheName = j.value("name", "");
                        if (!m_geminiCacheName.empty())
                        {
                            m_geminiCacheKey = cacheKey;
                            googleBackend->SetCacheName(m_geminiCacheName);
                            Log("Gemini cache created: " + m_geminiCacheName);
                        }
                    }
                    catch (...)
                    {
                    }
                }
                else
                {
                    Log("Gemini cache creation failed (HTTP " + std::to_string(status) + "), proceeding without cache");
                    m_geminiCacheName.clear();
                    m_geminiCacheKey.clear();
                }
            }
            else
            {
                googleBackend->SetCacheName(m_geminiCacheName);
                Log("Gemini cache reused: " + m_geminiCacheName);
            }
        }

        for (int round = 0; round < maxRounds; ++round)
        {
            if (m_cancel.load(std::memory_order_relaxed))
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = "cancelled";
                PushEvent(std::move(ev));
                return;
            }

            // build request
            auto messages = m_history.GetMessages(m_config.max_history_messages);

            // If backend doesn't support vision, describe images via Gemini fallback
            if (!m_backend->SupportsVision() && !m_config.gemini_api_key_for_vision.empty())
            {
                for (auto &msg : messages)
                {
                    if (msg.parts.empty())
                        continue;
                    bool hasImages = false;
                    for (const auto &p : msg.parts)
                        if (p.type == ContentPart::Type::ImageBase64)
                        {
                            hasImages = true;
                            break;
                        }
                    if (!hasImages)
                        continue;

                    // Replace image parts with text descriptions
                    std::vector<ContentPart> newParts;
                    for (const auto &p : msg.parts)
                    {
                        if (p.type == ContentPart::Type::ImageBase64)
                        {
                            std::string desc = ImageDescriber::Describe(
                                p.data, p.mime_type, m_config.gemini_api_key_for_vision,
                                m_config.custom_http_handler);
                            newParts.push_back({ContentPart::Type::Text, "[Image: " + desc + "]", ""});
                        }
                        else
                        {
                            newParts.push_back(p);
                        }
                    }
                    msg.parts = std::move(newParts);
                }
            }

            const auto toolsSchemaJson = m_toolRegistry.GenerateSchemaJson(*m_backend);

            // When cache is active it provides system_instruction + tools, so the system prompt
            // passed here is ignored by BuildRequestJson.  When cache is absent, include the
            // static context (repo_map) as before.  RAG is never baked into the system prompt.
            std::string systemPrompt = SanitizeUTF8(m_config.system_prompt + staticContext);

            // Inject per-turn RAG context into the messages payload (not into history).
            // Prepend as a user/model exchange so the model always has fresh retrieval results
            // without them polluting the persistent conversation history.
            auto requestMessages = messages;
            if (!ragContext.empty())
            {
                NeutralMessage ragMsg;
                ragMsg.role = NeutralMessage::Role::User;
                ragMsg.parts.push_back({ContentPart::Type::Text, "[Relevant codebase context:\n" + ragContext + "\n]", ""});

                NeutralMessage ragAck;
                ragAck.role = NeutralMessage::Role::Assistant;
                ragAck.content = "I have the relevant codebase context.";

                requestMessages.insert(requestMessages.begin(), std::move(ragAck));
                requestMessages.insert(requestMessages.begin(), std::move(ragMsg));
            }

            if (m_config.log_callback)
                m_config.log_callback("[PAgent] Round " + std::to_string(round) + ": " + std::to_string(m_toolRegistry.GetToolCount()) + " tools, " + std::to_string(messages.size()) + " msgs, model='" + activeModel + "', provider=" + std::to_string(static_cast<int>(m_config.provider)));

            const std::string body = m_backend->BuildRequestJson(
                activeModel,
                systemPrompt,
                m_config.max_tokens,
                m_config.temperature,
                requestMessages,
                toolsSchemaJson);

            if (m_config.log_callback)
                m_config.log_callback("[PAgent] Body size=" + std::to_string(body.size()));

            const std::string host = ResolveHost(m_config);
            const std::string path = m_backend->GetEndpointPath();
            const auto headers = BuildHeaders(m_config, m_backend);

            // HTTP POST + stream parse
            if (m_config.log_callback)
            {
                m_config.log_callback("[PAgent] POST " + host + path + " ...");
                m_config.log_callback("[PAgent] Body: " + body);
            }
            std::vector<AgentEvent> turnEvents;
            const auto errMsg = HttpPost(host, path, headers, body, turnEvents);
            if (m_config.log_callback)
                m_config.log_callback("[PAgent] POST done, err='" + errMsg + "', events=" + std::to_string(turnEvents.size()));

            // If the model doesn't support tools, retry without them
            if (!errMsg.empty() && (errMsg.find("does not support tools") != std::string::npos ||
                                    errMsg.find("not enabled for model") != std::string::npos ||
                                    errMsg.find("calling is not enabled") != std::string::npos))
            {
                if (m_config.log_callback)
                    m_config.log_callback("[PAgent] Model does not support tools, retrying without tools");

                const std::string bodyNoTools = m_backend->BuildRequestJson(
                    activeModel, m_config.system_prompt, m_config.max_tokens,
                    m_config.temperature, messages, "");

                turnEvents.clear();
                const auto retryErr = HttpPost(host, path, headers, bodyNoTools, turnEvents);
                if (!retryErr.empty())
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::Error;
                    ev.error_message = retryErr;
                    PushEvent(std::move(ev));
                    return;
                }
            }
            else if (!errMsg.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::Error;
                ev.error_message = errMsg;
                PushEvent(std::move(ev));
                return;
            }

            if (m_config.log_callback && !errMsg.empty())
                m_config.log_callback("[PAgent] Response error: " + errMsg);

            // push all streamed events and collect tool calls + full text
            std::string fullText;
            std::string fullReasoning;
            std::string fullThoughtSignature;
            std::vector<AgentEvent> toolCallCompletes;
            bool hasContent = false;

            for (auto &ev : turnEvents)
            {
                if (!ev.thought_signature.empty())
                    fullThoughtSignature = ev.thought_signature;

                if (ev.type == AgentEventType::TextComplete)
                {
                    fullText = ev.text;
                    hasContent = true;
                }
                else if (ev.type == AgentEventType::ThinkingComplete)
                {
                    fullReasoning = ev.text;
                    hasContent = true;
                }
                else if (ev.type == AgentEventType::ToolCallComplete)
                {
                    toolCallCompletes.push_back(ev);
                    hasContent = true;
                }

                PushEvent(ev);
            }

            // If the response had events (e.g. Usage) but no actual content or tool calls,
            // push a dummy complete event to satisfy the consumer.
            if (!hasContent && !turnEvents.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TextComplete;
                ev.text = "";
                PushEvent(std::move(ev));

                // Don't append empty responses to history as it might confuse certain models
                return;
            }

            // build and append the assistant turn to history
            {
                NeutralMessage assistantMsg;
                assistantMsg.role = NeutralMessage::Role::Assistant;
                assistantMsg.content = fullText;
                assistantMsg.reasoning = fullReasoning;
                assistantMsg.thought_signature = fullThoughtSignature;
                for (const auto &tc : toolCallCompletes)
                {
                    NeutralMessage::ToolCall call;
                    call.id = tc.tool_call_id;
                    call.name = tc.tool_name;
                    call.arguments_json = tc.tool_input_json;
                    call.thought_signature = tc.thought_signature; // Per-call signature
                    assistantMsg.tool_calls.push_back(std::move(call));
                }
                m_history.Append(std::move(assistantMsg));
            }

            // if no tool calls, the model is done
            if (toolCallCompletes.empty())
            {
                AgentEvent ev;
                ev.type = AgentEventType::TurnComplete;
                PushEvent(std::move(ev));
                return;
            }

            // dispatch each tool call
            for (const auto &tc : toolCallCompletes)
            {
                if (m_cancel.load(std::memory_order_relaxed))
                {
                    AgentEvent ev;
                    ev.type = AgentEventType::Error;
                    ev.error_message = "cancelled";
                    PushEvent(std::move(ev));
                    return;
                }

                Log("Dispatching tool: " + tc.tool_name);
                auto resultJson = SanitizeUTF8(m_toolRegistry.Dispatch(tc.tool_name, tc.tool_input_json));

                // Large tool results: summarize if eligible, otherwise hard-truncate.
                const int resultChars = static_cast<int>(resultJson.size());
                const bool summarizeEligible = ShouldSummarizeTool(tc.tool_name);

                if (m_config.summarize_tool_result_chars > 0 &&
                    resultChars > m_config.summarize_tool_result_chars &&
                    summarizeEligible)
                {
                    // Summarize via a cheap model call — preserves key facts, drops bulk
                    Log("Summarizing large tool result for '" + tc.tool_name +
                        "' (" + std::to_string(resultChars) + " chars)");
                    const std::string body = BuildToolSummaryBody(m_backend, m_config, tc.tool_name, resultJson);
                    std::vector<AgentEvent> summaryEvents;
                    const auto err = HttpPost(ResolveHost(m_config), m_backend->GetEndpointPath(),
                                              BuildHeaders(m_config, m_backend), body, summaryEvents);
                    std::string summary;
                    for (const auto &ev : summaryEvents)
                        if (ev.type == AgentEventType::TextComplete)
                            summary = ev.text;

                    if (!err.empty() || summary.empty())
                    {
                        // Summarization failed — fall back to hard truncation
                        Log("Tool summarization failed (" + (err.empty() ? "empty summary" : err) + "), truncating");
                        resultJson = resultJson.substr(0, m_config.max_tool_result_chars > 0
                                                              ? m_config.max_tool_result_chars
                                                              : 4000) +
                                     "...(truncated, " + std::to_string(resultChars) + " total chars)";
                    }
                    else
                    {
                        Log("Tool result summarized: " + std::to_string(resultChars) +
                            " -> " + std::to_string(summary.size()) + " chars");
                        resultJson = "[Summarized " + tc.tool_name + " result (" +
                                     std::to_string(resultChars) + " chars -> summary)]\n" + summary;
                    }
                }
                else if (m_config.max_tool_result_chars > 0 &&
                         resultChars > m_config.max_tool_result_chars &&
                         !ShouldSkipSizeLimiting(tc.tool_name))
                {
                    // Hard truncate: either raw-content tool, or summarization disabled/below threshold
                    resultJson = resultJson.substr(0, m_config.max_tool_result_chars) +
                                 "...(truncated, " + std::to_string(resultChars) + " total chars)";
                }

                AgentEvent resultEv;
                resultEv.type = AgentEventType::ToolResult;
                resultEv.tool_name = tc.tool_name;
                resultEv.tool_call_id = tc.tool_call_id;
                resultEv.text = resultJson;
                PushEvent(resultEv);

                m_history.Append(m_backend->FormatToolResult(tc.tool_call_id, tc.tool_name, resultJson));
            }
        }

        // hit max_tool_rounds
        AgentEvent ev;
        ev.type = AgentEventType::Error;
        ev.error_message = "Reached max_tool_rounds limit";
        PushEvent(std::move(ev));
    }

    std::string RequestWorker::HttpPost(
        const std::string &host,
        const std::string &path,
        const std::map<std::string, std::string> &headers,
        const std::string &body,
        std::vector<AgentEvent> &out_events)
    {
        // use custom_http_handler if provided
        if (m_config.custom_http_handler)
        {
            auto [status, response] = m_config.custom_http_handler("POST", host + path, headers, body);
            if (status != 200)
                return FormatHttpError(status, response);

            StreamParser parser(m_backend);
            parser.Reset(m_backend);
            parser.Feed(response.data(), response.size(), out_events);
            return {};
        }

        // strip protocol prefix and detect http vs https
        std::string cleanHost = host;
        bool useHttps = true;
        const std::string httpsPrefix = "https://";
        const std::string httpPrefix = "http://";
        if (cleanHost.rfind(httpsPrefix, 0) == 0)
            cleanHost = cleanHost.substr(httpsPrefix.size());
        else if (cleanHost.rfind(httpPrefix, 0) == 0)
        {
            cleanHost = cleanHost.substr(httpPrefix.size());
            useHttps = false;
        }
        else if (!m_config.base_url.empty())
        {
            useHttps = false;
        }

        // split host from any path prefix in base_url (e.g. "generativelanguage.googleapis.com/v1beta/openai")
        std::string pathPrefix;
        {
            auto slashPos = cleanHost.find('/');
            if (slashPos != std::string::npos)
            {
                pathPrefix = cleanHost.substr(slashPos); // e.g. "/v1beta/openai"
                cleanHost = cleanHost.substr(0, slashPos);
            }
        }
        // When a path prefix is set, it already contains the version (e.g. /v1beta/openai).
        // Strip the leading /v1 from the endpoint path to avoid duplication:
        //   pathPrefix="/v1beta/openai" + path="/v1/chat/completions" -> "/v1beta/openai/chat/completions"
        std::string endpointSuffix = path;
        if (!pathPrefix.empty() && endpointSuffix.rfind("/v1/", 0) == 0)
            endpointSuffix = endpointSuffix.substr(3); // strip "/v1"
        const std::string fullPath = pathPrefix + endpointSuffix;

        // build httplib headers
        httplib::Headers hlHeaders;
        for (const auto &[k, v] : headers)
            hlHeaders.emplace(k, v);

        StreamParser parser(m_backend);
        parser.Reset(m_backend);

        std::string responseBody;
        std::string errorMsg;

        if (!useHttps)
        {
            // plain HTTP - always available (e.g. Ollama on localhost)
            httplib::Client cli(cleanHost);
            cli.set_read_timeout(120);
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = [&cli]
                { cli.stop(); };
            }
            auto res = cli.Post(fullPath, hlHeaders, body, "application/json");
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = nullptr;
            }
            if (!res || res->status != 200)
                errorMsg = res ? FormatHttpError(res->status, res->body)
                               : ("Connection failed: " + httplib::to_string(res.error()));
            else
                responseBody = std::move(res->body);
        }
        else
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(cleanHost);
            cli.set_read_timeout(120);
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = [&cli]
                { cli.stop(); };
            }
            auto res = cli.Post(fullPath, hlHeaders, body, "application/json");
            {
                std::lock_guard lock(m_clientMutex);
                m_stopActiveClient = nullptr;
            }
            if (!res || res->status != 200)
                errorMsg = res ? FormatHttpError(res->status, res->body) : "Connection failed (SSL)";
            else
                responseBody = std::move(res->body);
#else
            errorMsg = "HTTPS not available: install OpenSSL (vcpkg install openssl:x64-windows) "
                       "or use http:// base_url for local endpoints (e.g. Ollama).";
#endif
        }

        if (errorMsg.empty() && !responseBody.empty())
        {
            parser.Feed(responseBody.data(), responseBody.size(), out_events);

            // If streaming response produced no events, the response may be a raw JSON error
            // (e.g. Gemini returns {"error": {...}} without SSE framing)
            if (out_events.empty())
                return "Empty response from API: " + responseBody.substr(0, 500);
        }

        return errorMsg;
    }

    void RequestWorker::PushEvent(AgentEvent ev)
    {
        m_eventQueue.Push(std::move(ev));
    }

    void RequestWorker::Log(const std::string &msg) const
    {
        if (m_config.log_callback)
            m_config.log_callback("[PhasmaAgent] " + msg);
    }

    std::pair<int, std::string> RequestWorker::SimplePost(
        const std::string &host,
        const std::string &path,
        const std::map<std::string, std::string> &headers,
        const std::string &body)
    {
        if (m_config.custom_http_handler)
            return m_config.custom_http_handler("POST", host + path, headers, body);

        std::string cleanHost = host;
        bool useHttps = true;
        if (cleanHost.rfind("https://", 0) == 0)
            cleanHost = cleanHost.substr(8);
        else if (cleanHost.rfind("http://", 0) == 0)
        {
            cleanHost = cleanHost.substr(7);
            useHttps = false;
        }

        httplib::Headers hlHeaders;
        for (const auto &[k, v] : headers)
            hlHeaders.emplace(k, v);

        if (!useHttps)
        {
            httplib::Client cli(cleanHost);
            cli.set_read_timeout(30);
            auto res = cli.Post(path, hlHeaders, body, "application/json");
            if (!res)
                return {0, "Connection failed"};
            return {res->status, res->body};
        }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        httplib::SSLClient cli(cleanHost);
        cli.set_read_timeout(30);
        auto res = cli.Post(path, hlHeaders, body, "application/json");
        if (!res)
            return {0, "Connection failed"};
        return {res->status, res->body};
#else
        return {0, "HTTPS not available (no OpenSSL)"};
#endif
    }
} // namespace pagent
