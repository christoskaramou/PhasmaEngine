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

#if _DEBUG
    inline void PE_ERROR_MSG(const std::string &msg,
                             const std::string &func,
                             const std::string &file,
                             int line)
    {
        std::string error = "Error: " + msg + ", \nFunc: " + func + ", \nFile: " + file + ", \nLine: " + std::to_string(line) + "\n\n";
        std::cout << error << std::endl;
        exit(-1);
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

    inline void PE_CHECK_RESULT(uint32_t res, const std::string &func, const std::string &file, int line)
    {
        if (res != 0)
            PE_ERROR_MSG("Check result error: " + std::to_string(res), func, file, line);
    }

#define PE_CHECK(res) PE_CHECK_RESULT(res, __func__, __FILE__, __LINE__)
#define PE_ERROR(msg) PE_ERROR_MSG(msg, __func__, __FILE__, __LINE__)
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_MSG(condition, msg, __func__, __FILE__, __LINE__)

#else

    inline void PE_CHECK_RESULT(uint32_t res)
    {
        if (res != 0)
            exit(-1);
    }

    inline void PE_ERROR_IF_MSG(bool condition)
    {
        if (condition)
            exit(-1);
    }

#define PE_CHECK(res) PE_CHECK_RESULT(res)
#define PE_ERROR(msg) exit(-1)
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_MSG(condition)

#endif

#define USE_GLM

#define PE_RENDER_DOC 1
}
