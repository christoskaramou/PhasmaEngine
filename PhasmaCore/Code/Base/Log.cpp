#include "Base/Log.h"

namespace pe
{
    std::vector<Log::Callback> Log::s_callbacks;
    std::vector<std::pair<std::string, LogType>> Log::s_earlyLogs;
    FILE* Log::s_file = nullptr;

    std::mutex& GetLogMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    void Log::Init()
    {
        std::lock_guard<std::mutex> lock(GetLogMutex());
        if (!s_file)
        {
#if defined(PE_WIN32)
            s_file = _fsopen("PhasmaEngine.log", "w", _SH_DENYNO);
#else
            s_file = fopen("PhasmaEngine.log", "w");
#endif
        }
    }

    void Log::Attach(Callback cb)
    {
        std::lock_guard<std::mutex> lock(GetLogMutex());
        s_callbacks.push_back(cb);

        // Replay early logs to this new callback
        for (const auto& log : s_earlyLogs)
        {
            cb(log.first, log.second);
        }
    }

    void Log::Dispatch(const std::string& msg, LogType type)
    {
        std::lock_guard<std::mutex> lock(GetLogMutex());
        
        std::string prefix = "";
        std::string colorCodeCout = ""; 
        std::string resetCodeCout = "\033[0m";

        switch(type)
        {
            case LogType::Info: prefix = "[INFO] "; colorCodeCout = "\033[0m";    break; // White/Default
            case LogType::Warn: prefix = "[WARN] "; colorCodeCout = "\033[33m";   break; // Yellow
            case LogType::Error: prefix = "[ERROR] "; colorCodeCout = "\033[31m"; break; // Red
        }

        std::string finalMsg = prefix + msg;

        // Console Output (with ANSI colors if supported, or just text)
        // Windows CMD supports ANSI since Win10 sometimes, but let's stick to simple cout for now or simple codes.
        std::cout << colorCodeCout << finalMsg << resetCodeCout << std::endl;

        // File Output
        if (s_file)
        {
            fprintf(s_file, "%s\n", finalMsg.c_str());
            fflush(s_file);
        }
        else
        {
#if defined(PE_WIN32)
            s_file = _fsopen("PhasmaEngine.log", "a", _SH_DENYNO);
#else
            s_file = fopen("PhasmaEngine.log", "a");
#endif
             if (s_file)
             {
                 fprintf(s_file, "%s\n", finalMsg.c_str());
                 fflush(s_file);
             }
        }

        // Callbacks (e.g. ImGui Console)
        if (s_callbacks.empty())
        {
            s_earlyLogs.push_back({finalMsg, type});
        }
        else
        {
            for (const auto& cb : s_callbacks)
            {
                cb(finalMsg, type); // Pass raw message and type, let widget suffix/prefix/color
            }
        }
    }

    void Log::Info(const std::string& msg)
    {
        Dispatch(msg, LogType::Info);
    }

    void Log::Warn(const std::string& msg)
    {
        Dispatch(msg, LogType::Warn);
    }

    void Log::Error(const std::string& msg)
    {
        Dispatch(msg, LogType::Error);
    }
}
