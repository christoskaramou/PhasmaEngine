#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Properties : public Widget
    {
    public:
        Properties() : Widget("Global Properties") {}
        void Update() override;
    };
} // namespace pe
