#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Properties : public Widget
    {
    public:
        Properties() : Widget("Properties") {}
        void Update() override;
    };
} // namespace pe
