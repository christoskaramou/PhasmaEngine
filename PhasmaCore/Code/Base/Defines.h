#pragma once

#define PE_DEBUG_MODE 1
#define PE_LOGGING 1
#define PE_DEBUG_MESSENGER 1
#define PE_USE_GLM 1

namespace pe
{
    template <class Base, class T>
    constexpr void ValidateBaseClass()
    {
        static_assert(std::is_base_of<Base, T>::value,
                      "ValidateBaseClass<Base, T>(): \"Base class of T\" assertion");
    }

    inline std::string PeVFormat(const char *fmt, va_list argsCopy)
    {
        char stackBuf[512];

        va_list argsMeasure;
        va_copy(argsMeasure, argsCopy);
        int needed = vsnprintf(stackBuf, sizeof(stackBuf), fmt, argsMeasure);
        va_end(argsMeasure);

        if (needed < 0)
        {
            return std::string();
        }

        if (needed < static_cast<int>(sizeof(stackBuf)))
        {
            return std::string(stackBuf, static_cast<size_t>(needed));
        }

        std::string out;
        out.resize(static_cast<size_t>(needed) + 1u);
        vsnprintf(out.data(), out.size(), fmt, argsCopy);
        out.resize(static_cast<size_t>(needed));
        return out;
    }

    inline std::string PeFormat(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string s = PeVFormat(fmt, args);
        va_end(args);
        return s;
    }

    inline void PeInfoImpl(const char *fmt, ...)
    {
#if PE_LOGGING
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        Log::Info(msgBody);
#endif
    }

    inline void PeWarnImpl(const char *fmt, ...)
    {
#if PE_LOGGING
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        Log::Warn(msgBody);
#endif
    }

#if defined(PE_DEBUG_MODE)
    inline void PeErrorImpl(const char *func,
                            const char *file,
                            int line,
                            const char *fmt,
                            ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        if (msgBody.empty())
            msgBody = "error";

        std::string msg = "Error in function: " + std::string(func) +
                          "\nFile: " + std::string(file) +
                          "\nLine: " + std::to_string(line) +
                          "\nMessage: " + msgBody;

        Log::Error(msg);
        throw std::runtime_error(msg);
    }

    inline void PeCheckImpl(int res,
                            const char *func,
                            const char *file,
                            int line)
    {
        if (res != 0)
        {
            PeErrorImpl(func, file, line, "Check result error: %d", res);
        }
    }

#else
    inline void PeErrorImpl(const char *, const char *, int, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        if (msgBody.empty())
            msgBody = "error";

        Log::Error(msgBody);
        throw std::runtime_error(msgBody);
    }

    inline void PeCheckImpl(int res,
                            const char * /*func*/,
                            const char * /*file*/,
                            int /*line*/)
    {
        if (res != 0)
        {
            PeErrorImpl("", "", 0, "Check result error: %d", res);
        }
    }
#endif
} // namespace pe

#define PE_ERROR(fmt, ...)                                                 \
    do                                                                     \
    {                                                                      \
        pe::PeErrorImpl(__func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while (0)

#define PE_ERROR_IF(condition, fmt, ...)                                       \
    do                                                                         \
    {                                                                          \
        if (condition)                                                         \
        {                                                                      \
            pe::PeErrorImpl(__func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        }                                                                      \
    } while (0)

#define PE_CHECK(condition)                                 \
    do                                                      \
    {                                                       \
        int res = (condition);                              \
        pe::PeCheckImpl(res, __func__, __FILE__, __LINE__); \
    } while (0)

#define PE_ASSERT(condition, fmt, ...)                                         \
    do                                                                         \
    {                                                                          \
        if (condition)                                                         \
        {                                                                      \
            pe::PeErrorImpl(__func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        }                                                                      \
    } while (0)

#define PE_INFO(fmt, ...)                   \
    do                                      \
    {                                       \
        pe::PeInfoImpl(fmt, ##__VA_ARGS__); \
    } while (0)

#define PE_WARN(fmt, ...)                   \
    do                                      \
    {                                       \
        pe::PeWarnImpl(fmt, ##__VA_ARGS__); \
    } while (0)
