#pragma once

namespace pe
{
    template <class Base, class T>
    constexpr void ValidateBaseClass()
    {
        static_assert(std::is_base_of<Base, T>::value,
                      "ValidateBaseClass<Base, T>(): \"Base class of T\" assertion");
    }

#define PE_DEBUG_MODE 1
#define PE_ERROR_MESSAGES 0
#define PE_DEBUG_MESSENGER 0

#if PE_ERROR_MESSAGES && _DEBUG

    inline void PE_ERROR_MSG(const std::string &msg,
                             const std::string &func,
                             const std::string &file,
                             int line)
    {
        std::string error = "Error: " + msg + ", \nFunc: " + func + ", \nFile: " + file + ", \nLine: " + std::to_string(line) + "\n\n";
        std::cout << error << std::endl;
        throw std::runtime_error(error);
    }

    inline void PE_ERROR_IF_MSG(bool condition,
                                const std::string &msg,
                                const std::string &func,
                                const std::string &file,
                                int line)
    {
        if (condition)
            PE_ERROR_MSG(msg, func, file, line);
    }

    inline void PE_CHECK_RESULT(int res, const std::string &func, const std::string &file, int line)
    {
        if (res != 0)
            PE_ERROR_MSG("Check result error: " + std::to_string(res), func, file, line);
    }

#define PE_CHECK(res) PE_CHECK_RESULT(res, __func__, __FILE__, __LINE__)
#define PE_ERROR(msg) PE_ERROR_MSG(msg, __func__, __FILE__, __LINE__)
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_MSG(condition, msg, __func__, __FILE__, __LINE__)
#define PE_INFO(msg) do { std::cout << msg << std::endl; } while (0)

#else

    inline void PE_CHECK_RESULT(int res)
    {
        if (res != 0)
            throw std::runtime_error("error");
    }

    inline void PE_ERROR_IF_MSG(bool condition)
    {
        if (condition)
            throw std::runtime_error("error");
    }

#define PE_CHECK(res) PE_CHECK_RESULT(res)
#define PE_ERROR(msg) do { throw std::runtime_error("error"); } while (0)
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_MSG(condition)
#define PE_INFO(msg) do { std::cout << msg << std::endl; } while (0)

#endif

#define PE_USE_GLM 1

#define PE_RENDER_DOC 1
}
