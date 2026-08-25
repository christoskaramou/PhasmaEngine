#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"

#if defined(PE_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pe
{
    // A child process spoken to over stdin/stdout — the bridge Lua needs to drive an external
    // tool that only speaks a line protocol (a UCI chess engine is the case this was written for).
    // The Lua state opens only base/math/string/table/coroutine, so a script has no io or os and
    // cannot do this itself.
    //
    // ponytail: exactly ONE child at a time. Everything that has wanted this so far wants a single
    // long-lived helper, and one slot keeps the whole lifetime question trivial. Add a handle id if
    // a second one is ever needed.
    //
    // stdout without a newline is capped (same trust boundary as net.read_line): a helper that
    // never sends '\n' must not grow the frame's buffer forever.
    //
    // The executable must live under the project's Assets/ — same sandbox as fs.read/fs.write. A
    // scene script can therefore only launch something that shipped with the project.
    namespace
    {
        constexpr size_t kMaxBuffer = 256 * 1024; // unparsed stdout before we kill the child

        class ChildProcess
        {
        public:
            ~ChildProcess() { Stop(); }

            bool Start(const std::filesystem::path &exe, const std::vector<std::string> &args, std::string &error)
            {
                Stop();
#if defined(PE_WIN32)
                // Kill-on-close job: if the game crashes or is killed, Windows takes the child with
                // it. Without this a stuck helper outlives every run and piles up.
                if (!m_job)
                {
                    m_job = CreateJobObjectW(nullptr, nullptr);
                    if (m_job)
                    {
                        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
                    }
                }

                SECURITY_ATTRIBUTES sa{};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = TRUE;

                HANDLE childIn = nullptr, childOut = nullptr;
                if (!CreatePipe(&childIn, &m_stdin, &sa, 0))
                {
                    error = "CreatePipe failed";
                    return false;
                }
                if (!CreatePipe(&m_stdout, &childOut, &sa, 0))
                {
                    CloseHandle(childIn);
                    CloseHandle(m_stdin);
                    childIn = nullptr;
                    m_stdin = nullptr;
                    error = "CreatePipe failed";
                    return false;
                }
                // The parent's ends must NOT be inherited, or the child holds a copy of them and the
                // read end never sees EOF.
                SetHandleInformation(m_stdin, HANDLE_FLAG_INHERIT, 0);
                SetHandleInformation(m_stdout, HANDLE_FLAG_INHERIT, 0);

                STARTUPINFOW si{};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                si.hStdInput = childIn;
                si.hStdOutput = childOut;
                si.hStdError = childOut; // one pipe for both: a second one is a classic deadlock
                si.wShowWindow = SW_HIDE;

                std::wstring cmd = L"\"" + exe.wstring() + L"\"";
                for (const std::string &a : args)
                    cmd += L" " + std::wstring(a.begin(), a.end());
                std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
                cmdBuf.push_back(L'\0');

                PROCESS_INFORMATION pi{};
                BOOL ok = CreateProcessW(exe.wstring().c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW, nullptr, exe.parent_path().wstring().c_str(), &si, &pi);
                // The child owns these now; the parent must let go or reads never terminate.
                CloseHandle(childIn);
                CloseHandle(childOut);
                if (!ok)
                {
                    error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
                    Stop();
                    return false;
                }

                m_process = pi.hProcess;
                CloseHandle(pi.hThread);
                if (m_job)
                    AssignProcessToJobObject(m_job, m_process);
                return true;
#else
                int inPipe[2], outPipe[2];
                if (pipe(inPipe) != 0)
                {
                    error = "pipe failed";
                    return false;
                }
                if (pipe(outPipe) != 0)
                {
                    close(inPipe[0]);
                    close(inPipe[1]);
                    error = "pipe failed";
                    return false;
                }

                pid_t pid = fork();
                if (pid < 0)
                {
                    close(inPipe[0]);
                    close(inPipe[1]);
                    close(outPipe[0]);
                    close(outPipe[1]);
                    error = "fork failed";
                    return false;
                }
                if (pid == 0)
                {
                    prctl(PR_SET_PDEATHSIG, SIGKILL); // die with the game, same intent as the job object
                    dup2(inPipe[0], STDIN_FILENO);
                    dup2(outPipe[1], STDOUT_FILENO);
                    dup2(outPipe[1], STDERR_FILENO);
                    close(inPipe[0]);
                    close(inPipe[1]);
                    close(outPipe[0]);
                    close(outPipe[1]);
                    std::vector<std::string> argv = {exe.string()};
                    argv.insert(argv.end(), args.begin(), args.end());
                    std::vector<char *> raw;
                    for (std::string &a : argv)
                        raw.push_back(a.data());
                    raw.push_back(nullptr);
                    execv(exe.c_str(), raw.data());
                    _exit(127);
                }

                close(inPipe[0]);
                close(outPipe[1]);
                m_stdin = inPipe[1];
                m_stdout = outPipe[0];
                fcntl(m_stdin, F_SETFL, O_NONBLOCK);
                fcntl(m_stdout, F_SETFL, O_NONBLOCK); // read_line / write must never block the frame
                m_pid = pid;
                return true;
#endif
            }

            bool IsRunning()
            {
#if defined(PE_WIN32)
                return m_process && WaitForSingleObject(m_process, 0) == WAIT_TIMEOUT;
#else
                if (m_pid <= 0)
                    return false;
                int status = 0;
                return waitpid(m_pid, &status, WNOHANG) == 0;
#endif
            }

            bool Write(const std::string &text)
            {
#if defined(PE_WIN32)
                if (!m_stdin)
                    return false;
                DWORD written = 0;
                return WriteFile(m_stdin, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
#else
                if (m_stdin < 0)
                    return false;
                return write(m_stdin, text.data(), text.size()) == static_cast<ssize_t>(text.size());
#endif
            }

            // One buffered line, or nullopt when none is complete yet. Never blocks. Callers poll
            // this in a loop until it returns nullopt — an engine mid-search emits many lines per
            // frame and one-per-frame would back the pipe up.
            std::optional<std::string> ReadLine()
            {
                Drain();
                size_t nl = m_buffer.find('\n');
                if (nl == std::string::npos)
                {
                    if (m_buffer.size() > kMaxBuffer)
                        Stop();
                    return std::nullopt;
                }
                std::string line = m_buffer.substr(0, nl);
                m_buffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return line;
            }

            void Stop()
            {
#if defined(PE_WIN32)
                if (m_process)
                {
                    TerminateProcess(m_process, 0);
                    CloseHandle(m_process);
                    m_process = nullptr;
                }
                if (m_stdin)
                {
                    CloseHandle(m_stdin);
                    m_stdin = nullptr;
                }
                if (m_stdout)
                {
                    CloseHandle(m_stdout);
                    m_stdout = nullptr;
                }
#else
                if (m_pid > 0)
                {
                    kill(m_pid, SIGKILL);
                    int status = 0;
                    waitpid(m_pid, &status, 0);
                    m_pid = -1;
                }
                if (m_stdin >= 0)
                {
                    close(m_stdin);
                    m_stdin = -1;
                }
                if (m_stdout >= 0)
                {
                    close(m_stdout);
                    m_stdout = -1;
                }
#endif
                m_buffer.clear();
            }

        private:
            void Drain()
            {
                char chunk[4096];
#if defined(PE_WIN32)
                if (!m_stdout)
                    return;
                for (;;)
                {
                    DWORD available = 0;
                    if (!PeekNamedPipe(m_stdout, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                        return;
                    DWORD read = 0;
                    DWORD want = static_cast<DWORD>(std::min<size_t>(sizeof(chunk), available));
                    if (!ReadFile(m_stdout, chunk, want, &read, nullptr) || read == 0)
                        return;
                    m_buffer.append(chunk, read);
                    if (m_buffer.size() > kMaxBuffer)
                        return; // let ReadLine drain complete lines; kill only if no newline
                }
#else
                if (m_stdout < 0)
                    return;
                for (;;)
                {
                    ssize_t read = ::read(m_stdout, chunk, sizeof(chunk));
                    if (read <= 0)
                        return;
                    m_buffer.append(chunk, static_cast<size_t>(read));
                    if (m_buffer.size() > kMaxBuffer)
                        return;
                }
#endif
            }

            std::string m_buffer;
#if defined(PE_WIN32)
            HANDLE m_job = nullptr;
            HANDLE m_process = nullptr;
            HANDLE m_stdin = nullptr;
            HANDLE m_stdout = nullptr;
#else
            pid_t m_pid = -1;
            int m_stdin = -1;
            int m_stdout = -1;
#endif
        };

        ChildProcess &Child()
        {
            static ChildProcess child;
            return child;
        }
    } // namespace

    static struct ProcessBindings
    {
        ProcessBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table proc = lua.create_named_table("proc");

                // proc.start("Bots/stockfish.exe") -> true | false, error
                proc.set_function("start", [](const std::string &path, sol::optional<sol::table> argsTable,
                                              sol::this_state ts) -> std::tuple<bool, sol::object> {
                    sol::state_view lua(ts);
                    std::filesystem::path exe = ResolveAssetsPath(path);
                    if (!IsUnderAssets(exe))
                        return {false, sol::make_object(lua, "path is outside Assets/")};
                    if (!std::filesystem::exists(exe))
                        return {false, sol::make_object(lua, "not found: " + exe.string())};

                    std::vector<std::string> args;
                    if (argsTable)
                    {
                        for (size_t i = 1; i <= argsTable->size(); ++i)
                        {
                            sol::optional<std::string> a = (*argsTable)[i];
                            if (a)
                                args.push_back(*a);
                        }
                    }

                    std::string error;
                    if (!Child().Start(exe, args, error))
                        return {false, sol::make_object(lua, error)};
                    return {true, sol::nil};
                });

                proc.set_function("write", [](const std::string &text) { return Child().Write(text); });

                proc.set_function("read_line", [](sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    std::optional<std::string> line = Child().ReadLine();
                    return line ? sol::make_object(lua, *line) : sol::make_object(lua, sol::nil);
                });

                proc.set_function("is_running", []() { return Child().IsRunning(); });
                proc.set_function("stop", []() { Child().Stop(); }); });
        }
    } g_processBindings;
} // namespace pe
