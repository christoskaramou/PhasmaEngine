#include "CameraSystem.h"

namespace pe
{
    CameraSystem::CameraSystem()
    {
        Camera *mainCamera = CreateGlobalComponent<Camera>();
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
}
