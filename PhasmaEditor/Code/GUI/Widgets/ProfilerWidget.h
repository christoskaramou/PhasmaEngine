#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class ProfilerWidget : public Widget
    {
    public:
        ProfilerWidget() : Widget("Profiler") { m_open = false; }
        void Update() override;
    };
} // namespace pe
