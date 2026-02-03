#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class GlobalWidget : public Widget
    {
    public:
        GlobalWidget();
        void Update() override;
    };
} // namespace pe
