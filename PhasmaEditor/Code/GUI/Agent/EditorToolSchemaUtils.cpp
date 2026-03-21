#include "EditorToolSchemaUtils.h"

namespace pagent
{
    namespace
    {
        static std::string SchemaTypeToJsonType(const SchemaType type)
        {
            switch (type)
            {
            case SchemaType::String:
                return "string";
            case SchemaType::Number:
                return "number";
            case SchemaType::Boolean:
                return "boolean";
            case SchemaType::Integer:
                return "integer";
            case SchemaType::Array:
                return "array";
            case SchemaType::Object:
                return "object";
            }

            return "string";
        }

        static nlohmann::json BuildToolInputSchema(const ToolDefinition &tool)
        {
            nlohmann::json properties = nlohmann::json::object();
            nlohmann::json required = nlohmann::json::array();
            for (const auto &property : tool.properties)
            {
                nlohmann::json schema = {
                    {"type", SchemaTypeToJsonType(property.type)},
                    {"description", property.description},
                };
                if (property.type == SchemaType::Array)
                {
                    schema["items"] = {
                        {"type", SchemaTypeToJsonType(property.array_item_type)},
                    };
                }

                properties[property.name] = std::move(schema);
                if (property.required)
                    required.push_back(property.name);
            }

            nlohmann::json result = {
                {"type", "object"},
                {"properties", std::move(properties)},
            };
            if (!required.empty())
                result["required"] = std::move(required);
            return result;
        }
    } // namespace

    const ToolDefinition *FindToolDefinition(const std::vector<ToolDefinition> &tools, const std::string &name)
    {
        for (const auto &tool : tools)
        {
            if (tool.name == name)
                return &tool;
        }
        return nullptr;
    }

    nlohmann::json BuildMcpToolList(const std::vector<ToolDefinition> &tools)
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto &tool : tools)
        {
            result.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"inputSchema", BuildToolInputSchema(tool)},
            });
        }
        return result;
    }
} // namespace pagent
