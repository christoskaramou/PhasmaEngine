#pragma once

namespace pe
{
    template <class Base, class T>
    constexpr void ValidateBaseClass()
    {
        static_assert(std::is_base_of<Base, T>::value,
                      "ValidateBaseClass<Base, T>(): \"Base class of T\" assertion");
    }

    inline void PE_ERROR_IF_DEBUG(bool condition,
                                  const std::string &msg,
                                  const std::string &condStr,
                                  const std::string &func,
                                  const std::string &file,
                                  int line)
    {
        if (condition)
        {
            std::string error = "Error: " + msg +
                                ", \nCondition: " + condStr +
                                ", \nFunction: " + func +
                                ", \nFile: " + file +
                                ", \nLine: " + std::to_string(line) + "\n\n";
            std::cout << error << std::endl;
            exit(-1);
        }
    }

    inline void PE_ERROR_IF_RELEASE(bool condition)
    {
        if (condition)
            exit(-1);
    }

#if _DEBUG
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_DEBUG(condition, msg, #condition, __func__, __FILE__, __LINE__)
#else
#define PE_ERROR_IF(condition, msg) PE_ERROR_IF_RELEASE(condition)
#endif
#define PE_ERROR(msg) PE_ERROR_IF(true, msg)
#define PE_CHECK(res) PE_ERROR_IF(res, "Check result error")
#define PE_DEBUG_MODE 1
#define USE_GLM
// Before enabling dynamic rendering "VUID-vkCmdBindPipeline-pipeline-06196" error must be fixed,
// maybe with multiple command buffers when needed
#define USE_DYNAMIC_RENDERING 0
#define PE_RENDER_DOC 0
}
