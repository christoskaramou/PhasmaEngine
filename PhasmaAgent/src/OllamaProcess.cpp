#include "OllamaProcess.h"
#include <httplib/httplib.h>
#include <thread>
#include <array>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#endif

namespace pagent
{
    OllamaProcess::~OllamaProcess()
    {
        Stop();
    }

    OllamaProcess::OllamaProcess(OllamaProcess &&other) noexcept
        : m_handle(other.m_handle)
    {
        other.m_handle = InvalidHandle;
    }

    OllamaProcess &OllamaProcess::operator=(OllamaProcess &&other) noexcept
    {
        if (this != &other)
        {
            Stop();
            m_handle = other.m_handle;
            other.m_handle = InvalidHandle;
        }
        return *this;
    }

    void OllamaProcess::Start(const std::string &model)
    {
        (void)model; // model is loaded on first API request by ollama serve
        Stop();

#ifdef _WIN32
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        char cmd[] = "ollama serve";
        if (CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            m_handle = pi.hProcess;
        }
#else
        pid_t pid = fork();
        if (pid == 0)
        {
            // Child: silence all I/O
            int devNull = open("/dev/null", O_RDWR);
            if (devNull >= 0)
            {
                dup2(devNull, STDIN_FILENO);
                dup2(devNull, STDOUT_FILENO);
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }
            execlp("ollama", "ollama", "serve", nullptr);
            _exit(1);
        }
        else if (pid > 0)
        {
            m_handle = pid;
        }
#endif
    }

    void OllamaProcess::Stop()
    {
        if (m_handle == InvalidHandle)
            return;

#ifdef _WIN32
        TerminateProcess(m_handle, 0);
        WaitForSingleObject(m_handle, 3000);
        CloseHandle(m_handle);
#else
        kill(m_handle, SIGTERM);
        waitpid(m_handle, nullptr, 0);
#endif
        m_handle = InvalidHandle;
    }
    static bool IsValidModelName(const std::string &name)
    {
        if (name.empty() || name.size() > 256)
            return false;
        for (char c : name)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)) &&
                c != '.' && c != '-' && c != '_' && c != ':' && c != '/')
                return false;
        }
        return true;
    }

    // Strip http:// prefix from Ollama host URL, defaulting to localhost:11434
    static std::string ResolveOllamaHost(const std::string &base_url)
    {
        std::string host = base_url.empty() ? "http://localhost:11434" : base_url;
        const std::string httpPrefix = "http://";
        if (host.rfind(httpPrefix, 0) == 0)
            host = host.substr(httpPrefix.size());
        return host;
    }

    void OllamaProcess::UnloadModel(const std::string &model, const std::string &base_url)
    {
        if (!IsValidModelName(model))
            return;

        httplib::Client cli(ResolveOllamaHost(base_url));
        cli.set_read_timeout(5);
        std::string body = "{\"model\":\"" + model + "\",\"keep_alive\":0}";
        cli.Post("/api/generate", body, "application/json");
    }

    void OllamaProcess::CancelPull(const CancelToken &token)
    {
        if (token)
            token->store(true);
    }

    OllamaProcess::CancelToken OllamaProcess::Pull(const std::string &model,
                                                   ProgressCallback progressCb,
                                                   CompleteCallback completeCb)
    {
        if (!IsValidModelName(model))
        {
            if (completeCb)
                completeCb(false);
            return nullptr;
        }

        auto cancel = std::make_shared<std::atomic<bool>>(false);

        std::thread([model, progressCb, completeCb, cancel]()
                    {
            // Strip ANSI escape sequences from ollama pull output
            auto stripAnsi = [](const std::string &s) -> std::string
            {
                std::string out;
                out.reserve(s.size());
                for (size_t i = 0; i < s.size(); ++i)
                {
                    if (s[i] == '\x1b' && i + 1 < s.size())
                    {
                        char next = s[i + 1];
                        if (next == '[')
                        {
                            // CSI sequence: ESC [ ... <letter>
                            i += 2;
                            while (i < s.size() && !std::isalpha(static_cast<unsigned char>(s[i])))
                                ++i;
                        }
                        else if (next == ']')
                        {
                            // OSC sequence: ESC ] ... (ST or BEL)
                            i += 2;
                            while (i < s.size() && s[i] != '\x07' && s[i] != '\x1b')
                                ++i;
                        }
                        else
                        {
                            // Other ESC sequences (e.g. ESC ( B): skip ESC + next char
                            ++i;
                        }
                    }
                    else if (s[i] == '\r')
                    {
                        // Carriage return - ollama uses \r for progress updates
                        out.clear();
                    }
                    else if (static_cast<unsigned char>(s[i]) >= 32)
                    {
                        out += s[i];
                    }
                }
                return out;
            };

#ifdef _WIN32
            // Windows: use CreateProcess so we can terminate it
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE readPipe = nullptr, writePipe = nullptr;
            if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
            {
                if (progressCb) progressCb("Failed to create pipe");
                if (completeCb) completeCb(false);
                return;
            }
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.hStdOutput = writePipe;
            si.hStdError = writePipe;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            std::string cmd = "ollama pull " + model;
            std::vector<char> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back('\0');
            if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            {
                CloseHandle(readPipe);
                CloseHandle(writePipe);
                if (progressCb) progressCb("Failed to start ollama pull");
                if (completeCb) completeCb(false);
                return;
            }
            CloseHandle(writePipe);
            CloseHandle(pi.hThread);

            std::array<char, 256> buf;
            DWORD bytesRead;
            std::string line;
            while (ReadFile(readPipe, buf.data(), static_cast<DWORD>(buf.size() - 1), &bytesRead, nullptr) && bytesRead > 0)
            {
                if (cancel->load())
                {
                    TerminateProcess(pi.hProcess, 1);
                    break;
                }
                buf[bytesRead] = '\0';
                line = buf.data();
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                line = stripAnsi(line);
                if (!line.empty() && progressCb)
                    progressCb(line);
            }
            WaitForSingleObject(pi.hProcess, 5000);
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(readPipe);
            bool success = !cancel->load() && (exitCode == 0);
#else
            // POSIX: fork/exec so we have a pid to kill
            int pipefd[2];
            if (pipe(pipefd) != 0)
            {
                if (progressCb) progressCb("Failed to create pipe");
                if (completeCb) completeCb(false);
                return;
            }

            pid_t pid = fork();
            if (pid == 0)
            {
                // Child
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);
                execlp("ollama", "ollama", "pull", model.c_str(), nullptr);
                _exit(1);
            }
            else if (pid < 0)
            {
                close(pipefd[0]);
                close(pipefd[1]);
                if (progressCb) progressCb("Failed to start ollama pull");
                if (completeCb) completeCb(false);
                return;
            }

            close(pipefd[1]);

            // Set read end to non-blocking so we can check the cancel flag
            fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);

            std::array<char, 256> buf;
            std::string line;
            bool cancelled = false;
            while (!cancelled)
            {
                if (cancel->load())
                {
                    kill(pid, SIGTERM);
                    cancelled = true;
                    break;
                }

                ssize_t n = read(pipefd[0], buf.data(), buf.size() - 1);
                if (n > 0)
                {
                    buf[n] = '\0';
                    line = buf.data();
                    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                        line.pop_back();
                    line = stripAnsi(line);
                    if (!line.empty() && progressCb)
                        progressCb(line);
                }
                else if (n == 0)
                {
                    break; // EOF - process done
                }
                else
                {
                    // EAGAIN: no data yet, sleep briefly
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            close(pipefd[0]);

            int status = 0;
            waitpid(pid, &status, 0);
            bool success = !cancelled && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
            if (completeCb) completeCb(success); })
            .detach();

        return cancel;
    }
} // namespace pagent
