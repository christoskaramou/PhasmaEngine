#pragma once

#include "Project/ProjectConfig.h"

namespace pe
{
    class RuntimeContext
    {
    public:
        RuntimeContext() = default;
        explicit RuntimeContext(ProjectConfig project);

        [[nodiscard]] const ProjectConfig &Project() const;
        void SetProject(ProjectConfig project);

    private:
        ProjectConfig m_project;
    };
} // namespace pe
