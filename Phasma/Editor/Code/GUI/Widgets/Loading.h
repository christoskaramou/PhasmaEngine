#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Loading : public Widget
    {
    public:
        Loading() : Widget("Loading") {}
        void Update() override;
    };
} // namespace pe
