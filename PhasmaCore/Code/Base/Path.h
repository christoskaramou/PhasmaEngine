#pragma once

namespace pe
{
    class Path
    {
    public:
        inline static std::string Executable;
        inline static std::string Assets;

        static void Init()
        {
            static std::once_flag s_once;
            std::call_once(s_once,
                           []()
                           {
#if defined(PE_WIN32)
                               char *str = nullptr;
                               _get_pgmptr(&str);
                               if (str)
                                   Executable = std::filesystem::path(str).remove_filename().string();
#else
                               char str[PATH_MAX];
                               ssize_t length = readlink("/proc/self/exe", str, sizeof(str) - 1);
                               if (length >= 0)
                               {
                                   str[length] = '\0';
                                   Executable = std::filesystem::path(str).remove_filename().string();
                               }
#endif

                               if (!std::filesystem::exists(Executable))
                                   Executable = std::filesystem::current_path().string();

                               std::replace(Executable.begin(), Executable.end(), '\\', '/');

                               if (std::filesystem::exists(Executable + "Assets"))
                                   Assets = Executable + "Assets/";
                           });
        }

        static const std::string &ExecutablePath()
        {
            Init();
            return Executable;
        }

        static const std::string &AssetsPath()
        {
            Init();
            return Assets;
        }
    };
} // namespace pe
