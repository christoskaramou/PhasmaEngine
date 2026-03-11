#pragma once

#include "PhasmaAgent/Agent.h"
#include <unordered_map>
#include <mutex>

namespace pagent
{
    class ToolRegistry
    {
    public:
        void Register(ToolDefinition tool);
        void Unregister(const std::string &name);
        void Clear();
        std::string GenerateSchemaJson(const IProviderBackend &backend) const;
        std::string Dispatch(const std::string &name, const std::string &input_json) const;
        std::vector<ToolDefinition> GetAll() const;

    private:
        std::unordered_map<std::string, ToolDefinition> m_tools;
        mutable std::mutex m_mutex;
    };
} // namespace pagent
