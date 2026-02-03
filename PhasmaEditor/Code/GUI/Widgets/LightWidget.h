#pragma once

#include "GUI/Widget.h"
#include "Scene/SelectionManager.h"

namespace pe
{
    struct LightsUBO;

    class LightWidget : public Widget
    {
    public:
        LightWidget();
        void DrawEmbed(LightsUBO *lights, LightType type, int index);
    };
} // namespace pe
