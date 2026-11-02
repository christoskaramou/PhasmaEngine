#pragma once

#include "GUI/Widget.h"

namespace pe
{
    class RuntimeUiPalette : public Widget
    {
    public:
        RuntimeUiPalette() : Widget("UI Editor") { m_open = false; }
        void Update() override;
    };
} // namespace pe
