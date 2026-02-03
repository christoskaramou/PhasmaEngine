#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Camera;

    class CameraWidget : public Widget
    {
    public:
        CameraWidget();
        ~CameraWidget() override = default;

        void Update() override;
        void DrawEmbed(Camera *camera);
    };
} // namespace pe
