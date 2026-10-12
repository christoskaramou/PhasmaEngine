#include "Base/Process.h"

#if defined(PE_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pe
{
    std::wstring QuoteWindowsArg(const std::wstring &arg)
    {
        if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
            return arg;

        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (wchar_t ch : arg)
        {
            if (ch == L'\\')
            {
                ++backslashes;
            }
            else if (ch == L'"')
            {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(ch);
                backslashes = 0;
            }
            else
            {
                quoted.append(backslashes, L'\\');
                backslashes = 0;
                quoted.push_back(ch);
            }
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

#if defined(PE_WIN32)
    ProcessResult RunProcess(const std::filesystem::path &exe,
                             const std::vector<std::string> &args,
                             const ProcessOptions &options)
    {
        ProcessResult result;

        std::wstring application = exe.wstring();
        if (!exe.has_parent_path())
        {
            // Resolve a bare name here so CreateProcess is never handed a command line to find the
            // executable in.
            wchar_t resolved[MAX_PATH]{};
            if (!SearchPathW(nullptr, application.c_str(), L".exe", MAX_PATH, resolved, nullptr))
                return result;
            application = resolved;
        }

        std::wstring commandLine = QuoteWindowsArg(application);
        for (const std::string &arg : args)
            commandLine += L' ' + QuoteWindowsArg(PathFromUtf8(arg).wstring());
        std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
        commandLineBuffer.push_back(L'\0');

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        // The inheritable write end exists from CreatePipe until it is closed below; a child another thread created
        // in that window would inherit it and keep this pipe open after our child exits.
        static std::mutex spawnMutex;
        std::unique_lock spawnLock(spawnMutex);
        if (options.captureOutput)
        {
            SECURITY_ATTRIBUTES inheritable{};
            inheritable.nLength = sizeof(inheritable);
            inheritable.bInheritHandle = TRUE;
            if (!CreatePipe(&readPipe, &writePipe, &inheritable, 0))
                return result;
            SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startup.hStdOutput = writePipe;
            startup.hStdError = writePipe;
        }

        const std::wstring workingDirectory = options.workingDirectory.wstring();
        PROCESS_INFORMATION process{};
        // Explicit application path and a CRT-quoted argv; no shell is involved.
        // nosec
        const BOOL created = CreateProcessW(application.c_str(),
                                            commandLineBuffer.data(),
                                            nullptr,
                                            nullptr,
                                            options.captureOutput ? TRUE : FALSE,
                                            options.detach ? 0 : CREATE_NO_WINDOW,
                                            nullptr,
                                            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                            &startup,
                                            &process);
        if (writePipe)
            CloseHandle(writePipe);
        spawnLock.unlock();
        if (!created)
        {
            if (readPipe)
                CloseHandle(readPipe);
            return result;
        }
        result.started = true;
        CloseHandle(process.hThread);

        if (options.detach)
        {
            CloseHandle(process.hProcess);
            result.exitCode = 0;
            return result;
        }

        // Drain the pipe on a thread, so the timeout below applies while the child is still running.
        std::atomic<bool> drained{false};
        std::thread reader;
        if (readPipe)
        {
            reader = std::thread([&result, &drained, readPipe]
                                 {
                                     char buffer[4096];
                                     DWORD read = 0;
                                     while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
                                         result.output.append(buffer, read);
                                     drained = true; });
        }

        const DWORD wait = WaitForSingleObject(process.hProcess, options.timeoutMs ? options.timeoutMs : INFINITE);
        if (wait == WAIT_TIMEOUT)
        {
            result.timedOut = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            // A grandchild can still hold the pipe open; cancel the blocking read (retried, as the reader may be
            // between reads when a cancel lands).
            while (reader.joinable() && !drained)
            {
                CancelSynchronousIo(reader.native_handle());
                Sleep(1);
            }
        }
        else
        {
            DWORD exitCode = 1;
            if (GetExitCodeProcess(process.hProcess, &exitCode))
                result.exitCode = static_cast<int>(exitCode);
        }
        if (reader.joinable())
            reader.join();
        if (readPipe)
            CloseHandle(readPipe);
        CloseHandle(process.hProcess);
        return result;
    }
#else
    namespace
    {
        // Close-on-exec, so a child another thread forks meanwhile does not inherit (and hold open) the pipe.
        bool CloexecPipe(int fds[2])
        {
#if defined(__linux__)
            return pipe2(fds, O_CLOEXEC) == 0;
#else
            if (pipe(fds) != 0)
                return false;
            fcntl(fds[0], F_SETFD, FD_CLOEXEC);
            fcntl(fds[1], F_SETFD, FD_CLOEXEC);
            return true;
#endif
        }
    } // namespace

    ProcessResult RunProcess(const std::filesystem::path &exe,
                             const std::vector<std::string> &args,
                             const ProcessOptions &options)
    {
        ProcessResult result;

        int pipeFds[2] = {-1, -1};
        if (options.captureOutput && !CloexecPipe(pipeFds))
            return result;
        // The child writes errno here if chdir or exec fails; a successful exec closes it, so the parent reads EOF.
        int errorFds[2] = {-1, -1};
        if (!CloexecPipe(errorFds))
        {
            if (options.captureOutput)
            {
                close(pipeFds[0]);
                close(pipeFds[1]);
            }
            return result;
        }

        std::vector<std::string> argvStorage;
        argvStorage.reserve(args.size() + 1);
        argvStorage.push_back(exe.string());
        argvStorage.insert(argvStorage.end(), args.begin(), args.end());
        std::vector<char *> argv;
        argv.reserve(argvStorage.size() + 1);
        for (std::string &arg : argvStorage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        const bool bareName = !exe.has_parent_path();
        const std::string workingDirectory = options.workingDirectory.string();

        const pid_t pid = fork();
        if (pid < 0)
        {
            if (options.captureOutput)
            {
                close(pipeFds[0]);
                close(pipeFds[1]);
            }
            close(errorFds[0]);
            close(errorFds[1]);
            return result;
        }
        if (pid == 0)
        {
            if (options.captureOutput)
            {
                // dup2 copies do not keep close-on-exec, so stdout/stderr survive the exec.
                dup2(pipeFds[1], STDOUT_FILENO);
                dup2(pipeFds[1], STDERR_FILENO);
            }
            int error = 0;
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
            {
                error = errno;
            }
            else
            {
                // argv reaches the child as a real vector; a bare name resolves through PATH. No shell.
                // nosec
                (void)(bareName ? execvp(argv[0], argv.data()) : execv(argv[0], argv.data()));
                error = errno;
            }
            (void)!write(errorFds[1], &error, sizeof(error));
            _exit(127);
        }
        close(errorFds[1]);
        if (options.captureOutput)
            close(pipeFds[1]);
        int childError = 0;
        ssize_t errorBytes = 0;
        do
        {
            errorBytes = read(errorFds[0], &childError, sizeof(childError));
        }
        while (errorBytes < 0 && errno == EINTR);
        close(errorFds[0]);
        if (errorBytes == sizeof(childError))
        {
            // chdir or exec failed: nothing started.
            if (options.captureOutput)
                close(pipeFds[0]);
            waitpid(pid, nullptr, 0);
            return result;
        }
        result.started = true;

        if (options.detach)
        {
            if (options.captureOutput)
                close(pipeFds[0]);
            result.exitCode = 0;
            return result;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeoutMs);
        if (options.captureOutput)
        {
            // poll() so the timeout applies while the child is still running.
            char buffer[4096];
            for (;;)
            {
                int waitMs = -1;
                if (options.timeoutMs)
                {
                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                    if (left <= 0)
                        break;
                    waitMs = static_cast<int>(left);
                }
                pollfd readable{pipeFds[0], POLLIN, 0};
                const int ready = poll(&readable, 1, waitMs);
                if (ready < 0 && errno == EINTR)
                    continue;
                if (ready <= 0)
                    break;
                const ssize_t count = read(pipeFds[0], buffer, sizeof(buffer));
                if (count > 0)
                {
                    result.output.append(buffer, static_cast<size_t>(count));
                    continue;
                }
                if (count < 0 && errno == EINTR)
                    continue;
                break;
            }
            close(pipeFds[0]);
        }

        int status = 0;
        pid_t waited = 0;
        if (options.timeoutMs)
        {
            while ((waited = waitpid(pid, &status, WNOHANG)) == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (waited == 0)
            {
                result.timedOut = true;
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                return result;
            }
        }
        else
        {
            do
            {
                waited = waitpid(pid, &status, 0);
            }
            while (waited < 0 && errno == EINTR);
        }
        if (waited == pid && WIFEXITED(status))
            result.exitCode = WEXITSTATUS(status);
        return result;
    }
#endif
} // namespace pe
