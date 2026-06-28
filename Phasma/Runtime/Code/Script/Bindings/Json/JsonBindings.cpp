#include "Script/ScriptSystem.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace pe
{
    namespace
    {
        using JsonAllocator = rapidjson::Document::AllocatorType;
        using JsonValue = rapidjson::Value;

        bool IsLuaArray(const sol::table &table)
        {
            int count = 0;
            int maxIndex = 0;
            for (const auto &pair : table)
            {
                if (pair.first.get_type() != sol::type::number)
                    return false;
                const lua_Number key = pair.first.as<lua_Number>();
                if (key < 1.0 || std::floor(key) != key)
                    return false;
                ++count;
                maxIndex = std::max(maxIndex, static_cast<int>(key));
            }
            return maxIndex == count;
        }

        bool LuaToJsonValue(sol::object value, JsonValue &out, JsonAllocator &alloc, std::string &error)
        {
            switch (value.get_type())
            {
            case sol::type::lua_nil:
            case sol::type::none:
                out.SetNull();
                return true;
            case sol::type::boolean:
                out.SetBool(value.as<bool>());
                return true;
            case sol::type::number:
            {
                const lua_Number number = value.as<lua_Number>();
                if (std::floor(number) == number && number >= static_cast<lua_Number>(INT64_MIN) &&
                    number <= static_cast<lua_Number>(INT64_MAX))
                    out.SetInt64(static_cast<int64_t>(number));
                else
                    out.SetDouble(static_cast<double>(number));
                return true;
            }
            case sol::type::string:
            {
                const std::string text = value.as<std::string>();
                out.SetString(text.c_str(), static_cast<rapidjson::SizeType>(text.size()), alloc);
                return true;
            }
            case sol::type::table:
            {
                const sol::table table = value.as<sol::table>();
                if (IsLuaArray(table))
                {
                    out.SetArray();
                    for (int index = 1; index <= table.size(); ++index)
                    {
                        JsonValue element;
                        if (!LuaToJsonValue(table.get<sol::object>(index), element, alloc, error))
                            return false;
                        out.PushBack(element, alloc);
                    }
                    return true;
                }

                std::vector<std::string> keys;
                keys.reserve(table.size());
                for (const auto &pair : table)
                {
                    if (!pair.first.is<std::string>())
                    {
                        error = "object keys must be strings";
                        return false;
                    }
                    keys.push_back(pair.first.as<std::string>());
                }
                std::sort(keys.begin(), keys.end());

                out.SetObject();
                for (const std::string &key : keys)
                {
                    JsonValue element;
                    if (!LuaToJsonValue(table.get<sol::object>(key), element, alloc, error))
                        return false;
                    out.AddMember(JsonValue(key.c_str(), static_cast<rapidjson::SizeType>(key.size()), alloc).Move(),
                                  element, alloc);
                }
                return true;
            }
            default:
                error = "unsupported Lua type for JSON encode";
                return false;
            }
        }

        sol::object JsonToLua(const JsonValue &value, sol::state_view lua)
        {
            if (value.IsNull())
                return sol::lua_nil;
            if (value.IsBool())
                return sol::make_object(lua, value.GetBool());
            if (value.IsInt64())
                return sol::make_object(lua, value.GetInt64());
            if (value.IsUint64())
                return sol::make_object(lua, value.GetUint64());
            if (value.IsDouble())
                return sol::make_object(lua, value.GetDouble());
            if (value.IsString())
                return sol::make_object(lua, std::string(value.GetString(), value.GetStringLength()));
            if (value.IsArray())
            {
                sol::table table = lua.create_table();
                for (rapidjson::SizeType i = 0; i < value.Size(); ++i)
                    table[static_cast<int>(i + 1)] = JsonToLua(value[i], lua);
                return sol::make_object(lua, table);
            }
            if (value.IsObject())
            {
                sol::table table = lua.create_table();
                for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
                    table[std::string(it->name.GetString(), it->name.GetStringLength())] = JsonToLua(it->value, lua);
                return sol::make_object(lua, table);
            }
            return sol::lua_nil;
        }
    } // namespace

    static struct JsonBindings
    {
        JsonBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua.set_function("json_encode", [](sol::object value) -> std::string {
                    std::string error;
                    rapidjson::Document document;
                    if (!LuaToJsonValue(value, document, document.GetAllocator(), error))
                        throw sol::error("json encode failed: " + error);

                    rapidjson::StringBuffer buffer;
                    rapidjson::Writer writer(buffer);
                    document.Accept(writer);
                    return std::string(buffer.GetString(), buffer.GetSize());
                });

                lua.set_function("json_decode", [](const std::string &text, sol::this_state ts) -> sol::table {
                    sol::state_view view(ts);
                    sol::table result = view.create_table();
                    if (text.empty())
                    {
                        result["error"] = "empty input";
                        return result;
                    }

                    rapidjson::Document document;
                    document.Parse(text.c_str(), text.size());
                    if (document.HasParseError())
                    {
                        result["error"] = "invalid JSON";
                        return result;
                    }
                    result["value"] = JsonToLua(document, view);
                    return result;
                }); });
        }
    } s_jsonBindings;
} // namespace pe
