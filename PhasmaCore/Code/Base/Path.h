#pragma once

namespace pe
{
    class Path
    {
    public:
        inline static std::string Root;
        inline static std::string Executable;
        inline static std::string Assets;
        inline static std::string EditorAssets;

        static void Init()
        {
            static std::once_flag s_once;
            std::call_once(s_once,
                           []()
                           {
                               auto normalizeDirectory = [](std::filesystem::path path)
                               {
                                   std::string value = path.lexically_normal().string();
                                   std::replace(value.begin(), value.end(), '\\', '/');
                                   if (!value.empty() && value.back() != '/')
                                       value.push_back('/');
                                   return value;
                               };

#if defined(PE_WIN32)
                               char *str = nullptr;
                               _get_pgmptr(&str);
                               if (str)
                                   Executable = std::filesystem::path(str).remove_filename().string();
#elif defined(PE_ANDROID)
                               const char *storagePath = SDL_AndroidGetInternalStoragePath();
                               if (storagePath && storagePath[0] != '\0')
                                   Executable = storagePath;
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

                               std::filesystem::path executableDir = std::filesystem::path(Executable).lexically_normal();
                               if (executableDir.filename().empty())
                                   executableDir = executableDir.parent_path();

#if defined(PE_ANDROID)
                               Executable = normalizeDirectory(executableDir);
                               Root = Executable;
                               Assets = normalizeDirectory(executableDir / "Assets");
                               EditorAssets.clear();
                               return;
#endif

                               std::filesystem::path rootDir = executableDir;
                               const std::string executableLeaf = executableDir.filename().string();
                               if (executableLeaf == "Editor" || executableLeaf == "Player")
                               {
                                   rootDir = executableDir.parent_path();
                               }
                               else if (executableLeaf == "WebGPU" &&
                                        executableDir.parent_path().filename().string() == "Samples")
                               {
                                   rootDir = executableDir.parent_path().parent_path();
                               }

                               Executable = normalizeDirectory(executableDir);
                               Root = normalizeDirectory(rootDir);

                               const std::filesystem::path rootAssets = rootDir / "Assets";
                               const std::filesystem::path executableAssets = executableDir / "Assets";
                               if (std::filesystem::exists(rootAssets))
                                   Assets = normalizeDirectory(rootAssets);
                               else if (std::filesystem::exists(executableAssets))
                                   Assets = normalizeDirectory(executableAssets);

                               const std::filesystem::path rootEditorAssets = rootDir / "EditorAssets";
                               const std::filesystem::path executableEditorAssets = executableDir / "EditorAssets";
                               if (std::filesystem::exists(rootEditorAssets))
                                   EditorAssets = normalizeDirectory(rootEditorAssets);
                               else if (std::filesystem::exists(executableEditorAssets))
                                   EditorAssets = normalizeDirectory(executableEditorAssets);
                           });
        }

        static const std::string &RootPath()
        {
            Init();
            return Root;
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

        static const std::string &EditorAssetsPath()
        {
            Init();
            return EditorAssets;
        }
    };
} // namespace pe
