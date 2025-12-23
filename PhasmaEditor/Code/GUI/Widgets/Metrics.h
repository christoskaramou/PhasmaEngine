#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class Metrics : public Widget
    {
    public:
        Metrics() : Widget("Metrics") {}
        void Update() override;
    };
} // namespace pe
