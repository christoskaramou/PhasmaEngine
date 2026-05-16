#include "Runtime/RuntimeContext.h"

#include <utility>

namespace pe
{
    RuntimeContext::RuntimeContext(ProjectConfig project) : m_project(std::move(project))
    {
    }

    const ProjectConfig &RuntimeContext::Project() const
    {
        return m_project;
    }

    void RuntimeContext::SetProject(ProjectConfig project)
    {
        m_project = std::move(project);
    }
} // namespace pe
