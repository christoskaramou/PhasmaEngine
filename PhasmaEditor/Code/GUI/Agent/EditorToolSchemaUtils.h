#pragma once

#include "PhasmaAgent/Agent.h"

#include <nlohmann/json.hpp>

namespace pagent
{
    const ToolDefinition *FindToolDefinition(const std::vector<ToolDefinition> &tools, const std::string &name);
    nlohmann::json BuildMcpToolList(const std::vector<ToolDefinition> &tools);
} // namespace pagent
