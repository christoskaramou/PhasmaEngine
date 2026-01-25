#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class CameraWidget : public Widget
    {
    public:
        CameraWidget();
        ~CameraWidget() override = default;

        void Update() override;
    };
} // namespace pe
