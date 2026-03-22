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
                    sol::meta_function::unary_minus, [](const vec2 &a) { return vec2(-a.x, -a.y); },
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
                        [](float s, const vec3 &a) { return s * a; },
                        [](const vec3 &a, const vec3 &b) { return a * b; }),
                    sol::meta_function::division, sol::overload(
                        [](const vec3 &a, float s) { return a / s; },
                        [](const vec3 &a, const vec3 &b) { return a / b; }),
                    sol::meta_function::unary_minus, [](const vec3 &a) { return -a; },
                    sol::meta_function::equal_to, [](const vec3 &a, const vec3 &b) { return a == b; },
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
                        [](float s, const vec4 &a) { return s * a; },
                        [](const vec4 &a, const vec4 &b) { return a * b; }),
                    sol::meta_function::division, sol::overload(
                        [](const vec4 &a, float s) { return a / s; },
                        [](const vec4 &a, const vec4 &b) { return a / b; }),
                    sol::meta_function::unary_minus, [](const vec4 &a) { return -a; },
                    sol::meta_function::equal_to, [](const vec4 &a, const vec4 &b) { return a == b; },
                    sol::meta_function::to_string, [](const vec4 &v) {
                        return "vec4(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
                    });
                lua.set_function("vec4", sol::overload(
                    []() { return vec4(0.0f); },
                    [](float s) { return vec4(s); },
                    [](float x, float y, float z, float w) { return vec4(x, y, z, w); },
                    [](const vec3 &v, float w) { return vec4(v, w); }));

                // mat4 type
                lua.new_usertype<mat4>("_mat4_type", sol::no_constructor,
                    sol::meta_function::multiplication, sol::overload(
                        [](const mat4 &a, const mat4 &b) { return a * b; },
                        [](const mat4 &a, const vec4 &b) { return a * b; },
                        [](const mat4 &a, float s) { return a * s; }),
                    sol::meta_function::equal_to, [](const mat4 &a, const mat4 &b) { return a == b; },
                    sol::meta_function::to_string, [](const mat4 &m) {
                        return "mat4(...)";
                    });
                lua.set_function("mat4", sol::overload(
                    []() { return mat4(1.0f); },
                    [](float s) { return mat4(s); }));

                // quat type
                lua.new_usertype<quat>("_quat_type", sol::no_constructor,
                    "x", sol::property([](const quat &q) { return q.x; }, [](quat &q, float val) { q.x = val; }),
                    "y", sol::property([](const quat &q) { return q.y; }, [](quat &q, float val) { q.y = val; }),
                    "z", sol::property([](const quat &q) { return q.z; }, [](quat &q, float val) { q.z = val; }),
                    "w", sol::property([](const quat &q) { return q.w; }, [](quat &q, float val) { q.w = val; }),
                    sol::meta_function::multiplication, sol::overload(
                        [](const quat &a, const quat &b) { return a * b; },
                        [](const quat &a, const vec3 &v) { return a * v; },
                        [](const quat &a, float s) { return a * s; }),
                    sol::meta_function::addition, [](const quat &a, const quat &b) { return a + b; },
                    sol::meta_function::subtraction, [](const quat &a, const quat &b) { return a - b; },
                    sol::meta_function::unary_minus, [](const quat &a) { return -a; },
                    sol::meta_function::equal_to, [](const quat &a, const quat &b) { return a == b; },
                    sol::meta_function::to_string, [](const quat &q) {
                        return "quat(" + std::to_string(q.w) + ", " + std::to_string(q.x) + ", " + std::to_string(q.y) + ", " + std::to_string(q.z) + ")";
                    },
                    "to_euler", [](const quat &q) -> vec3 { return glm::degrees(glm::eulerAngles(q)); },
                    "to_mat4", [](const quat &q) -> mat4 { return glm::mat4_cast(q); },
                    "inverse", [](const quat &q) -> quat { return glm::inverse(q); },
                    "conjugate", [](const quat &q) -> quat { return glm::conjugate(q); },
                    "length", [](const quat &q) -> float { return glm::length(q); },
                    "normalized", [](const quat &q) -> quat { return glm::normalize(q); });

                lua.set_function("quat", sol::overload(
                    []() { return quat(1.0f, 0.0f, 0.0f, 0.0f); },
                    [](float w, float x, float y, float z) { return quat(w, x, y, z); },
                    [](const vec3 &euler_deg) {
                        return glm::quat(vec3(glm::radians(euler_deg.x), glm::radians(euler_deg.y), glm::radians(euler_deg.z)));
                    },
                    [](float angle_deg, const vec3 &axis) {
                        return glm::angleAxis(glm::radians(angle_deg), glm::normalize(axis));
                    }));

                // --- Existing vector functions ---
                lua.set_function("radians", [](float deg) { return glm::radians(deg); });
                lua.set_function("degrees", [](float rad) { return glm::degrees(rad); });
                lua.set_function("normalize", sol::overload(
                    [](const vec3 &v) { return glm::normalize(v); },
                    [](const vec4 &v) { return glm::normalize(v); },
                    [](const quat &q) { return glm::normalize(q); }));
                lua.set_function("length", sol::overload(
                    [](const vec2 &v) { return glm::length(v); },
                    [](const vec3 &v) { return glm::length(v); },
                    [](const vec4 &v) { return glm::length(v); }));
                lua.set_function("distance", sol::overload(
                    [](const vec2 &a, const vec2 &b) { return glm::distance(a, b); },
                    [](const vec3 &a, const vec3 &b) { return glm::distance(a, b); }));
                lua.set_function("dot", sol::overload(
                    [](const vec3 &a, const vec3 &b) { return glm::dot(a, b); },
                    [](const vec4 &a, const vec4 &b) { return glm::dot(a, b); }));
                lua.set_function("cross", [](const vec3 &a, const vec3 &b) { return glm::cross(a, b); });
                lua.set_function("reflect", [](const vec3 &v, const vec3 &n) { return glm::reflect(v, n); });

                // --- Interpolation ---
                lua.set_function("lerp", sol::overload(
                    [](float a, float b, float t) { return glm::mix(a, b, t); },
                    [](const vec2 &a, const vec2 &b, float t) { return glm::mix(a, b, t); },
                    [](const vec3 &a, const vec3 &b, float t) { return glm::mix(a, b, t); },
                    [](const vec4 &a, const vec4 &b, float t) { return glm::mix(a, b, t); }));
                lua.set_function("slerp", [](const quat &a, const quat &b, float t) { return glm::slerp(a, b, t); });

                // --- Clamping ---
                lua.set_function("clamp", sol::overload(
                    [](float x, float lo, float hi) { return glm::clamp(x, lo, hi); },
                    [](const vec3 &v, const vec3 &lo, const vec3 &hi) { return glm::clamp(v, lo, hi); },
                    [](const vec3 &v, float lo, float hi) { return glm::clamp(v, lo, hi); }));
                lua.set_function("saturate", [](float x) { return glm::clamp(x, 0.0f, 1.0f); });
                lua.set_function("math_min", sol::overload(
                    [](float a, float b) { return glm::min(a, b); },
                    [](const vec3 &a, const vec3 &b) { return glm::min(a, b); }));
                lua.set_function("math_max", sol::overload(
                    [](float a, float b) { return glm::max(a, b); },
                    [](const vec3 &a, const vec3 &b) { return glm::max(a, b); }));
                lua.set_function("abs", sol::overload(
                    [](float x) { return glm::abs(x); },
                    [](const vec3 &v) { return glm::abs(v); }));
                lua.set_function("floor", [](float x) { return glm::floor(x); });
                lua.set_function("ceil", [](float x) { return glm::ceil(x); });
                lua.set_function("fract", [](float x) { return glm::fract(x); });
                lua.set_function("sign", [](float x) { return glm::sign(x); });

                // --- Matrix operations ---
                lua.set_function("inverse", sol::overload(
                    [](const mat4 &m) { return glm::inverse(m); },
                    [](const quat &q) { return glm::inverse(q); }));
                lua.set_function("transpose", [](const mat4 &m) { return glm::transpose(m); });
                lua.set_function("determinant", [](const mat4 &m) { return glm::determinant(m); });

                // --- Transform builders ---
                lua.set_function("translate", [](const mat4 &m, const vec3 &v) { return glm::translate(m, v); });
                lua.set_function("rotate", [](const mat4 &m, float angle_deg, const vec3 &axis) {
                    return glm::rotate(m, glm::radians(angle_deg), axis);
                });
                lua.set_function("scale", [](const mat4 &m, const vec3 &v) { return glm::scale(m, v); });
                lua.set_function("look_at", [](const vec3 &eye, const vec3 &target, const vec3 &up) {
                    return glm::lookAt(eye, target, up);
                });
                lua.set_function("perspective", [](float fov_deg, float aspect, float zNear, float zFar) {
                    return glm::perspective(glm::radians(fov_deg), aspect, zNear, zFar);
                }); });
        }
    } s_mathBindings;
} // namespace pe
