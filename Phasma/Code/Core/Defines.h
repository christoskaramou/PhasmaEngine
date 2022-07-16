/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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
            PE_ERROR_MSG("Result error", func, file, line);
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

// Before enabling dynamic rendering "VUID-vkCmdBindPipeline-pipeline-06196" error must be fixed,
// maybe with multiple command buffers when needed
#define USE_DYNAMIC_RENDERING 0

}
