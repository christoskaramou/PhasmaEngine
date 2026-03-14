#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

namespace pe
{
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
                    "get_inv_view", &Camera::GetInvView,
                    "get_inv_projection", &Camera::GetInvProjection,
                    "get_jitter", &Camera::GetProjJitter,
                    "set_jitter", &Camera::SetProjJitter,
                    "get_prev_jitter", &Camera::GetPrevProjJitter,
                    "set_prev_jitter", &Camera::SetPrevProjJitter);

                lua.set_function("get_camera", []() -> Camera * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? r->GetScene().GetActiveCamera() : nullptr;
                }); });
        }
    } s_cameraBindings;
} // namespace pe
#endif
