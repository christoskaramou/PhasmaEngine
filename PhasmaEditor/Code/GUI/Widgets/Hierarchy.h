#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Hierarchy : public Widget
    {
    public:
        Hierarchy();
        void Update() override;
    };
} // namespace pe
