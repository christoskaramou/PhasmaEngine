/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "CameraSystem.h"
#include "ECS/Entity.h"
#include "ECS/Context.h"

namespace pe
{
    CameraSystem::CameraSystem()
    {
        Camera* mainCamera = Context::Get()->MainEntity->CreateComponent<Camera>();
        AttachComponent(mainCamera);
    }

    Camera* CameraSystem::GetCamera(size_t index)
    {
        return GetComponentsOfType<Camera>().at(index);
    }

    void CameraSystem::Init()
    {

    }

    void CameraSystem::Update(double delta)
    {
        std::vector<Camera*>& components = GetComponentsOfType<Camera>();
        for (Camera* camera : components)
        {
            if (camera->IsEnabled())
                camera->Update();
        }
    }

    void CameraSystem::Destroy()
    {
        std::vector<Camera*>& components = GetComponentsOfType<Camera>();
        for (auto camera : components)
            camera->Destroy();
    }
}