#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
namespace pe
{
    static struct MathBindings
    {
        MathBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                       {
                // vec2 type
                lua.new_usertype<vec2>("_vec2_type", sol::no_constructor,
                    "x", sol::property([](const vec2 &v) { return v.x; }, [](vec2 &v, float val) { v.x = val; }),
                    "y", sol::property([](const vec2 &v) { return v.y; }, [](vec2 &v, float val) { v.y = val; }),
                    sol::meta_function::addition, [](const vec2 &a, const vec2 &b) { return a + b; },
                    sol::meta_function::subtraction, [](const vec2 &a, const vec2 &b) { return a - b; },
                    sol::meta_function::multiplication, sol::overload(
                        [](const vec2 &a, float s) { return a * s; },
                        [](float s, const vec2 &a) { return s * a; }),
                    sol::meta_function::to_string, [](const vec2 &v) {
                        return "vec2(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
                    });
                lua.set_function("vec2", sol::overload(
                    []() { return vec2(0.0f); },
                    [](float s) { return vec2(s); },
                    [](float x, float y) { return vec2(x, y); }));

                // vec3 type
                lua.new_usertype<vec3>("_vec3_type", sol::no_constructor,
                    "x", sol::property([](const vec3 &v) { return v.x; }, [](vec3 &v, float val) { v.x = val; }),
                    "y", sol::property([](const vec3 &v) { return v.y; }, [](vec3 &v, float val) { v.y = val; }),
                    "z", sol::property([](const vec3 &v) { return v.z; }, [](vec3 &v, float val) { v.z = val; }),
                    sol::meta_function::addition, [](const vec3 &a, const vec3 &b) { return a + b; },
                    sol::meta_function::subtraction, [](const vec3 &a, const vec3 &b) { return a - b; },
                    sol::meta_function::multiplication, sol::overload(
                        [](const vec3 &a, float s) { return a * s; },
                        [](float s, const vec3 &a) { return s * a; }),
                    sol::meta_function::to_string, [](const vec3 &v) {
                        return "vec3(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                    });
                lua.set_function("vec3", sol::overload(
                    []() { return vec3(0.0f); },
                    [](float s) { return vec3(s); },
                    [](float x, float y, float z) { return vec3(x, y, z); }));

                // vec4 type
                lua.new_usertype<vec4>("_vec4_type", sol::no_constructor,
                    "x", sol::property([](const vec4 &v) { return v.x; }, [](vec4 &v, float val) { v.x = val; }),
                    "y", sol::property([](const vec4 &v) { return v.y; }, [](vec4 &v, float val) { v.y = val; }),
                    "z", sol::property([](const vec4 &v) { return v.z; }, [](vec4 &v, float val) { v.z = val; }),
                    "w", sol::property([](const vec4 &v) { return v.w; }, [](vec4 &v, float val) { v.w = val; }),
                    sol::meta_function::addition, [](const vec4 &a, const vec4 &b) { return a + b; },
                    sol::meta_function::subtraction, [](const vec4 &a, const vec4 &b) { return a - b; },
                    sol::meta_function::multiplication, sol::overload(
                        [](const vec4 &a, float s) { return a * s; },
                        [](float s, const vec4 &a) { return s * a; }),
                    sol::meta_function::to_string, [](const vec4 &v) {
                        return "vec4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
                    });
                lua.set_function("vec4", sol::overload(
                    []() { return vec4(0.0f); },
                    [](float s) { return vec4(s); },
                    [](float x, float y, float z, float w) { return vec4(x, y, z, w); }));

                // mat4 type
                lua.new_usertype<mat4>("_mat4_type", sol::no_constructor,
                    sol::meta_function::multiplication, sol::overload(
                        [](const mat4 &a, const mat4 &b) { return a * b; },
                        [](const mat4 &a, const vec4 &b) { return a * b; }),
                    sol::meta_function::to_string, [](const mat4 &m) {
                        return "mat4(...)";
                    });
                lua.set_function("mat4", sol::overload(
                    []() { return mat4(1.0f); },
                    [](float s) { return mat4(s); }));

                lua.set_function("radians", [](float deg) { return glm::radians(deg); });
                lua.set_function("degrees", [](float rad) { return glm::degrees(rad); });
                lua.set_function("normalize", [](const vec3 &v) { return glm::normalize(v); });
                lua.set_function("length", [](const vec3 &v) { return glm::length(v); });
                lua.set_function("distance", [](const vec3 &a, const vec3 &b) { return glm::distance(a, b); });
                lua.set_function("dot", [](const vec3 &a, const vec3 &b) { return glm::dot(a, b); });
                lua.set_function("cross", [](const vec3 &a, const vec3 &b) { return glm::cross(a, b); }); });
        }
    } s_mathBindings;
} // namespace pe
#endif
