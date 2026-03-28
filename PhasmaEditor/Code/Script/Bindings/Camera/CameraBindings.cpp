#include "Script/ScriptSystem.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static const std::unordered_map<std::string_view, CameraDirection> s_camDirMap = {
        {"forward", CameraDirection::FORWARD},
        {"backward", CameraDirection::BACKWARD},
        {"left", CameraDirection::LEFT},
        {"right", CameraDirection::RIGHT}};

    static struct CameraBindings
    {
        CameraBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua.new_usertype<Camera>("Camera",
                    sol::no_constructor,
                    "get_position", &Camera::GetPosition,
                    "set_position", &Camera::SetPosition,
                    "get_euler", &Camera::GetEuler,
                    "set_euler", &Camera::SetEuler,
                    "get_front", &Camera::GetFront,
                    "get_right", &Camera::GetRight,
                    "get_up", &Camera::GetUp,
                    "get_fov", [](Camera &c) { return glm::degrees(c.Fovx()); },
                    "set_fov", [](Camera &c, float deg) { c.SetFovx(glm::radians(deg)); },
                    "get_near", &Camera::GetNearPlane,
                    "set_near", &Camera::SetNearPlane,
                    "get_far", &Camera::GetFarPlane,
                    "set_far", &Camera::SetFarPlane,
                    "get_speed", &Camera::GetSpeed,
                    "set_speed", &Camera::SetSpeed,
                    "get_rotation_speed", &Camera::GetRotationSpeed,
                    "set_rotation_speed", &Camera::SetRotationSpeed,
                    "get_name", &Camera::GetName,
                    "set_name", &Camera::SetName,
                    "get_aspect", &Camera::GetAspect,
                    "get_view", &Camera::GetView,
                    "get_projection", &Camera::GetProjection,
                    "get_view_projection", &Camera::GetViewProjection,
                    "get_previous_view_projection", &Camera::GetPreviousViewProjection,
                    "get_inv_view", &Camera::GetInvView,
                    "get_inv_projection", &Camera::GetInvProjection,
                    "get_inv_view_projection", &Camera::GetInvViewProjection,
                    "get_jitter", &Camera::GetProjJitter,
                    "set_jitter", &Camera::SetProjJitter,
                    "get_prev_jitter", &Camera::GetPrevProjJitter,
                    "set_prev_jitter", &Camera::SetPrevProjJitter,
                    "move", [](Camera &c, const std::string &dir, sol::optional<float> speed) {
                        auto it = s_camDirMap.find(std::string_view(dir));
                        if (it == s_camDirMap.end()) return;
                        c.Move(it->second, speed.value_or(c.GetSpeed()));
                    },
                    "rotate", [](Camera &c, float xoffset, float yoffset) {
                        c.Rotate(xoffset, yoffset);
                    },
                    "look_at", [](Camera &c, const vec3 &target) {
                        vec3 dir = glm::normalize(target - c.GetPosition());
                        float pitch = -asin(dir.y);
                        float yaw = atan2(dir.x, dir.z);
                        c.SetEuler(vec3(pitch, yaw, 0.0f));
                    },
                    "point_in_frustum", [](Camera &c, const vec3 &point, sol::optional<float> radius) -> bool {
                        return c.PointInFrustum(point, radius.value_or(0.0f));
                    },
                    "aabb_in_frustum", [](Camera &c, sol::table aabb, sol::this_state ts) -> bool {
                        sol::state_view lua(ts);
                        AABB box;
                        box.min = aabb["min"].get<vec3>();
                        box.max = aabb["max"].get<vec3>();
                        return c.AABBInFrustum(box);
                    });

                lua.set_function("get_camera", []() -> Camera * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? r->GetScene().GetActiveCamera() : nullptr;
                }); });
        }
    } s_cameraBindings;
} // namespace pe
