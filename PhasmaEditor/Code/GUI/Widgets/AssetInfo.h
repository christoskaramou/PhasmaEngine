#pragma once
#include "GUI/Widget.h"

namespace pe
{
    class AssetInfo : public Widget
    {
    public:
        AssetInfo() : Widget("Asset Info") {}
        void Update() override;
    };
} // namespace pe
