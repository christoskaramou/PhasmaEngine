#pragma once

#include "GUI/Widget.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    class Scene;

    class LightWidget : public Widget
    {
    public:
        LightWidget();
        void DrawEmbed(Scene *scene, LightType type, int index);
    };
} // namespace pe
