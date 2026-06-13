#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Shader.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeShaderStageFlags>
        s_shaderStageMap = {
            {"vertex", PE_SHADER_STAGE_VERTEX},
            {"fragment", PE_SHADER_STAGE_FRAGMENT},
            {"compute", PE_SHADER_STAGE_COMPUTE},
            {"raygen", PE_SHADER_STAGE_RAYGEN_KHR},
            {"miss", PE_SHADER_STAGE_MISS_KHR},
            {"closest_hit", PE_SHADER_STAGE_CLOSEST_HIT_KHR},
            {"any_hit", PE_SHADER_STAGE_ANY_HIT_KHR},
            {"intersection", PE_SHADER_STAGE_INTERSECTION_KHR},
    };

    static std::string ShaderStageToString(PeShaderStageFlags flags)
    {
        for (auto &[k, v] : s_shaderStageMap)
            if (v == flags)
                return std::string(k);
        return "unknown";
    }

    static struct ShaderBindings
    {
        ShaderBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
      sol::usertype<Shader> shaderType =
          lua.new_usertype<Shader>("Shader", sol::no_constructor);

      // --- Static functions ---

      // AddGlobalDefine
      lua.set_function("add_global_define",
                       [](const std::string &name, const std::string &value) {
                         Shader::AddGlobalDefine(name, value);
                       });

      // Factory
      lua.set_function(
          "create_shader",
          [](const std::string &path, const std::string &stage,
             const std::string &entry) -> Shader * {
            auto it = s_shaderStageMap.find(std::string_view(stage));
            if (it == s_shaderStageMap.end()) {
              PE_WARN("[Lua] Unknown shader stage: %s", stage.c_str());
              return nullptr;
            }
            return Shader::Create({.sourcePath = Path::ResolveAsset(path),
                                   .entryPoint = entry,
                                   .stage = it->second});
          });
      lua.set_function("destroy_shader", [](Shader *shader) {
        if (shader)
          Shader::Destroy(shader);
      });

      // --- Instance methods (declaration order) ---

      // GetEntryName
      shaderType["get_entry_name"] = sol::property(&Shader::GetEntryName);

      // GetShaderStage (returns string)
      shaderType["get_shader_stage"] = [](Shader &s) -> std::string {
        return ShaderStageToString(s.GetShaderStage());
      };

      // GetPathID (as string to avoid integer overflow in Lua)
      shaderType["get_path_id"] = sol::property([](Shader &s) -> std::string {
        return std::to_string(s.GetPathID());
      });

      // GetLocalDefines
      shaderType["get_local_defines"] = [](Shader &s,
                                           sol::this_state ts) -> sol::table {
        sol::state_view lua(ts);
        sol::table t = lua.create_table();
        auto &defines = s.GetLocalDefines();
        for (size_t i = 0; i < defines.size(); i++) {
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
