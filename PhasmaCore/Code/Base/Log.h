#pragma once

namespace pe
{
    enum class LogType
    {
        Info,
        Warn,
        Error
    };

    class Log
    {
    public:
        using Callback = std::function<void(const std::string&, LogType)>;

        static void Init();
        static void Info(const std::string& msg);
        static void Warn(const std::string& msg);
        static void Error(const std::string& msg);
        static void Attach(Callback cb);

    private:
        static void Dispatch(const std::string& msg, LogType type);

        static std::vector<Callback> s_callbacks;
        static std::vector<std::pair<std::string, LogType>> s_earlyLogs;
        static FILE* s_file;
    };
}
