#include "Base/Process.h"

#if defined(PE_WIN32)
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
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
            commandLine += L' ' + QuoteWindowsArg(std::filesystem::path(reinterpret_cast<const char8_t *>(arg.c_str())).wstring());
        std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
        commandLineBuffer.push_back(L'\0');

        HANDLE readPipe = nullptr;
        HANDLE writePipe = nullptr;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
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

        if (readPipe)
        {
            char buffer[4096];
            DWORD read = 0;
            while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
                result.output.append(buffer, read);
            CloseHandle(readPipe);
        }

        const DWORD wait = WaitForSingleObject(process.hProcess, options.timeoutMs ? options.timeoutMs : INFINITE);
        if (wait == WAIT_TIMEOUT)
        {
            result.timedOut = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
        }
        else
        {
            DWORD exitCode = 1;
            if (GetExitCodeProcess(process.hProcess, &exitCode))
                result.exitCode = static_cast<int>(exitCode);
        }
        CloseHandle(process.hProcess);
        return result;
    }
#else
    ProcessResult RunProcess(const std::filesystem::path &exe,
                             const std::vector<std::string> &args,
                             const ProcessOptions &options)
    {
        ProcessResult result;

        int pipeFds[2] = {-1, -1};
        if (options.captureOutput && pipe(pipeFds) != 0)
            return result;

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
            return result;
        }
        if (pid == 0)
        {
            if (options.captureOutput)
            {
                close(pipeFds[0]);
                dup2(pipeFds[1], STDOUT_FILENO);
                dup2(pipeFds[1], STDERR_FILENO);
                close(pipeFds[1]);
            }
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                _exit(127);
            // argv reaches the child as a real vector; a bare name resolves through PATH. No shell.
            // nosec
            (void)(bareName ? execvp(argv[0], argv.data()) : execv(argv[0], argv.data()));
            _exit(127);
        }
        result.started = true;

        if (options.detach)
        {
            result.exitCode = 0;
            return result;
        }

        if (options.captureOutput)
        {
            close(pipeFds[1]);
            char buffer[4096];
            for (;;)
            {
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
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeoutMs);
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
