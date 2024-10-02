#pragma once

#include "Camera/Camera.h"

namespace pe
{
    class CameraSystem : public ISystem
    {
    public:
        CameraSystem();
        ~CameraSystem() override = default;

        Camera *GetCamera(size_t index);
        void Init(CommandBuffer *cmd) override;
        void Update(double delta) override;
        void Destroy() override;
    };
}