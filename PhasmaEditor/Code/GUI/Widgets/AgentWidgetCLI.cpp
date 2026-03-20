#include "AgentWidget.h"
#include "PhasmaAgent/AgentUtils.h"
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#if defined(PE_WIN32)
#include <Windows.h>
#elif defined(PE_LINUX)
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace pagent;

namespace pe
{
    std::string AgentWidget::GetExternalResponsePath() const
    {
        std::filesystem::path p(m_externalFile);
        std::string parent = p.parent_path().string();
        std::string stem = p.stem().string();
        std::string ext = p.extension().string();
        if (ext.empty())
            ext = ".txt";
        std::string rel = (parent.empty() ? "" : parent + "/") + stem + "_response" + ext;
        return Path::Assets + "Agent/" + rel;
    }

    std::string AgentWidget::BuildCLIFullPrompt(const std::string &prompt)
    {
        // Build conversation history for context (last ~10 non-system messages, excluding current)
        std::string historyText;
        {
            std::lock_guard lock(m_chatMutex);
            const int historyLimit = 10;
            int start = std::max(0, static_cast<int>(m_chat.size()) - historyLimit - 1);
            for (int i = start; i < static_cast<int>(m_chat.size()) - 1; ++i)
            {
                const auto &msg = m_chat[i];
                if (msg.role == ChatMessage::Role::User)
                    historyText += "[USER]\n" + msg.text + "\n";
                else if (msg.role == ChatMessage::Role::Assistant)
                    historyText += "[ASSISTANT]\n" + msg.text + "\n";
            }
        }

        // RAG: inject relevant file paths so the CLI tool has a head start
        std::vector<std::string> ragFiles = BuildRagFilePaths(prompt);

        std::string fullPrompt;
        if (!m_cliSystemContext.empty())
        {
            // Inject START.md as background knowledge; clarify it is not a standing instruction to use Lua
            fullPrompt += "[Background reference — use only when the user explicitly asks to perform actions "
                          "in the running editor. Do not mention this block or use these APIs unprompted:\n" +
                          m_cliSystemContext + "]\n\n";
        }
        if (!ragFiles.empty())
        {
            fullPrompt += "[Likely relevant files — start here before searching further:\n";
            for (const auto &f : ragFiles)
                fullPrompt += "- " + f + "\n";
            fullPrompt += "]\n\n";
        }
        if (!historyText.empty())
            fullPrompt += "Recent conversation:\n" + historyText + "\n";
        fullPrompt += prompt;
        return fullPrompt;
    }

    std::string AgentWidget::LaunchCLIProcess(const std::string &cmd, const std::string &promptContent,
                                              const std::string &promptFile,
                                              std::function<void(const char *, size_t)> onData)
    {
        const std::string workingDir = GetRepoRootFromAssets().string();

        // Write prompt to file (avoids shell injection)
        {
            std::ofstream f(promptFile, std::ios::trunc);
            f << promptContent;
        }

        // Accumulate all output; also forward each chunk to caller via onData.
        std::string accumulated;
        auto feedChunk = [&](const char *buf, size_t n)
        {
            accumulated.append(buf, n);
            if (onData)
                onData(buf, n);
        };

        int exitCode = -1;
#if defined(PE_WIN32)
        {
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            // Pipe: child writes to hWritePipe, parent reads from hReadPipe.
            HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
            CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
            SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0); // parent-side not inherited

            // Job Object so TerminateJobObject kills cmd.exe + CLI child together.
            HANDLE hJob = CreateJobObjectA(nullptr, nullptr);
            if (hJob)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
                jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
            }

            // NUL handle for stdin — the shell command already contains "< promptFile".
            HANDLE hNullIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

            std::string winCmd = "cmd.exe /c " + cmd;
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = hNullIn != INVALID_HANDLE_VALUE ? hNullIn : GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = hWritePipe;
            si.hStdError = hWritePipe; // capture stderr too
            PROCESS_INFORMATION pi = {};

            if (CreateProcessA(nullptr, winCmd.data(), nullptr, nullptr, TRUE,
                               CREATE_SUSPENDED, nullptr, workingDir.c_str(), &si, &pi))
            {
                if (hJob)
                    AssignProcessToJobObject(hJob, pi.hProcess);
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
                {
                    std::lock_guard lock(m_cliProcessMutex);
                    m_cliProcessId = reinterpret_cast<intptr_t>(hJob ? hJob : pi.hProcess);
                }

                // Close the write end in the parent so ReadFile returns EOF when the child exits.
                CloseHandle(hWritePipe);
                hWritePipe = nullptr;
                if (hNullIn != INVALID_HANDLE_VALUE)
                    CloseHandle(hNullIn);

                char buf[4096];
                DWORD bytesRead;
                while (!m_cliCancelled &&
                       ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, nullptr) &&
                       bytesRead > 0)
                {
                    feedChunk(buf, bytesRead);
                }

                WaitForSingleObject(pi.hProcess, 5000);
                {
                    std::lock_guard lock(m_cliProcessMutex);
                    m_cliProcessId = 0;
                }
                DWORD dw = 1;
                GetExitCodeProcess(pi.hProcess, &dw);
                CloseHandle(pi.hProcess);
                exitCode = static_cast<int>(dw);
            }
            else
            {
                if (hNullIn != INVALID_HANDLE_VALUE)
                    CloseHandle(hNullIn);
            }

            if (hWritePipe)
                CloseHandle(hWritePipe);
            if (hReadPipe)
                CloseHandle(hReadPipe);
            if (hJob)
                CloseHandle(hJob);
        }
#else
        {
            int pipefd[2];
            pipe(pipefd);

            pid_t pid = fork();
            if (pid == 0)
            {
                // Child: stdin from /dev/null (shell handles "< promptFile" in cmd).
                int nullFd = open("/dev/null", O_RDONLY);
                if (nullFd >= 0)
                {
                    dup2(nullFd, STDIN_FILENO);
                    close(nullFd);
                }
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                if (!workingDir.empty())
                    chdir(workingDir.c_str());
                execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
                _exit(127);
            }
            else if (pid > 0)
            {
                close(pipefd[1]);
                {
                    std::lock_guard lock(m_cliProcessMutex);
                    m_cliProcessId = static_cast<intptr_t>(pid);
                }
                char buf[4096];
                ssize_t n;
                while (!m_cliCancelled && (n = read(pipefd[0], buf, sizeof(buf))) > 0)
                    feedChunk(buf, static_cast<size_t>(n));

                int status = 0;
                waitpid(pid, &status, 0);
                {
                    std::lock_guard lock(m_cliProcessMutex);
                    m_cliProcessId = 0;
                }
                close(pipefd[0]);
                exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
            }
            else
            {
                close(pipefd[0]);
                close(pipefd[1]);
            }
        }
#endif

        // Strip internal reasoning tokens that leak into output (e.g. "<|endoftext|>{...}")
        auto leakPos = accumulated.find("<|endoftext|>");
        if (leakPos != std::string::npos)
            accumulated.erase(leakPos);
        while (!accumulated.empty() &&
               (accumulated.back() == '\n' || accumulated.back() == '\r' || accumulated.back() == ' '))
            accumulated.pop_back();

        if (accumulated.empty() && exitCode != 0)
            accumulated = "[CLI process failed with exit code " + std::to_string(exitCode) + "]";

        return accumulated;
    }

    // Strip ANSI/VT escape sequences (color codes, cursor moves, etc.) from CLI output.
    static std::string StripAnsiCodes(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            if (s[i] != '\x1b')
            {
                out += s[i++];
                continue;
            }
            ++i; // skip ESC
            if (i >= s.size())
                break;
            if (s[i] == '[') // CSI sequence: ESC [ ... <letter>
            {
                ++i;
                while (i < s.size() && s[i] != '\x1b' &&
                       (s[i] == ';' || s[i] == '?' || (s[i] >= '0' && s[i] <= '9')))
                    ++i;
                if (i < s.size() && s[i] != '\x1b')
                    ++i; // skip final command byte
            }
            else if (s[i] == ']') // OSC sequence: ESC ] ... BEL or ESC backslash
            {
                ++i;
                while (i < s.size() && s[i] != '\x07')
                {
                    if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\')
                    {
                        i += 2;
                        break;
                    }
                    ++i;
                }
                if (i < s.size() && s[i] == '\x07')
                    ++i;
            }
            else
            {
                ++i; // skip single-char escape (ESC c, ESC =, etc.)
            }
        }
        return out;
    }

    static std::string SanitizeCLIModel(const char *buf)
    {
        std::string s = buf;
        for (char &c : s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_')
                c = '_';
        return s;
    }

    bool AgentWidget::HasSavedConversationHistory() const
    {
        std::lock_guard lock(m_chatMutex);
        return std::any_of(m_chat.begin(), m_chat.end(),
                           [](const ChatMessage &msg)
                           { return msg.role == ChatMessage::Role::User || msg.role == ChatMessage::Role::Assistant; });
    }

    void AgentWidget::RunCodexCLI(const std::string &prompt)
    {
        m_cliCancelled = false;
        std::string agentDir = Path::Assets + "Agent/";
        std::string promptFile = agentDir + "cli_prompt.tmp";
        std::string repoRoot = GetRepoRootFromAssets().string();

        std::string model = SanitizeCLIModel(m_codexModelBuf);
        const bool shouldResume = m_codexHasSession || (!m_currentSessionPath.empty() && HasSavedConversationHistory());
        std::string cmd;
        if (shouldResume)
        {
            // Resume the most recent session — Codex has full context; just send the new prompt.
            cmd = "codex exec --json resume --last";
            cmd += " - < \"" + promptFile + "\"";
        }
        else
        {
            cmd = "codex exec --json --full-auto";
            if (!model.empty())
                cmd += " -m " + model;
            cmd += " -C \"" + repoRoot + "\"";
            cmd += " - < \"" + promptFile + "\"";
        }

        // On resume, Codex has full context — just send the bare prompt.
        // On first run, inject history + system context via BuildCLIFullPrompt.
        std::string promptContent = shouldResume ? prompt : BuildCLIFullPrompt(prompt);

        auto alive = m_alive;
        std::string accText;
        std::string accThinking;
        std::string accTools;
        std::string lineBuffer;
        bool sawToolExecution = false;

        auto dispatchText = [&](const std::string &t)
        {
            accText += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingText += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchThinking = [&](const std::string &t)
        {
            accThinking += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingThinking += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchTools = [&](const std::string &t)
        {
            // Strip ANSI codes — tool output (e.g. shell command results) may contain color sequences.
            std::string clean = StripAnsiCodes(t);
            accTools += clean;
            QueueAction([this, alive, clean]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingTools += clean;
                m_scrollToBottom = 3; });
        };

        std::string raw = LaunchCLIProcess(cmd, promptContent, promptFile,
                                           [this, alive, &lineBuffer, &dispatchText, &dispatchThinking, &dispatchTools, &sawToolExecution, &accText, &accThinking, &accTools](const char *buf, size_t n)
                                           {
                                               lineBuffer.append(buf, n);
                                               size_t pos = 0;
                                               while ((pos = lineBuffer.find('\n')) != std::string::npos)
                                               {
                                                   std::string line = lineBuffer.substr(0, pos);
                                                   lineBuffer.erase(0, pos + 1);
                                                   if (!line.empty() && line.back() == '\r')
                                                       line.pop_back();
                                                   if (line.empty())
                                                       continue;
                                                   if (line.front() != '{')
                                                       continue; // Ignore warnings / plain-text status lines.

                                                   rapidjson::Document doc;
                                                   doc.Parse(line.c_str(), line.size());
                                                   if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("type") || !doc["type"].IsString())
                                                       continue;

                                                   const std::string type = doc["type"].GetString();
                                                   if ((type == "item.started" || type == "item.completed") &&
                                                       doc.HasMember("item") && doc["item"].IsObject())
                                                   {
                                                       const auto &item = doc["item"];
                                                       if (!item.HasMember("type") || !item["type"].IsString())
                                                           continue;

                                                       const std::string itemType = item["type"].GetString();
                                                       if (itemType == "agent_message" && item.HasMember("text") && item["text"].IsString())
                                                       {
                                                           std::string text = item["text"].GetString();
                                                           if (text.empty())
                                                               continue;

                                                           if (sawToolExecution)
                                                           {
                                                               if (!accText.empty())
                                                                   dispatchText("\n\n");
                                                               dispatchText(text);
                                                           }
                                                           else
                                                           {
                                                               if (!accThinking.empty())
                                                                   dispatchThinking("\n\n");
                                                               dispatchThinking(text);
                                                           }
                                                       }
                                                       else if (itemType == "command_execution")
                                                       {
                                                           sawToolExecution = true;
                                                           std::string toolInfo;
                                                           if (type == "item.started")
                                                           {
                                                               const std::string command = item.HasMember("command") && item["command"].IsString()
                                                                                               ? item["command"].GetString()
                                                                                               : "";
                                                               toolInfo = "[Tool: Command]";
                                                               if (!command.empty())
                                                                   toolInfo += "\n" + command;
                                                           }
                                                           else
                                                           {
                                                               toolInfo = "[Tool Result]";
                                                               if (item.HasMember("aggregated_output") && item["aggregated_output"].IsString())
                                                               {
                                                                   std::string output = item["aggregated_output"].GetString();
                                                                   while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                                                                       output.pop_back();
                                                                   if (!output.empty())
                                                                       toolInfo += "\n" + output;
                                                               }
                                                           }

                                                           if (!toolInfo.empty())
                                                           {
                                                               if (!accTools.empty())
                                                                   dispatchTools("\n\n");
                                                               dispatchTools(toolInfo);
                                                           }
                                                       }
                                                   }
                                               }
                                           });

        if (m_cliCancelled)
        {
            QueueAction([this, alive]()
                        { if (*alive) FirePendingSteer(); });
            return;
        }
        m_codexHasSession = true;
        if (accText.empty() && !sawToolExecution && !accThinking.empty())
        {
            accText = accThinking;
            accThinking.clear();
        }
        if (accText.empty())
            accText = raw;
        if (accText.empty())
            accText = "[Codex returned an empty response]";

        QueueAction([this, alive, accText, accThinking, accTools]()
                    {
            if (!*alive) return;
            if (m_cliCancelled) { FirePendingSteer(); return; }
            std::lock_guard lock(m_chatMutex);
            ChatMessage chatMsg;
            chatMsg.role = ChatMessage::Role::Assistant;
            chatMsg.text = accText;
            chatMsg.thinking = accThinking;
            chatMsg.tools = accTools;
            m_chat.push_back(std::move(chatMsg));
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            m_isStreaming = false;
            m_scrollToBottom = 3;
            FirePendingSteer(); });
    }

    void AgentWidget::RunClaudeCLI(const std::string &prompt)
    {
        m_cliCancelled = false;
        std::string agentDir = Path::Assets + "Agent/";
        std::string promptFile = agentDir + "cli_prompt.tmp";

        std::string model = SanitizeCLIModel(m_claudeModelBuf);
        const bool shouldResume = m_claudeHasSession || (!m_currentSessionPath.empty() && HasSavedConversationHistory());
        // --print is required for --output-format stream-json to work.
        // --include-partial-messages adds token-level delta events for live streaming.
        std::string cmd = "claude --print --verbose --dangerously-skip-permissions"
                          " --output-format stream-json --include-partial-messages";
        if (shouldResume)
            cmd += " --continue"; // resume most recent session; Claude has full context
        else if (!model.empty())
            cmd += " --model " + model;
        cmd += " < \"" + promptFile + "\"";

        auto alive = m_alive;

        // Accumulated final content (for the committed ChatMessage)
        std::string accText;
        std::string accThinking;
        std::string accTools;

        // Line buffer for NDJSON parsing.
        // With --include-partial-messages we get two event styles:
        //   content_block_delta: token-level deltas (text_delta, thinking_delta) — used for live display
        //   assistant (complete):  full content blocks — used only for tool_use (no delta stream)
        std::string lineBuffer;
        bool gotDeltas = false; // true once we've received any content_block_delta

        auto dispatchText = [&](const std::string &t)
        {
            accText += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingText += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchThinking = [&](const std::string &t)
        {
            accThinking += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingThinking += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchTools = [&](const std::string &t)
        {
            accTools += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingTools += t;
                m_scrollToBottom = 3; });
        };

        auto onData = [&](const char *buf, size_t n)
        {
            lineBuffer.append(buf, n);
            size_t pos;
            while ((pos = lineBuffer.find('\n')) != std::string::npos)
            {
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;

                rapidjson::Document doc;
                doc.Parse(line.c_str(), line.size());
                if (doc.HasParseError() || !doc.IsObject())
                    continue;
                if (!doc.HasMember("type") || !doc["type"].IsString())
                    continue;

                const std::string type = doc["type"].GetString();

                // --- Token-level streaming deltas (from --include-partial-messages) ---
                if (type == "content_block_delta" && doc.HasMember("delta") && doc["delta"].IsObject())
                {
                    const auto &delta = doc["delta"];
                    if (!delta.HasMember("type") || !delta["type"].IsString())
                        continue;
                    const std::string dtype = delta["type"].GetString();
                    if (dtype == "text_delta" && delta.HasMember("text") && delta["text"].IsString())
                    {
                        gotDeltas = true;
                        dispatchText(delta["text"].GetString());
                    }
                    else if (dtype == "thinking_delta" && delta.HasMember("thinking") && delta["thinking"].IsString())
                    {
                        gotDeltas = true;
                        dispatchThinking(delta["thinking"].GetString());
                    }
                }
                // --- Complete assistant message (tool_use blocks + fallback when no deltas) ---
                else if (type == "assistant" && doc.HasMember("message"))
                {
                    const auto &msg = doc["message"];
                    if (!msg.IsObject() || !msg.HasMember("content") || !msg["content"].IsArray())
                        continue;

                    for (const auto &block : msg["content"].GetArray())
                    {
                        if (!block.IsObject() || !block.HasMember("type"))
                            continue;
                        const std::string btype = block["type"].GetString();

                        if (btype == "tool_use")
                        {
                            // Tool calls are not streamed via deltas; always show them.
                            std::string toolName = block.HasMember("name") && block["name"].IsString()
                                                       ? block["name"].GetString()
                                                       : "unknown";
                            std::string toolInfo = "[Tool: " + toolName + "]";
                            if (block.HasMember("input") && block["input"].IsObject())
                            {
                                rapidjson::StringBuffer sb;
                                rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                                block["input"].Accept(writer);
                                toolInfo += "\n";
                                toolInfo += sb.GetString();
                            }
                            if (!accTools.empty())
                                toolInfo = "\n\n" + toolInfo;
                            dispatchTools(toolInfo);
                        }
                        else if (!gotDeltas)
                        {
                            // Fallback: no deltas received — use complete blocks for text/thinking.
                            if (btype == "text" && block.HasMember("text") && block["text"].IsString())
                            {
                                if (!accText.empty())
                                    dispatchText("\n\n");
                                dispatchText(block["text"].GetString());
                            }
                            else if (btype == "thinking" && block.HasMember("thinking") && block["thinking"].IsString())
                            {
                                if (!accThinking.empty())
                                    dispatchThinking("\n\n");
                                dispatchThinking(block["thinking"].GetString());
                            }
                        }
                    }
                }
                // --- Top-level error ---
                else if (type == "result" && doc.HasMember("is_error") && doc["is_error"].IsBool() &&
                         doc["is_error"].GetBool())
                {
                    if (doc.HasMember("result") && doc["result"].IsString())
                    {
                        std::string err = std::string("\n\n[Error] ") + doc["result"].GetString();
                        dispatchText(err);
                    }
                }
            }
        };

        // On resume, Claude has full session context — just send the bare prompt.
        std::string promptContent = shouldResume ? prompt : BuildCLIFullPrompt(prompt);
        std::string raw = LaunchCLIProcess(cmd, promptContent, promptFile, onData);

        if (m_cliCancelled)
        {
            QueueAction([this, alive]()
                        { if (*alive) FirePendingSteer(); });
            return;
        }
        m_claudeHasSession = true;

        // Fallback: if NDJSON parsing produced nothing, show raw output.
        if (accText.empty())
            accText = raw;
        if (accText.empty())
            accText = "[Claude CLI returned an empty response]";

        QueueAction([this, alive, accText, accThinking, accTools]()
                    {
            if (!*alive || m_cliCancelled) return;
            std::lock_guard lock(m_chatMutex);
            ChatMessage chatMsg;
            chatMsg.role = ChatMessage::Role::Assistant;
            chatMsg.text = accText;
            chatMsg.thinking = accThinking;
            chatMsg.tools = accTools;
            m_chat.push_back(std::move(chatMsg));
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            m_isStreaming = false;
            m_scrollToBottom = 3;
            FirePendingSteer(); });
    }

    void AgentWidget::RunGeminiCLI(const std::string &prompt)
    {
        m_cliCancelled = false;
        std::string agentDir = Path::Assets + "Agent/";
        std::string promptFile = agentDir + "cli_prompt.tmp";

        std::string model = SanitizeCLIModel(m_geminiModelBuf);
        const bool shouldResume = m_geminiHasSession || (!m_currentSessionPath.empty() && HasSavedConversationHistory());
        std::string cmd = "gemini -y";
        if (shouldResume)
            cmd += " --resume latest";
        else if (!model.empty())
            cmd += " -m " + model;
        cmd += " --output-format stream-json";
        cmd += " -p \"\"";
        cmd += " < \"" + promptFile + "\"";

        auto alive = m_alive;
        std::string promptContent = shouldResume ? prompt : BuildCLIFullPrompt(prompt);
        std::string accText;
        std::string accThinking;
        std::string accTools;
        std::string lineBuffer;
        bool gotAssistantDelta = false;
        bool sawToolUse = false;

        auto dispatchText = [&](const std::string &t)
        {
            accText += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingText += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchThinking = [&](const std::string &t)
        {
            accThinking += t;
            QueueAction([this, alive, t]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingThinking += t;
                m_scrollToBottom = 3; });
        };
        auto dispatchTools = [&](const std::string &t)
        {
            std::string clean = StripAnsiCodes(t);
            accTools += clean;
            QueueAction([this, alive, clean]()
                        {
                if (!*alive || m_cliCancelled) return;
                std::lock_guard lock(m_chatMutex);
                m_streamingTools += clean;
                m_scrollToBottom = 3; });
        };

        LaunchCLIProcess(cmd, promptContent, promptFile,
                         [this, &lineBuffer, &dispatchText, &dispatchThinking, &dispatchTools,
                          &gotAssistantDelta, &accText, &accThinking, &accTools, &sawToolUse](const char *buf, size_t n)
                         {
                             lineBuffer.append(buf, n);
                             size_t pos = 0;
                             while ((pos = lineBuffer.find('\n')) != std::string::npos)
                             {
                                 std::string line = lineBuffer.substr(0, pos);
                                 lineBuffer.erase(0, pos + 1);
                                 if (!line.empty() && line.back() == '\r')
                                     line.pop_back();
                                 if (line.empty() || line.front() != '{')
                                     continue; // Ignore CLI chatter/stderr like AttachConsole failures.

                                 rapidjson::Document doc;
                                 doc.Parse(line.c_str(), line.size());
                                 if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("type") || !doc["type"].IsString())
                                     continue;

                                 const std::string type = doc["type"].GetString();
                                 if (type == "message" &&
                                     doc.HasMember("role") && doc["role"].IsString() &&
                                     std::string(doc["role"].GetString()) == "assistant" &&
                                     doc.HasMember("content") && doc["content"].IsString())
                                 {
                                     const std::string content = doc["content"].GetString();
                                     if (content.empty())
                                         continue;

                                     const bool isDelta = doc.HasMember("delta") && doc["delta"].IsBool() && doc["delta"].GetBool();
                                     if (isDelta)
                                     {
                                         gotAssistantDelta = true;
                                         if (sawToolUse)
                                             dispatchText(content);
                                         else
                                             dispatchThinking(content);
                                     }
                                     else if (!gotAssistantDelta && accText.empty() && accThinking.empty())
                                     {
                                         if (sawToolUse)
                                             dispatchText(content);
                                         else
                                             dispatchThinking(content);
                                     }
                                 }
                                 else if (type == "tool_use" &&
                                          doc.HasMember("tool_name") && doc["tool_name"].IsString())
                                 {
                                     sawToolUse = true;
                                     std::string toolInfo = std::string("[Tool: ") + doc["tool_name"].GetString() + "]";
                                     if (doc.HasMember("parameters") && doc["parameters"].IsObject())
                                     {
                                         rapidjson::StringBuffer sb;
                                         rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
                                         doc["parameters"].Accept(writer);
                                         toolInfo += "\n";
                                         toolInfo += sb.GetString();
                                     }
                                     if (!accTools.empty())
                                         dispatchTools("\n\n");
                                     dispatchTools(toolInfo);
                                 }
                                 else if (type == "tool_result")
                                 {
                                     sawToolUse = true;
                                     std::string toolInfo = "[Tool Result]";
                                     if (doc.HasMember("status") && doc["status"].IsString())
                                         toolInfo += std::string(" (") + doc["status"].GetString() + ")";
                                     if (doc.HasMember("output") && doc["output"].IsString())
                                     {
                                         std::string output = doc["output"].GetString();
                                         while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                                             output.pop_back();
                                         if (!output.empty())
                                             toolInfo += "\n" + output;
                                     }
                                     if (!accTools.empty())
                                         dispatchTools("\n\n");
                                     dispatchTools(toolInfo);
                                 }
                                 else if (type == "result" &&
                                          doc.HasMember("status") && doc["status"].IsString() &&
                                          std::string(doc["status"].GetString()) != "success" &&
                                          accText.empty() && accThinking.empty())
                                 {
                                     dispatchText("[Gemini CLI failed]");
                                 }
                             }
                         });

        if (m_cliCancelled)
        {
            QueueAction([this, alive]()
                        { if (*alive) FirePendingSteer(); });
            return;
        }
        m_geminiHasSession = true;
        if (accText.empty() && !sawToolUse && accThinking.empty())
            accText = "[Gemini CLI returned an empty response]";
        else if (accText.empty() && !sawToolUse && !accThinking.empty())
        {
            accText = accThinking;
            accThinking.clear();
        }

        QueueAction([this, alive, accText, accThinking, accTools]()
                    {
            if (!*alive || m_cliCancelled) return;
            std::lock_guard lock(m_chatMutex);
            ChatMessage chatMsg;
            chatMsg.role = ChatMessage::Role::Assistant;
            chatMsg.text = accText;
            chatMsg.thinking = accThinking;
            chatMsg.tools = accTools;
            m_chat.push_back(std::move(chatMsg));
            m_streamingText.clear();
            m_streamingThinking.clear();
            m_streamingTools.clear();
            m_isStreaming = false;
            m_scrollToBottom = 3;
            FirePendingSteer(); });
    }

    void AgentWidget::UpdateExternalFileWatch()
    {
        std::string agentDir = Path::Assets + "Agent/";
        std::string responsePath = GetExternalResponsePath();

        // Remove previous watch if path changed
        if (!m_externalResponsePath.empty() && m_externalResponsePath != responsePath)
            FileWatcher::Erase(m_externalResponsePath);
        m_externalResponsePath = responsePath;

        // Ensure parent directories and files exist
        std::string inputPath = agentDir + m_externalFile;
        for (const auto &path : {inputPath, responsePath})
        {
            auto parent = std::filesystem::path(path).parent_path();
            if (!std::filesystem::exists(parent))
                std::filesystem::create_directories(parent);
            if (!std::filesystem::exists(path))
                std::ofstream(path).close();
        }

        // Watch response file
        FileWatcher::Add(responsePath, [this](size_t)
                         { QueueAction([this]()
                                       { PollExternalResponse(); }); });
    }

    void AgentWidget::PollExternalResponse()
    {
        if (!m_isExternalAI || !m_isStreaming)
            return;

        const auto &responsePath = m_externalResponsePath;
        if (!std::filesystem::exists(responsePath))
            return;

        // Check if file has content
        auto fileSize = std::filesystem::file_size(responsePath);
        if (fileSize == 0)
            return;

        std::ifstream f(responsePath);
        if (!f.is_open())
            return;

        std::string response((std::istreambuf_iterator<char>(f)), {});
        f.close();

        if (response.empty())
            return;

        // Clear the response file so we don't re-read it
        std::ofstream(responsePath, std::ios::trunc).close();

        {
            std::lock_guard lock(m_chatMutex);
            m_chat.push_back({ChatMessage::Role::Assistant, response});
            m_streamingTools.clear();
            m_scrollToBottom = 3;
        }
        m_isStreaming = false;
        WriteExternalHistory();
        FirePendingSteer();
    }

    void AgentWidget::WriteExternalHistory()
    {
        std::string historyPath = std::filesystem::path(Path::Assets + "Agent/" + m_externalFile)
                                      .parent_path()
                                      .string() +
                                  "/chat_history.txt";
        std::ofstream f(historyPath, std::ios::trunc);
        if (!f.is_open())
            return;

        std::lock_guard lock(m_chatMutex);
        for (const auto &msg : m_chat)
        {
            const char *role = msg.role == ChatMessage::Role::User        ? "USER"
                               : msg.role == ChatMessage::Role::Assistant ? "ASSISTANT"
                                                                          : "SYSTEM";
            f << "[" << role << "]\n"
              << msg.text << "\n\n";
        }
    }

} // namespace pe
