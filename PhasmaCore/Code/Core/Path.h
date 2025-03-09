#pragma once

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pe
{
    class Path
    {
    public:
        inline static std::string Executable;

    private:
        // Helper to initialize static variables
        class Constructor
        {
        public:
            Constructor()
            {
#ifdef WIN32
                char str[MAX_PATH];
                GetModuleFileNameA(nullptr, str, MAX_PATH);
                Executable = std::filesystem::path(str).remove_filename().string();
#else
                char str[PATH_MAX];
                ssize_t length = readlink("/proc/self/exe", str, sizeof(str) - 1);
                if (length >= 0)
                {
                    str[length] = '\0'; // Null-terminate the string
                    Executable = std::filesystem::path(str).remove_filename().string();
                }
#endif

                if (!std::filesystem::exists(Executable))
                    Executable = std::filesystem::current_path().string();

                std::replace(Executable.begin(), Executable.end(), '\\', '/');
            }
        };

        inline static Constructor s_constructor;
    };
}
