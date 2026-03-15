#include "ToolRegistry.h"

namespace pagent
{
    void ToolRegistry::Register(ToolDefinition tool)
    {
        std::lock_guard lock(m_mutex);
        m_tools[tool.name] = std::move(tool);
    }

    void ToolRegistry::Unregister(const std::string &name)
    {
        std::lock_guard lock(m_mutex);
        m_tools.erase(name);
    }

    void ToolRegistry::Clear()
    {
        std::lock_guard lock(m_mutex);
        m_tools.clear();
    }

    std::string ToolRegistry::GenerateSchemaJson(const IProviderBackend &backend) const
    {
        std::lock_guard lock(m_mutex);
        std::vector<ToolDefinition> all;
        all.reserve(m_tools.size());
        for (const auto &[name, def] : m_tools)
            all.push_back(def);
        return backend.BuildToolsJson(all);
    }

    std::string ToolRegistry::Dispatch(const std::string &name, const std::string &input_json) const
    {
        std::unique_lock lock(m_mutex);
        
        // fuzzy lookup (handle prefixes like "default_api:" or "google:")
        std::string searchName = name;
        auto it = m_tools.find(searchName);
        if (it == m_tools.end())
        {
            auto colonPos = searchName.find(':');
            if (colonPos != std::string::npos)
            {
                searchName = searchName.substr(colonPos + 1);
                it = m_tools.find(searchName);
            }
        }

        if (it == m_tools.end())
            return "{\"error\":\"unknown tool: " + name + "\"}";

        auto handler = it->second.handler; // copy so we can release the lock before calling
        lock.unlock();

        try
        {
            return handler(input_json);
        }
        catch (const std::exception &ex)
        {
            return std::string("{\"error\":\"") + ex.what() + "\"}";
        }
        catch (...)
        {
            return "{\"error\":\"unknown exception in tool handler\"}";
        }
    }

    std::vector<ToolDefinition> ToolRegistry::GetAll() const
    {
        std::lock_guard lock(m_mutex);
        std::vector<ToolDefinition> out;
        out.reserve(m_tools.size());
        for (const auto &[name, def] : m_tools)
            out.push_back(def);
        return out;
    }
    size_t ToolRegistry::GetToolCount() const
    {
        std::lock_guard lock(m_mutex);
        return m_tools.size();
    }
} // namespace pagent
