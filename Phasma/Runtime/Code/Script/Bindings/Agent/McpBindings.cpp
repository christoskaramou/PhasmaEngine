#include "Script/ScriptSystem.h"
#include "Agent/AgentToolRegistry.h"

namespace pe
{
    // `mcp` Lua table: lets a running game script expose its own agent-callable tools. A registered tool
    // shows up in the player's MCP `tools/list` and, when called, runs `handler(args)` on the main thread
    // (args is the JSON the agent passed, marshaled to a Lua table; the return is marshaled back to JSON).
    // Only meaningful when the player MCP server is running (agent_config.json "mcp": true); harmless
    // otherwise (registrations just sit in the registry).
    static struct RuntimeMcpBindings
    {
        RuntimeMcpBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table mcp = lua["mcp"].get_or_create<sol::table>();

                // mcp.register_tool{ name=, description=, schema=, handler=function(args) ... end }
                mcp.set_function("register_tool", [](sol::table def)
                {
                    const std::string name = def.get_or("name", std::string());
                    if (name.empty())
                    {
                        PE_WARN("mcp.register_tool: missing 'name'");
                        return;
                    }
                    sol::object handlerObj = def["handler"];
                    if (handlerObj.get_type() != sol::type::function)
                    {
                        PE_WARN("mcp.register_tool: tool '%s' needs a function 'handler'", name.c_str());
                        return;
                    }
                    const std::string description = def.get_or("description", std::string());
                    nlohmann::json schema;
                    sol::object schemaObj = def["schema"];
                    if (schemaObj.valid() && schemaObj.get_type() == sol::type::table)
                        schema = LuaToJson(schemaObj);

                    AgentToolRegistry::Instance().Register(name, description, std::move(schema),
                                                           handlerObj.as<sol::protected_function>());
                });

                mcp.set_function("unregister_tool", [](const std::string &name)
                {
                    AgentToolRegistry::Instance().Unregister(name);
                }); });
        }
    } g_runtimeMcpBindings;
} // namespace pe
