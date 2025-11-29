#include "CameraSystem.h"
#include "Scene/SceneNodeComponent.h"
#include "Scene/SceneSystem.h"
#include "ECS/Context.h"

namespace pe
{
    CameraSystem::CameraSystem()
    {
        SceneSystem *sceneSystem = CreateGlobalSystem<SceneSystem>();

        Entity *cameraEntity = Context::Get()->CreateEntity();
        Camera *mainCamera = cameraEntity->CreateComponent<Camera>();
        AttachComponent(mainCamera);

        SceneNodeComponent *sceneNode = cameraEntity->CreateComponent<SceneNodeComponent>();
        sceneNode->Bind(sceneSystem);
        mainCamera->SetSceneNode(sceneNode);
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
