#pragma once

#include <cassert>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "ECS/Context.h"
#include "Scene/Components/SceneComponent.h"
#include "Scene/Components/StaticMeshComponent.h"
#include "Scene/Components/InstancedStaticMeshComponent.h"
#include "Scene/Actor.h"
#include "Render/Proxies/RenderProxyManager.h"

namespace pe::scene
{
    inline void RunSceneProxyExample()
    {
        // Create world context and systems if not already created.
        // Context::Get()->InitSystems();

        // Stub mesh/material handles; in real usage, acquire from asset system.
        MeshHandle mesh{1};
        MaterialHandle mat{2};

        // Resolve mesh bounds: here a unit cube for demonstration.
        RenderProxyManager::Get().SetMeshBoundsResolver([](const MeshHandle &) {
            AABB aabb;
            aabb.min = {-0.5f, -0.5f, -0.5f};
            aabb.max = {0.5f, 0.5f, 0.5f};
            return aabb;
        });

        // Entity with StaticMeshComponent
        {
            auto *actor = Context::Get()->CreateEntity();
            auto *root = actor->CreateComponent<SceneComponent>();
            root->SetRelativeTransform({glm::vec3(1.0f, 2.0f, 3.0f), glm::quat(glm::vec3(0.0f, glm::radians(45.0f), 0.0f)), glm::vec3(2.0f)});

            auto *meshComp = actor->CreateComponent<StaticMeshComponent>();
            meshComp->SetMesh(mesh);
            meshComp->SetMaterial(mat);
            root->AddChild(meshComp);

            // Force update to propagate to proxy
            meshComp->GetWorldTransform();

            // Inspect proxies: expect at least one mesh proxy with the world matrix we set.
            RenderProxyManager::Get().ForEachMeshProxy([](const MeshRenderProxy &proxy) {
                const glm::mat4 &m = proxy.GetWorldMatrix();
                std::cout << "StaticMesh proxy world[3][3]: " << m[3][0] << ", " << m[3][1] << ", " << m[3][2] << "\n";
                const AABB &b = proxy.GetBounds();
                std::cout << "Bounds min: (" << b.min.x << ", " << b.min.y << ", " << b.min.z << ") max: (" << b.max.x << ", " << b.max.y << ", " << b.max.z << ")\n";
            });
        }

        // Entity with InstancedStaticMeshComponent
        {
            auto *actor = Context::Get()->CreateEntity();
            auto *root = actor->CreateComponent<SceneComponent>();
            root->SetRelativeTransform({glm::vec3(-2.0f, 0.0f, 1.0f), glm::quat(glm::vec3(glm::radians(10.0f), 0.0f, 0.0f)), glm::vec3(1.0f)});

            auto *instComp = actor->CreateComponent<InstancedStaticMeshComponent>();
            instComp->SetMesh(mesh);
            instComp->SetMaterial(mat);
            root->AddChild(instComp);

            instComp->AddInstance({glm::vec3(0.0f), glm::quat(glm::vec3(0.0f)), glm::vec3(1.0f)});
            instComp->AddInstance({glm::vec3(2.0f, 0.0f, 0.0f), glm::quat(glm::vec3(glm::radians(30.0f), 0.0f, 0.0f)), glm::vec3(1.0f)});
            instComp->AddInstance({glm::vec3(0.0f, 0.0f, 2.0f), glm::quat(glm::vec3(0.0f, glm::radians(60.0f), 0.0f)), glm::vec3(0.5f)});

            instComp->UpdateInstanceWorldTransforms();

            RenderProxyManager::Get().ForEachInstancedMeshProxy([](const InstancedMeshRenderProxy &proxy) {
                const auto &instances = proxy.GetInstanceTransforms();
                std::cout << "Instanced proxy instance count: " << instances.size() << "\n";
                for (size_t i = 0; i < instances.size(); ++i)
                {
                    const glm::mat4 &m = instances[i];
                    std::cout << "Instance " << i << " world[3][3]: " << m[3][0] << ", " << m[3][1] << ", " << m[3][2] << "\n";
                }
            });
        }
    }
}
