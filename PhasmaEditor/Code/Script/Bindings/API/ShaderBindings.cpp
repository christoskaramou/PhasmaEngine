#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Shader.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::ShaderStageFlagBits> s_shaderStageMap = {
        {"vertex", vk::ShaderStageFlagBits::eVertex},
        {"fragment", vk::ShaderStageFlagBits::eFragment},
        {"compute", vk::ShaderStageFlagBits::eCompute},
        {"raygen", vk::ShaderStageFlagBits::eRaygenKHR},
        {"miss", vk::ShaderStageFlagBits::eMissKHR},
        {"closest_hit", vk::ShaderStageFlagBits::eClosestHitKHR},
        {"any_hit", vk::ShaderStageFlagBits::eAnyHitKHR},
        {"intersection", vk::ShaderStageFlagBits::eIntersectionKHR},
    };

    static std::string ShaderStageToString(vk::ShaderStageFlags flags)
    {
        for (auto &[k, v] : s_shaderStageMap)
            if (vk::ShaderStageFlags(v) == flags)
                return std::string(k);
        return "unknown";
    }

    static struct ShaderBindings
    {
        ShaderBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Shader> shaderType = lua.new_usertype<Shader>("Shader", sol::no_constructor);

                // --- Static functions ---

                // AddGlobalDefine
                lua.set_function("add_global_define", [](const std::string &name, const std::string &value) {
                    Shader::AddGlobalDefine(name, value);
                });

                // Factory
                lua.set_function("create_shader", [](const std::string &path, const std::string &stage, const std::string &entry) -> Shader * {
                    auto it = s_shaderStageMap.find(std::string_view(stage));
                    if (it == s_shaderStageMap.end())
                    {
                        PE_WARN("[Lua] Unknown shader stage: %s", stage.c_str());
                        return nullptr;
                    }
                    return Shader::Create(Path::Assets + path, it->second, entry, std::vector<Define>{}, ShaderCodeType::HLSL);
                });
                lua.set_function("destroy_shader", [](Shader *shader) {
                    if (shader) Shader::Destroy(shader);
                });

                // --- Instance methods (declaration order) ---

                // GetEntryName
                shaderType["get_entry_name"] = sol::property(&Shader::GetEntryName);

                // GetShaderStage (returns string)
                shaderType["get_shader_stage"] = [](Shader &s) -> std::string {
                    return ShaderStageToString(s.GetShaderStage());
                };

                // Size (SPIR-V uint32 count)
                shaderType["get_size"] = sol::property(&Shader::Size);

                // BytesCount
                shaderType["get_bytes_count"] = sol::property(&Shader::BytesCount);

                // GetPathID
                shaderType["get_path_id"] = sol::property(&Shader::GetPathID);

                // GetLocalDefines
                shaderType["get_local_defines"] = [&lua](Shader &s) -> sol::table {
                    sol::table t = lua.create_table();
                    auto &defines = s.GetLocalDefines();
                    for (size_t i = 0; i < defines.size(); i++)
                    {
                        sol::table d = lua.create_table();
                        d["name"] = defines[i].name;
                        d["value"] = defines[i].value;
                        t[i + 1] = d;
                    }
                    return t;
                }; });
        }
    } s_shaderBindings;
} // namespace pe
