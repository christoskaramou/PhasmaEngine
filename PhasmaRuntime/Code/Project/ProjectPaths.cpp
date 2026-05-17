#include "Project/ProjectPaths.h"

#include "Base/Path.h"
#include "Project/Detail/ProjectHelpers.h"

namespace pe
{
    std::filesystem::path ResolveRuntimeStartupPath(const std::filesystem::path &path)
    {
        if (path.empty())
            return {};

        Path::Init();

        const std::filesystem::path normalizedPath = project_detail::Normalize(path);
        if (normalizedPath.is_absolute() || std::filesystem::exists(normalizedPath))
            return normalizedPath;

        const std::filesystem::path fromExecutable =
            project_detail::Normalize(std::filesystem::path(Path::Executable) / normalizedPath);
        if (std::filesystem::exists(fromExecutable))
            return fromExecutable;

        std::filesystem::path fromAssets;
        auto it = normalizedPath.begin();
        if (it != normalizedPath.end() && *it == "Assets")
            ++it;
        for (; it != normalizedPath.end(); ++it)
            fromAssets /= *it;

        return project_detail::Normalize(std::filesystem::path(Path::Assets) / fromAssets);
    }
} // namespace pe
