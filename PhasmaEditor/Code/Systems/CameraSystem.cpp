#include "CameraSystem.h"
#include "ECS/Context.h"

namespace pe
{
    CameraSystem::CameraSystem()
    {
        Entity *cameraEntity = Context::Get()->CreateEntity();
        Camera *mainCamera = cameraEntity->CreateComponent<Camera>();
        AttachComponent(mainCamera);
    }

    Camera *CameraSystem::GetCamera(size_t index)
    {
        return GetComponentsOfType<Camera>().at(index);
    }

    void CameraSystem::Init(CommandBuffer *cmd)
    {
    }

    void CameraSystem::Update()
    {
        const std::vector<Camera *> &components = GetComponentsOfType<Camera>();
        for (Camera *camera : components)
        {
            if (camera->IsEnabled())
                camera->Update();
        }
    }

    void CameraSystem::Destroy()
    {
        const std::vector<Camera *> &components = GetComponentsOfType<Camera>();
        for (auto camera : components)
            camera->Destroy();
    }
} // namespace pe
