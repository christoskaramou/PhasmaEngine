#pragma once

#include "GUI/Widget.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    struct LightsUBO;
    class LightSystem;

    class LightWidget : public Widget
    {
    public:
        LightWidget();
        void DrawEmbed(LightSystem *ls, LightType type, int index);
    };
} // namespace pe
