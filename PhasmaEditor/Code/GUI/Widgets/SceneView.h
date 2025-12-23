#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class SceneView : public Widget
    {
    public:
        SceneView() : Widget("Scene View") {}
        void Init(GUI *gui) override;
        void Update() override;
    };
} // namespace pe
