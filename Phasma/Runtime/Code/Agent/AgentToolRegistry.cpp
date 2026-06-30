#include "Agent/AgentToolRegistry.h"

namespace pe
{
    AgentToolRegistry &AgentToolRegistry::Instance()
    {
        static AgentToolRegistry s_instance;
        return s_instance;
    }

    void AgentToolRegistry::Register(std::string name, std::string description, nlohmann::json inputSchema,
                                     sol::protected_function handler)
    {
        std::lock_guard lock(m_mtx);
        for (Entry &e : m_tools)
        {
            if (e.name == name)
            {
                e.description = std::move(description);
                e.inputSchema = std::move(inputSchema);
                e.handler = std::move(handler);
                return;
            }
        }
        m_tools.push_back({std::move(name), std::move(description), std::move(inputSchema), std::move(handler)});
    }

    void AgentToolRegistry::Unregister(const std::string &name)
    {
        std::lock_guard lock(m_mtx);
        for (auto it = m_tools.begin(); it != m_tools.end(); ++it)
        {
            if (it->name == name)
            {
                m_tools.erase(it);
                return;
            }
        }
    }

    void AgentToolRegistry::Clear()
    {
        std::lock_guard lock(m_mtx);
        m_tools.clear();
    }

    std::vector<AgentToolRegistry::Meta> AgentToolRegistry::SnapshotMeta() const
    {
        std::lock_guard lock(m_mtx);
        std::vector<Meta> out;
        out.reserve(m_tools.size());
        for (const Entry &e : m_tools)
            out.push_back({e.name, e.description, e.inputSchema});
        return out;
    }

    nlohmann::json AgentToolRegistry::Invoke(sol::state_view lua, const std::string &name,
                                             const nlohmann::json &args, bool &ok, std::string &error) const
    {
        // Copy the handle out under the lock, then call without holding it (the handler may itself
        // register/unregister tools). sol::protected_function copies are cheap ref copies.
        sol::protected_function handler;
        {
            std::lock_guard lock(m_mtx);
            for (const Entry &e : m_tools)
            {
                if (e.name == name)
                {
                    handler = e.handler;
                    break;
                }
            }
        }
        if (!handler.valid())
        {
            ok = false;
            error = "tool not found: " + name;
            return {};
        }

        sol::protected_function_result result = handler(JsonToLua(lua, args));
        if (!result.valid())
        {
            sol::error err = result;
            ok = false;
            error = err.what();
            return {};
        }
        ok = true;
        sol::object ret = result.get<sol::object>();
        return LuaToJson(ret);
    }

    sol::object JsonToLua(sol::state_view lua, const nlohmann::json &value)
    {
        switch (value.type())
        {
        case nlohmann::json::value_t::null:
            return sol::lua_nil;
        case nlohmann::json::value_t::boolean:
            return sol::make_object(lua, value.get<bool>());
        case nlohmann::json::value_t::number_integer:
            return sol::make_object(lua, value.get<int64_t>());
        case nlohmann::json::value_t::number_unsigned:
            return sol::make_object(lua, value.get<uint64_t>());
        case nlohmann::json::value_t::number_float:
            return sol::make_object(lua, value.get<double>());
        case nlohmann::json::value_t::string:
            return sol::make_object(lua, value.get<std::string>());
        case nlohmann::json::value_t::array:
        {
            sol::table t = lua.create_table(static_cast<int>(value.size()), 0);
            int i = 1;
            for (const auto &el : value)
                t[i++] = JsonToLua(lua, el);
            return t;
        }
        case nlohmann::json::value_t::object:
        {
            sol::table t = lua.create_table(0, static_cast<int>(value.size()));
            for (auto it = value.begin(); it != value.end(); ++it)
                t[it.key()] = JsonToLua(lua, it.value());
            return t;
        }
        default:
            return sol::lua_nil;
        }
    }

    nlohmann::json LuaToJson(const sol::object &value)
    {
        switch (value.get_type())
        {
        case sol::type::lua_nil:
        case sol::type::none:
            return nullptr;
        case sol::type::boolean:
            return value.as<bool>();
        case sol::type::number:
        {
            // Preserve integers as integers; everything else stays a double.
            double d = value.as<double>();
            int64_t i = static_cast<int64_t>(d);
            if (static_cast<double>(i) == d)
                return i;
            return d;
        }
        case sol::type::string:
            return value.as<std::string>();
        case sol::type::table:
        {
            sol::table t = value.as<sol::table>();
            // Distinguish array (contiguous 1..n integer keys) from object.
            const size_t n = t.size();
            bool isArray = n > 0;
            for (size_t i = 1; i <= n && isArray; ++i)
                if (!t[i].valid())
                    isArray = false;
            if (isArray)
            {
                nlohmann::json arr = nlohmann::json::array();
                for (size_t i = 1; i <= n; ++i)
                    arr.push_back(LuaToJson(t[i].get<sol::object>()));
                return arr;
            }
            nlohmann::json obj = nlohmann::json::object();
            for (auto &kv : t)
            {
                if (kv.first.get_type() == sol::type::string)
                    obj[kv.first.as<std::string>()] = LuaToJson(kv.second);
                else if (kv.first.get_type() == sol::type::number)
                    obj[std::to_string(kv.first.as<int64_t>())] = LuaToJson(kv.second);
            }
            return obj;
        }
        default:
            return nullptr;
        }
    }
} // namespace pe
