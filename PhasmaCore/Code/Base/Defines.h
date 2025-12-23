#pragma once

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

#if PE_ERROR_MESSAGES && _DEBUG

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

        std::string error =
            "Error: " + msgBody +
            "\nFunc: " + func +
            "\nFile: " + file +
            "\nLine: " + std::to_string(line) +
            "\n";

        std::cout << error << std::endl;
        throw std::runtime_error(error);
    }

    inline void PeInfoImpl(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        std::cout << msgBody << std::endl;
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

        throw std::runtime_error(msgBody);
    }

    inline void PeInfoImpl(const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        std::string msgBody = PeVFormat(fmt, args);
        va_end(args);

        std::cout << msgBody << std::endl;
    }

    inline void PeCheckImpl(int res,
                            const char * /*func*/,
                            const char * /*file*/,
                            int /*line*/)
    {
        if (res != 0)
        {
            throw std::runtime_error("error");
        }
    }
#endif

#define PE_DEBUG_MODE 1
#define PE_ERROR_MESSAGES 0
#define PE_DEBUG_MESSENGER 0
#define PE_USE_GLM 1
#define PE_RENDER_DOC 0
} // namespace pe

#define PE_CHECK(res)                                         \
    do                                                        \
    {                                                         \
        pe::PeCheckImpl((res), __func__, __FILE__, __LINE__); \
    } while (0)

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

#define PE_INFO(fmt, ...)                   \
    do                                      \
    {                                       \
        pe::PeInfoImpl(fmt, ##__VA_ARGS__); \
    } while (0)
