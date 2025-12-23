#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Models : public Widget
    {
    public:
        Models() : Widget("Models") {}
        void Update() override;
    };
} // namespace pe
