#pragma once

#include "GUI/Widget.h"

namespace pe
{
    class RuntimeUiPalette : public Widget
    {
    public:
        RuntimeUiPalette() : Widget("UI Editor") {}
        void Update() override;
    };
} // namespace pe
